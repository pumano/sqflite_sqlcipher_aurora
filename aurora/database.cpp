/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#include "src/constants.h"
#include "src/database.h"

#include <filesystem>
#include <limits>
#include <sstream>

namespace {

void addError(Database::EncodableList &results, const utils::error &error,
              const std::string &sql, const Database::EncodableList &arguments) {
  if (error) {
    Database::EncodableMap errorMap{{"code", "sqlite_error"},
                                    {"message", error.message()},
                                    {"data",
                                     Database::EncodableMap{
                                         {"sql", sql},
                                         {"arguments", arguments},
                                     }}};
    if (error.resultCode()) {
      errorMap.emplace("resultCode", *error.resultCode());
    }
    results.emplace_back(Database::EncodableMap{
        {"error", errorMap},
    });
  }
}

// sqlite3_column_text() returns a NUL-terminated C string, but the value
// itself may contain embedded NUL bytes, so the copy must use the explicit
// length from sqlite3_column_bytes() instead of strlen().
std::string textColumnValue(sqlite3_stmt *stmt, int idx) {
  const char *text =
      reinterpret_cast<const char *>(sqlite3_column_text(stmt, idx));
  return std::string(text, sqlite3_column_bytes(stmt, idx));
}

template <typename T> void addResult(Database::EncodableList &results, const T &result) {
  results.emplace_back(Database::EncodableMap{{"result", result}});
}

std::string encodableValueToString(const Database::EncodableValue &value) {
  if (value.IsNull()) {
    return "null";
  }
  if (const auto boolean = value.GetValue<bool>()) {
    return *boolean ? "true" : "false";
  }
  if (const auto int32Value = value.GetValue<int32_t>()) {
    return std::to_string(*int32Value);
  }
  if (const auto int64Value = value.GetValue<int64_t>()) {
    return std::to_string(*int64Value);
  }
  if (const auto doubleValue = value.GetValue<double>()) {
    std::ostringstream stream;
    stream << *doubleValue;
    return stream.str();
  }
  if (const auto stringValue = value.GetValue<std::string>()) {
    return *stringValue;
  }
  if (const auto listValue = value.GetValue<Database::EncodableList>()) {
    std::ostringstream stream;
    stream << "[";
    for (size_t idx = 0; idx < listValue->size(); ++idx) {
      if (idx > 0) {
        stream << ", ";
      }
      stream << encodableValueToString((*listValue)[idx]);
    }
    stream << "]";
    return stream.str();
  }
  return "<unsupported>";
}

std::string argumentsToString(const Database::EncodableList &arguments) {
  std::ostringstream stream;
  stream << "[";
  for (size_t idx = 0; idx < arguments.size(); ++idx) {
    if (idx > 0) {
      stream << ", ";
    }
    stream << encodableValueToString(arguments[idx]);
  }
  stream << "]";
  return stream.str();
}

utils::error withOperationContext(const utils::error &error,
                                  const std::string &sql,
                                  const Database::EncodableList &arguments) {
  if (!error) {
    return error;
  }

  std::string message = error.message() + " sql '" + sql + "'";
  if (!arguments.empty()) {
    message += " args " + argumentsToString(arguments);
  }

  if (error.resultCode()) {
    return utils::error(message, *error.resultCode());
  }
  return utils::error(message);
}

} /* namespace */

Database::Database(int id, const std::string &path, bool singleInstance,
                   const Logger &logger)
    : id_(id), path_(path), isSingleInstance_(singleInstance),
      logger_(logger.logLevel(), logger.tag() + "-db-" + std::to_string(id)),
      transactionID_(0), currentTransactionID_(DatabaseTransaction::ID::None),
      cursorID_(0), db_(nullptr) {}

Database::~Database() { close(); }

utils::error Database::setKey(const std::string &password) {
  if (password.empty()) {
    return utils::error::none();
  }

  // Escape single quotes and pass the key as a quoted literal.
  std::string sql;
  sql.reserve(password.size() * 2 + 16);
  sql += "PRAGMA key = '";
  for (const char c : password) {
    sql += c;
    if (c == '\'') {
      sql += '\'';
    }
  }
  sql += '\'';

  char *errmsg = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    const auto message = errmsg ? errmsg : sqlite3_errmsg(db_);
    return utils::error(message, rc);
  }
  return utils::error::none();
}

utils::error Database::open(const std::string &password) {
  const auto error = createParentDir();
  if (error)
    return error;

  int result_code =
      sqlite3_open_v2(path_.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (result_code != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  const auto keyError = setKey(password);
  if (keyError) {
    close();
    return keyError;
  }

  isReadOnly_ = false;
  sqlite3_busy_timeout(db_, 1000);
  const auto validationError = validateOpenDatabase();
  if (!validationError) {
    return utils::error::none();
  }

  // Never remove an existing database file: for an encrypted database a
  // missing or wrong key makes a perfectly valid file look corrupted,
  // so "recovery" by removing and recreating the file would silently
  // destroy user data. Opening an unreadable or unopenable file is an
  // error; deleting a database is a separate, explicit action.
  close();
  return validationError;
}

utils::error Database::openReadOnly(const std::string &password) {
  const auto error = createParentDir();
  if (error)
    return error;

  int result_code =
      sqlite3_open_v2(path_.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr);
  if (result_code != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  const auto keyError = setKey(password);
  if (keyError) {
    close();
    return keyError;
  }

  isReadOnly_ = true;
  sqlite3_busy_timeout(db_, 1000);

  // For an encrypted database the key is only proved by a real query;
  // a wrong key would otherwise surface later as "file is not a database".
  if (!password.empty()) {
    const auto validationError = validateOpenDatabase();
    if (validationError) {
      close();
      return validationError;
    }
  }

  return utils::error::none();
}

utils::error Database::close() {
  if (!db_)
    return utils::error::none();

  if (sqlite3_close_v2(db_) != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  db_ = nullptr;
  pendingSqlCallbacks_ = {};

  return utils::error::none();
}

utils::error Database::execute(const std::string &sql,
                               const EncodableList &args) {
  const char *sqlPtr = sql.c_str();
  const char *tail = nullptr;
  bool argsBound = false;

  while (sqlPtr && *sqlPtr != '\0') {
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sqlPtr, -1, &stmt, &tail) != SQLITE_OK)
      return utils::error(currentErrorMessage(), currentErrorCode());

    sqlPtr = tail;

    if (!stmt)
      continue;

    const auto shouldBindArgs =
        !args.empty() && !argsBound && sqlite3_bind_parameter_count(stmt) > 0;
    if (shouldBindArgs) {
      const auto error = bindStmtArgs(stmt, args);
      if (error) {
        sqlite3_finalize(stmt);
        return error;
      }
      argsBound = true;
    }

    while (true) {
      int status = sqlite3_step(stmt);

      if (status == SQLITE_ROW)
        continue;

      if (status == SQLITE_DONE)
        break;

      sqlite3_finalize(stmt);
      return utils::error(currentErrorMessage(), currentErrorCode());
    }

    sqlite3_finalize(stmt);
  }

  if (!args.empty() && !argsBound) {
    return utils::error("column index out of range (code 25)", 25);
  }

  return utils::error::none();
}

utils::error Database::query(const std::string &sql, const EncodableList &args,
                             EncodableMap &result) {
  sqlite3_stmt *stmt = nullptr;

  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  const auto error = bindStmtArgs(stmt, args);

  if (error) {
    sqlite3_finalize(stmt);
    return error;
  }

  EncodableList columns;

  for (int idx = 0; idx < sqlite3_column_count(stmt); idx++)
    columns.emplace_back(sqlite3_column_name(stmt, idx));

  EncodableList rows;

  while (true) {
    int status = sqlite3_step(stmt);

    if (status == SQLITE_ROW) {
      EncodableList row;

      for (size_t idx = 0; idx < columns.size(); idx++) {
        switch (sqlite3_column_type(stmt, idx)) {
        case SQLITE_INTEGER:
          row.emplace_back(sqlite3_column_int64(stmt, idx));
          break;
        case SQLITE_FLOAT:
          row.emplace_back(sqlite3_column_double(stmt, idx));
          break;
        case SQLITE_TEXT:
          row.emplace_back(textColumnValue(stmt, idx));
          break;
        case SQLITE_BLOB: {
          const uint8_t *blob =
              reinterpret_cast<const uint8_t *>(sqlite3_column_blob(stmt, idx));
          std::vector<uint8_t> v(blob, blob + sqlite3_column_bytes(stmt, idx));
          row.emplace_back(v);
          break;
        }
        case SQLITE_NULL: {
          const char *columnDecltype = sqlite3_column_decltype(stmt, idx);

          if (columnDecltype != NULL && std::string("BLOB") == columnDecltype)
            row.emplace_back(std::vector<uint8_t>());
          else
            row.emplace_back(nullptr);

          break;
        }
        default:
          break;
        }
      }

      rows.emplace_back(row);
      continue;
    }

    if (status == SQLITE_DONE)
      break;

    sqlite3_finalize(stmt);
    return utils::error(currentErrorMessage(), currentErrorCode());
  }

  result = EncodableMap{{"columns", columns}, {"rows", rows}};

  sqlite3_finalize(stmt);
  return utils::error::none();
}

utils::error Database::queryWithPageSize(const std::string &sql,
                                         const EncodableList &args,
                                         int64_t pageSize,
                                         EncodableMap &result) {
  sqlite3_stmt *stmt = nullptr;

  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  const auto error = bindStmtArgs(stmt, args);

  if (error) {
    sqlite3_finalize(stmt);
    return error;
  }

  cursorID_ += 1;
  cursors_[cursorID_] = Cursor{cursorID_, stmt, pageSize};

  return resultFromCursor(cursors_[cursorID_], result);
}

utils::error Database::queryCursorNext(int cursorID, bool cancel,
                                       EncodableMap &result) {
  logger_.verb() << "querying cursor next (ID=" << cursorID
                  << "; CANCEL=" << cancel << ")" << std::endl;

  if (!cursors_.count(cursorID))
    return utils::error("cursor not found");

  const Cursor &cursor = cursors_[cursorID];

  if (cancel) {
    closeCursor(cursor);
    return utils::error::none();
  }

  return resultFromCursor(cursor, result);
}

utils::error Database::insert(const std::string &sql, const EncodableList &args,
                              int &insertID) {
  if (isReadOnly_)
    return utils::error("database is readonly");

  const auto error = execute(sql, args);

  if (error)
    return error;

  const int updated = sqlite3_changes(db_);

  logger_.sql() << "rows updated: " << updated << std::endl;

  if (updated == 0) {
    insertID = 0;
    return utils::error::none();
  }

  insertID = sqlite3_last_insert_rowid(db_);
  if (insertID == 0) {
    // Match sqflite compat behavior for tables created WITHOUT ROWID.
    insertID = updated;
  }
  logger_.sql() << "last inserted row id: " << insertID << std::endl;

  return utils::error::none();
}

utils::error Database::update(const std::string &sql, const EncodableList &args,
                              int &updated) {
  if (isReadOnly_)
    return utils::error("database is readonly");

  const auto error = execute(sql, args);

  if (error)
    return error;

  updated = sqlite3_changes(db_);
  logger_.sql() << "rows updated: " << updated << std::endl;

  return utils::error::none();
}

void Database::processSqlCommand(int transactionID,
                                 const SqlCommandCallback &callback) {
  if (currentTransactionID_ == DatabaseTransaction::ID::None) {
    callback();
    return;
  }

  if (transactionID == currentTransactionID_ ||
      transactionID == DatabaseTransaction::ID::Force) {
    callback();

    if (currentTransactionID_ == DatabaseTransaction::ID::None) {
      while (!pendingSqlCallbacks_.empty() && isOpen()) {
        const auto &pendingCallback = pendingSqlCallbacks_.front();
        pendingCallback();
        pendingSqlCallbacks_.pop();
      }
    }

    return;
  }

  pendingSqlCallbacks_.push(callback);
}

void Database::enterInTransaction() {
  transactionID_ += 1;
  currentTransactionID_ = transactionID_;
}

void Database::leaveTransaction() {
  currentTransactionID_ = DatabaseTransaction::ID::None;
}

int Database::currentTransactionID() { return currentTransactionID_; }

bool Database::isOpen() const { return db_ != nullptr; }

const std::string &Database::path() const { return path_; }

bool Database::isSingleInstance() const { return isSingleInstance_; }

bool Database::isReadOnly() const { return isReadOnly_; }

bool Database::isInTransaction() const {
  return currentTransactionID_ != DatabaseTransaction::ID::None;
}

int64_t Database::id() const { return id_; }

Logger::Level Database::logLevel() const { return logger_.logLevel(); }

bool Database::isInMemory() const {
  return path_.empty() || path_ == ":memory:";
}

std::string Database::currentErrorMessage() {
  const auto message = sqlite3_errmsg(db_);
  const auto code = currentErrorCode();

  return std::string(message) + " (code " + std::to_string(code) + ")";
}

int Database::currentErrorCode() {
  return sqlite3_extended_errcode(db_);
}

utils::error Database::createParentDir() {
  if (isInMemory())
    return utils::error::none();

  const auto parentDir = std::filesystem::path(path_).parent_path();

  if (std::filesystem::exists(parentDir))
    return utils::error::none();

  if (std::filesystem::create_directories(parentDir))
    return utils::error::none();

  return utils::error("couldn't create parent directory");
}

utils::error Database::validateOpenDatabase() {
  sqlite3_stmt *stmt = nullptr;
  const auto status =
      sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &stmt, nullptr);
  if (status != SQLITE_OK)
    return utils::error(currentErrorMessage(), currentErrorCode());

  const auto stepStatus = sqlite3_step(stmt);
  if (stepStatus != SQLITE_ROW && stepStatus != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return utils::error(currentErrorMessage(), currentErrorCode());
  }

  sqlite3_finalize(stmt);
  return utils::error::none();
}

utils::error Database::bindStmtArgs(sqlite3_stmt *stmt,
                                    const EncodableList &args) {
  int result = SQLITE_OK;

  for (size_t i = 0; i < args.size(); i++) {
    auto idx = i + 1;
    EncodableValue arg = args[i];

    if (arg.IsNull()) {
      result = sqlite3_bind_null(stmt, idx);
    } else if (const auto val = arg.GetValue<bool>()) {
      result = sqlite3_bind_int(stmt, idx, static_cast<int>(*val));
    } else if (const auto val = arg.GetValue<int32_t>()) {
      result = sqlite3_bind_int(stmt, idx, *val);
    } else if (const auto val = arg.GetValue<int64_t>()) {
      result = sqlite3_bind_int64(stmt, idx, *val);
    } else if (const auto val = arg.GetValue<double>()) {
      result = sqlite3_bind_double(stmt, idx, *val);
    } else if (const auto val = arg.GetValue<std::string>()) {
      const auto &string = *val;
      result = sqlite3_bind_text(stmt, idx, string.c_str(), string.size(),
                                 SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<std::vector<uint8_t>>()) {
      result = sqlite3_bind_blob(stmt, idx, val->data(), val->size(),
                                 SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<std::vector<int32_t>>()) {
      result =
          sqlite3_bind_blob(stmt, idx, val->data(),
                            val->size() * sizeof(int32_t), SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<std::vector<int64_t>>()) {
      result =
          sqlite3_bind_blob(stmt, idx, val->data(),
                            val->size() * sizeof(int64_t), SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<std::vector<float>>()) {
      result = sqlite3_bind_blob(stmt, idx, val->data(),
                                 val->size() * sizeof(float), SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<std::vector<double>>()) {
      result =
          sqlite3_bind_blob(stmt, idx, val->data(),
                            val->size() * sizeof(double), SQLITE_TRANSIENT);
    } else if (const auto val = arg.GetValue<EncodableList>()) {
      /* only bytes list is supported */
      std::vector<uint8_t> container;

      for (const auto &entry : *val) {
        const auto el = entry.GetValue<int>();
        if (!el)
          return utils::error(
              "only list of bytes is supported for statement parameter");

        if (*el < 0 || *el > 255)
          return utils::error(
              "only list of bytes is supported for statement parameter");

        container.push_back(static_cast<uint8_t>(*el));
      }

      result = sqlite3_bind_blob(stmt, idx, container.data(), container.size(),
                                 SQLITE_TRANSIENT);
    } else {
      return utils::error("statement parameter has invalid type");
    }

    if (result != SQLITE_OK)
      return utils::error(currentErrorMessage(), currentErrorCode());
  }

  logger_.sql() << sqlite3_expanded_sql(stmt) << std::endl;
  return utils::error::none();
}

void Database::closeCursor(const Cursor &cursor) {
  logger_.verb() << "closing cursor (ID=" << cursor.id << ")" << std::endl;
  sqlite3_finalize(cursor.stmt);
  cursors_.erase(cursor.id);
}

utils::error Database::resultFromCursor(const Cursor &cursor,
                                        EncodableMap &result) {
  EncodableList columns;

  for (int idx = 0; idx < sqlite3_column_count(cursor.stmt); idx++)
    columns.emplace_back(sqlite3_column_name(cursor.stmt, idx));

  EncodableList rows;
  int status = SQLITE_ROW;

  while (static_cast<int64_t>(rows.size()) < cursor.pageSize) {
    status = sqlite3_step(cursor.stmt);

    if (status == SQLITE_ROW) {
      EncodableList row;

      for (size_t idx = 0; idx < columns.size(); idx++) {
        switch (sqlite3_column_type(cursor.stmt, idx)) {
        case SQLITE_INTEGER:
          row.emplace_back(sqlite3_column_int64(cursor.stmt, idx));
          break;
        case SQLITE_FLOAT:
          row.emplace_back(sqlite3_column_double(cursor.stmt, idx));
          break;
        case SQLITE_TEXT:
          row.emplace_back(textColumnValue(cursor.stmt, idx));
          break;
        case SQLITE_BLOB: {
          const uint8_t *blob = reinterpret_cast<const uint8_t *>(
              sqlite3_column_blob(cursor.stmt, idx));
          std::vector<uint8_t> v(blob,
                                 blob + sqlite3_column_bytes(cursor.stmt, idx));
          row.emplace_back(v);
          break;
        }
        case SQLITE_NULL: {
          const char *columnDecltype =
              sqlite3_column_decltype(cursor.stmt, idx);

          if (columnDecltype != NULL && std::string("BLOB") == columnDecltype)
            row.emplace_back(std::vector<uint8_t>());
          else
            row.emplace_back(nullptr);

          break;
        }
        default:
          break;
        }
      }

      rows.emplace_back(row);
      continue;
    }

    if (status == SQLITE_DONE) {
      closeCursor(cursor);
      break;
    }

    closeCursor(cursor);
    return utils::error(currentErrorMessage(), currentErrorCode());
  }

  result = EncodableMap{{"columns", columns}, {"rows", rows}};

  if (status != SQLITE_DONE)
    result.insert({Arguments::CursorId, cursor.id});

  return utils::error::none();
}

utils::error Database::batch(const std::vector<Operation> &operations,
                             bool continueOnError, EncodableList &results) {
  for (const auto &operation : operations) {
    const auto &method = operation.method;

    if (method == Methods::Insert) {
      int insertID = 0;
      const auto error = insert(operation.sql, operation.arguments, insertID);

      if (error) {
        if (!continueOnError)
          return withOperationContext(error, operation.sql, operation.arguments);

        addError(results, error, operation.sql, operation.arguments);
        continue;
      }

      if (insertID == 0)
        addResult(results, nullptr);
      else
        addResult(results, insertID);

      continue;
    }

    if (method == Methods::Execute) {
      const auto error = execute(operation.sql, operation.arguments);

      if (error) {
        if (!continueOnError)
          return withOperationContext(error, operation.sql, operation.arguments);

        addError(results, error, operation.sql, operation.arguments);
        continue;
      }

      addResult(results, nullptr);
      continue;
    }

    if (method == Methods::Query) {
      EncodableMap result;
      const auto error = query(operation.sql, operation.arguments, result);

      if (error) {
        if (!continueOnError)
          return withOperationContext(error, operation.sql, operation.arguments);

        addError(results, error, operation.sql, operation.arguments);
        continue;
      }

      addResult(results, result);
      continue;
    }

    if (method == Methods::Update) {
      int updated = 0;
      const auto error = update(operation.sql, operation.arguments, updated);

      if (error) {
        if (!continueOnError)
          return withOperationContext(error, operation.sql, operation.arguments);

        addError(results, error, operation.sql, operation.arguments);
        continue;
      }

      addResult(results, updated);
      continue;
    }
  }

  return utils::error::none();
}
