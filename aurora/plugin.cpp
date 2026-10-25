#include <sqflite_sqlcipher_aurora/plugin.h>

#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <client_wrapper/method_channel.h>
#include <client_wrapper/plugin_registrar.h>

#include "src/sqflite_sqlcipher_aurora_plugin.h"

// True if `sql` is a `ROLLBACK TO [SAVEPOINT] ...` statement, in any of its
// forms: `ROLLBACK TO sp`, `ROLLBACK TRANSACTION TO sp`,
// `ROLLBACK TRANSACTION TO SAVEPOINT sp`, with SQL comments between the
// keywords.
//
// sqflite_common derives the `inTransaction` argument from the statement
// text (`startsWith('rollback')`), so both a plain `ROLLBACK` and
// `ROLLBACK TO savepoint` arrive with `inTransaction: false`. Only the
// former ends the surrounding transaction; the latter must keep it, or a
// transaction the client still considers open would silently turn into
// autocommit.
static bool isRollbackToSavepoint(const std::string& sql) {
  // Skips whitespace and SQL comments (`/* ... */` and `-- ...`), returning
  // the index of the next token.
  const auto skip = [&sql](size_t pos) {
    while (pos < sql.size()) {
      const auto c = static_cast<unsigned char>(sql[pos]);
      if (std::isspace(c)) {
        ++pos;
      } else if (c == '/' && pos + 1 < sql.size() && sql[pos + 1] == '*') {
        const auto end = sql.find("*/", pos + 2);
        pos = (end == std::string::npos) ? sql.size() : end + 2;
      } else if (c == '-' && pos + 1 < sql.size() && sql[pos + 1] == '-') {
        const auto end = sql.find('\n', pos);
        pos = (end == std::string::npos) ? sql.size() : end + 1;
      } else {
        break;
      }
    }
    return pos;
  };
  // True if a whole (case-insensitive) word `word` starts at `pos`.
  const auto isWord = [](const std::string& s, size_t pos, const char* word) {
    const size_t n = std::strlen(word);
    if (pos > s.size() || s.size() - pos < n) {
      return false;
    }
    for (size_t k = 0; k < n; ++k) {
      if (std::toupper(static_cast<unsigned char>(s[pos + k])) != word[k]) {
        return false;
      }
    }
    const size_t end = pos + n;
    return end == s.size() || !std::isalnum(static_cast<unsigned char>(s[end]));
  };
  size_t i = skip(0);
  if (!isWord(sql, i, "ROLLBACK")) {
    return false;
  }
  i = skip(i + std::strlen("ROLLBACK"));
  // `TRANSACTION` is optional and does not change the meaning: both
  // `ROLLBACK TO ...` and `ROLLBACK TRANSACTION TO ...` roll back to a
  // savepoint only and keep the surrounding transaction open.
  if (isWord(sql, i, "TRANSACTION")) {
    i = skip(i + std::strlen("TRANSACTION"));
  }
  return isWord(sql, i, "TO");
}

SqfliteSqlcipherAuroraPlugin::SqfliteSqlcipherAuroraPlugin(cw::PluginRegistrar* registrar)
    : cw::BasicPlugin<>(Channels::Methods, registrar->messenger())
    , app_(registrar->app())
    , dbID_(0)
    , logger_(Logger::Level::None, "sqflite_sqlcipher")
{}

bool SqfliteSqlcipherAuroraPlugin::Preprocess(const SqfliteSqlcipherAuroraPlugin::Call& call, SqfliteSqlcipherAuroraPlugin::MethodResultPtr& result) {
  if (call.method_name() == Methods::OpenDatabase) {
    onOpenDatabaseCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::CloseDatabase) {
    onCloseDatabaseCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::DeleteDatabase) {
    onDeleteDatabaseCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::Execute) {
    onExecuteCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::Query) {
    onQueryCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::QueryCursorNext) {
    onQueryCursorNextCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::Update) {
    onUpdateCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::Insert) {
    onInsertCall(call, std::move(result));
    return false;
  } else if (call.method_name() == Methods::Batch) {
    onBatchCall(call, std::move(result));
    return false;
  }
  return true;
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::HandleMethodCall(const std::string& name, const EncodableValue* arguments) {
  if (name == Methods::PlatformVersion) {
    return onPlatformVersionCall();
  } else if (name == Methods::DatabaseExists) {
    return onDatabaseExistsCall(arguments);
  } else if (name == Methods::DatabasesPath) {
    return onGetDatabasesPathCall();
  } else if (name == Methods::Options) {
    return onOptionsCall(arguments);
  } else if (name == Methods::Debug) {
    return onDebugCall(arguments);
  }

  return Result(cw::NotImplemented());
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::onPlatformVersionCall() {
  std::string result;
  if (app_) {
    cw::ApplicationWrapper::OSVersion version = app_->GetOSVersion();
    result += "Aurora " + std::to_string(version.major) +
              "." + std::to_string(version.minor) +
              "." + std::to_string(version.patch);

  }

  return Result(result);
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::onDatabaseExistsCall(const EncodableValue* arguments) {
  if (const auto dbPath = arguments->GetValue<std::string>(Arguments::Path)) {
    return Result(std::filesystem::exists(*dbPath));
  }
  return Result(cw::FlutterError("No database exists."));
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::onGetDatabasesPathCall() {
  if (app_) {
    const auto type = cw::ApplicationWrapper::DirectoryType::AppLocalData;
    return Result(app_->GetPath(type).string());
  }
  return Result(cw::FlutterError(
      "An error occurred while determining the database directory."));
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::onOptionsCall(const EncodableValue* arguments) {
  if (const auto level = arguments->GetValue<int>(Arguments::LogLevel)) {
      logger_.setLogLevel(static_cast<Logger::Level>(*level));
  }
  return Result();
}

SqfliteSqlcipherAuroraPlugin::Result SqfliteSqlcipherAuroraPlugin::onDebugCall(const EncodableValue* arguments) {
  const auto cmd = arguments->GetValue<std::string>(Arguments::Command).value_or("");

  std::lock_guard lock(mutex_);
  EncodableMap response;

  if (cmd == "get") {
    if (logger_.logLevel() > Logger::Level::None)
      response.emplace(Arguments::LogLevel,
                       EncodableValue(logger_.logLevel()));

    if (!databases_.empty()) {
      cw::EncodableMap databases;

      for (const auto &[id, db] : databases_) {
        cw::EncodableMap info;

        info.emplace(Arguments::Path, db->path());
        info.emplace(Arguments::SingleInstance, db->isSingleInstance());

        if (db->logLevel() > Logger::Level::None)
          info.emplace(Arguments::LogLevel, EncodableValue(db->logLevel()));

        databases.emplace(std::to_string(id), info);
      }

      response.emplace(Arguments::Databases, databases);
    }
  }
  return Result(response);

}

void SqfliteSqlcipherAuroraPlugin::onOpenDatabaseCall(const MethodCall &call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbPath = args->GetValue<std::string>(Arguments::Path).value_or("");
  const auto readOnly = args->GetValue<bool>(Arguments::ReadOnly).value_or(false);
  const auto password =
      args->GetValue<std::string>(Arguments::Password).value_or("");

  const auto inMemory = dbPath.empty() || dbPath == ":memory:";
  auto isSingleInstance =
      args->GetValue<bool>(Arguments::SingleInstance).value_or(false) && !inMemory;

  if (isSingleInstance) {
    const auto db = databaseByPath(dbPath);

    if (db) {
      if (db->isOpen()) {
        logger_.verb() << "re-opened single instance database"
                        << (db->isInTransaction() ? "(in transaction) " : "")
                        << db->id() << " " << db->path() << std::endl;

        result->Success(makeOpenResult(db->id(), true, db->isInTransaction()));
        return;
      }

      logger_.verb() << "single instance database " << db->path()
                      << " not opened" << std::endl;
    }
  }

  const auto db =
      std::make_shared<Database>(++dbID_, dbPath, isSingleInstance, logger_);

  asyncQueue_.push([this, db, readOnly, password, result = std::shared_ptr<MethodResult>(std::move(result))] {
    logger_.sql() << "open database " + db->path() + " (ID=" << db->id() << ")"
                   << std::endl;

    const auto error =
        readOnly ? db->openReadOnly(password) : db->open(password);
    if (error) {
      result->Error(Errors::Sqflite,
                    formatError(Errors::Open, db->path(), error.message()));
      return;
    }

    databaseAdd(db);
    result->Success(makeOpenResult(db->id(), false, false));
  });
}

void SqfliteSqlcipherAuroraPlugin::onCloseDatabaseCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto db = databaseByID(dbID);

  asyncQueue_.push([this, db, dbID, result = std::shared_ptr<MethodResult>(std::move(result))] {
    if (!db) {
      result->Error(Errors::Sqflite,
                    formatError(Errors::Closed, "database closed",
                                "ID=" + std::to_string(dbID) + ")"));
      return;
    }

    logger_.sql() << "closing database with ID=" << db->id() << std::endl;

    const auto error = db->close();
    if (error) {
      result->Error(Errors::Sqflite,
                    formatError(Errors::Close, db->path(), error.message()));
      return;
    }

    databaseRemove(db);
    result->Success();
  });

}

void SqfliteSqlcipherAuroraPlugin::onDeleteDatabaseCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbPath = args->GetValue<std::string>(Arguments::Path).value_or("");
  const auto db = databaseByPath(dbPath);

  asyncQueue_.push([this, db, dbPath, result = std::shared_ptr<MethodResult>(std::move(result))] {
    if (db) {
      if (db->isOpen()) {
        logger_.verb() << "close database " << db->path()
                        << " (ID=" << db->id() << ")" << std::endl;

        const auto error = db->close();
        if (error) {
          result->Error(Errors::Sqflite, formatError(Errors::Close, db->path(),
                                                     error.message()));
          return;
        }
      }

      databaseRemove(db);
    }

    // Delete the database file and every sidecar. A clean close normally
    // merges the WAL and removes -wal/-shm, but a crash or unclean close (or
    // a rollback journal) can leave them behind; deleteDatabase must leave no
    // trace. The non-throwing overloads (std::error_code) are used so that a
    // filesystem failure (e.g. a sidecar path that is a directory, or a
    // directory without write permission) becomes a reported error instead of
    // an exception lost in the async queue, which would hang the Dart Future.
    auto removeFile = [this, result](const std::filesystem::path& path,
                                     const char* what) {
      std::error_code ec;
      if (!std::filesystem::exists(path, ec)) {
        if (ec) {
          result->Error(Errors::Internal, formatError(Errors::Internal, path.string(),
                                                      "failed to check for the file: " +
                                                          ec.message()));
        }
        // A missing file is the success case: nothing to delete.
        return !ec;
      }

      logger_.verb() << "delete " << what << " " << path << std::endl;
      if (!std::filesystem::remove(path, ec) || ec) {
        result->Error(Errors::Internal,
                      formatError(Errors::Internal, path.string(),
                                  std::string("failed to remove the ") + what + ": " +
                                              ec.message()));
        return false;
      }
      return true;
    };

    if (!removeFile(dbPath, "the database file") ||
        !removeFile(dbPath + "-journal", "the journal file") ||
        !removeFile(dbPath + "-wal", "the WAL file") ||
        !removeFile(dbPath + "-shm", "the SHM file")) {
      return;  // The error has already been reported.
    }

    result->Success();
  });
}

void SqfliteSqlcipherAuroraPlugin::onExecuteCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto sql = args->GetValue<std::string>(Arguments::Sql).value_or("");
  // Three-valued: nullopt means "no change" (a regular statement inside a
  // transaction does not carry the flag), true = BEGIN, false =
  // COMMIT/ROLLBACK.
  const auto inTransactionChange =
      args->GetValue<bool>(Arguments::InTransaction);
  const auto sqlArgs = args->GetValue<EncodableList>(Arguments::SqlArguments).value_or(EncodableList());
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto enteringTransaction =
      inTransactionChange.has_value() && *inTransactionChange &&
      transactionID == DatabaseTransaction::ID::None;

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  asyncQueue_.push([this, db, sql, sqlArgs, inTransactionChange,
                     enteringTransaction, transactionID, result = std::shared_ptr<MethodResult>(std::move(result))] {
    db->processSqlCommand(transactionID, [this, db, sql, sqlArgs,
                                          inTransactionChange,
                                          enteringTransaction, result] {
      if (enteringTransaction) {
        db->enterInTransaction();
      }
      const auto error = db->execute(sql, sqlArgs);
      if (error) {
        if (inTransactionChange.has_value() && !*inTransactionChange &&
            !isRollbackToSavepoint(sql)) {
          // A failed COMMIT (or a plain ROLLBACK): the Dart client
          // (sqflite_common) marks the transaction closed in a `finally`
          // even when this statement fails and will never reference this id
          // again. If the handle is still inside the transaction (e.g. a
          // deferred constraint rejected the COMMIT), roll it back natively
          // and clear the bookkeeping; keeping the id would leave every
          // subsequent non-transactional command queued behind it forever.
          db->abortTransaction();
        } else {
          // A failed statement does not end an open transaction (SQLite
          // keeps it) — including a failed `ROLLBACK TO savepoint`, which
          // the client flags `inTransaction: false` but which leaves the
          // transaction the client still considers open intact. A failed
          // BEGIN leaves no transaction. Align the bookkeeping with the
          // actual handle state.
          db->resyncTransactionState();
        }
        result->Error(Errors::Sqflite,
                      formatError(Errors::Internal, error.message()),
                      makeSqlErrorDetails(sql, sqlArgs));
        return;
      }
      if (enteringTransaction) {
        result->Success(EncodableMap{
            {Arguments::TransactionId, db->currentTransactionID()}});
        return;
      }
      // An explicit "inTransaction: false" means the statement is expected
      // to end the transaction (COMMIT / plain ROLLBACK). Sync with the
      // actual handle state instead of assuming it did: a `ROLLBACK TO
      // savepoint` is also flagged `inTransaction: false` by the client
      // (the flag is derived from the statement text) but keeps the
      // transaction open, and so would any statement form the parser above
      // does not recognize. If the handle is still inside a transaction the
      // id is kept (a queued foreign command must not run inside it); an
      // absent flag means "no change", so a regular statement inside a
      // transaction keeps the state.
      if (inTransactionChange.has_value() && !*inTransactionChange) {
        db->resyncTransactionState();
      }
      result->Success();
    });
  });
}

void SqfliteSqlcipherAuroraPlugin::onQueryCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto sql = args->GetValue<std::string>(Arguments::Sql).value_or("");
  const auto pageSize = args->GetValue<int>(Arguments::CursorPageSize).value_or(-1);
  const auto sqlArgs = args->GetValue<EncodableList>(Arguments::SqlArguments).value_or(EncodableList());
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  asyncQueue_.push([this, db, sql, sqlArgs, transactionID, pageSize, result = std::shared_ptr<MethodResult>(std::move(result))] {
    db->processSqlCommand(
        transactionID, [this, db, sql, sqlArgs, pageSize, result] {
          EncodableMap response;
          utils::error error;
          if (pageSize <= 0) {
            error = db->query(sql, sqlArgs, response);
          } else {
            error = db->queryWithPageSize(sql, sqlArgs, pageSize, response);
          }
          if (error) {
            result->Error(Errors::Sqflite,
                          formatError(Errors::Internal, error.message()),
                          makeSqlErrorDetails(sql, sqlArgs));
            return;
          }
          result->Success(response);
        });
  });
}

void SqfliteSqlcipherAuroraPlugin::onQueryCursorNextCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto cursorID = args->GetValue<int>(Arguments::CursorId).value_or(-1);
  const auto closeCursor = args->GetValue<bool>(Arguments::Cancel).value_or(false);
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  asyncQueue_.push([this, db, cursorID, closeCursor, transactionID, result = std::shared_ptr<MethodResult>(std::move(result))] {
    db->processSqlCommand(transactionID, [this, db, cursorID, closeCursor,
                                          result] {
      EncodableMap response;
      const auto error = db->queryCursorNext(cursorID, closeCursor, response);
      if (error) {
        result->Error(Errors::Sqflite,
                      formatError(Errors::Internal, error.message()));
        return;
      }
      result->Success(response);
    });
  });
}

void SqfliteSqlcipherAuroraPlugin::onUpdateCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto sql = args->GetValue<std::string>(Arguments::Sql).value_or("");
  const auto sqlArgs = args->GetValue<EncodableList>(Arguments::SqlArguments).value_or(EncodableList());
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  asyncQueue_.push([this, db, sql, sqlArgs, transactionID, result = std::shared_ptr<MethodResult>(std::move(result))] {
    db->processSqlCommand(transactionID, [this, db, sql, sqlArgs, result] {
      int updated = 0;
      const auto error = db->update(sql, sqlArgs, updated);
      if (error) {
        result->Error(Errors::Sqflite,
                      formatError(Errors::Internal, error.message()),
                      makeSqlErrorDetails(sql, sqlArgs));
        return;
      }
      result->Success(updated);
    });
  });
}

void SqfliteSqlcipherAuroraPlugin::onInsertCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto sql = args->GetValue<std::string>(Arguments::Sql).value_or("");
  const auto sqlArgs = args->GetValue<EncodableList>(Arguments::SqlArguments).value_or(EncodableList());
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  asyncQueue_.push([this, db, sql, sqlArgs, transactionID, result = std::shared_ptr<MethodResult>(std::move(result))] {
    db->processSqlCommand(transactionID, [this, db, sql, sqlArgs, result] {
      // 64-bit: rowids above 2^31-1 (e.g. 4294967297) must survive the
      // round trip, so the value is sent as int64, not int32.
      int64_t insertID = 0;
      const auto error = db->insert(sql, sqlArgs, insertID);
      if (error) {
        result->Error(Errors::Sqflite,
                      formatError(Errors::Internal, error.message()),
                      makeSqlErrorDetails(sql, sqlArgs));
        return;
      }
      if (insertID == 0) {
        result->Success();
      } else {
        result->Success(insertID);
      }
    });
  });
}

void SqfliteSqlcipherAuroraPlugin::onBatchCall(const MethodCall& call, std::unique_ptr<MethodResult> result) {
  auto args = call.arguments();
  if (!args) {
    result->Error("Bad arguments", "method invoked without args");
    return;
  }

  const auto dbID = args->GetValue<int>(Arguments::Id).value_or(-1);
  const auto operations = args->GetValue<EncodableList>(Arguments::Operations).value_or(EncodableList());
  const auto noResult = args->GetValue<bool>(Arguments::NoResult).value_or(false);
  const auto continueOnError = args->GetValue<bool>(Arguments::ContinueOnError).value_or(false);
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto db = databaseByID(dbID);

  if (!db) {
    result->Error(Errors::Sqflite,
                  formatError(Errors::Closed, "database closed",
                              "ID=" + std::to_string(dbID) + ")"));
    return;
  }

  std::vector<Database::Operation> dbOperations;

  for (const auto &operation : operations) {
    dbOperations.emplace_back(
        Database::Operation{
            operation.GetValue<std::string>(Arguments::Method).value_or(""),
            operation.GetValue<std::string>(Arguments::Sql).value_or(""),
            operation.GetValue<EncodableList>(Arguments::SqlArguments).value_or(
                EncodableList()),
        });
  }

  asyncQueue_.push([this, db, dbOperations, transactionID, continueOnError,
                     noResult, result = std::shared_ptr<MethodResult>(std::move(result))] {
    const auto command = [this, db, dbOperations, continueOnError, noResult,
                          result = std::shared_ptr<MethodResult>(std::move(result))] {
      EncodableList results;
      const auto error = db->batch(dbOperations, continueOnError, results);
      if (error) {
        result->Error(Errors::Sqflite,
                      formatError(Errors::Internal, error.message()));
        return;
      }
      if (noResult) {
        result->Success();
      } else {
        result->Success(results);
      }
    };
    db->processSqlCommand(transactionID, command);
  });
}

std::shared_ptr<Database> SqfliteSqlcipherAuroraPlugin::databaseByPath(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!singleInstanceDatabases_.count(path)) {
    return nullptr;
  }
  return singleInstanceDatabases_.at(path);
}

std::shared_ptr<Database> SqfliteSqlcipherAuroraPlugin::databaseByID(int64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!databases_.count(id)) {
    return nullptr;
  }
  return databases_.at(id);
}


void SqfliteSqlcipherAuroraPlugin::databaseRemove(std::shared_ptr<Database> db) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db->isSingleInstance()) {
    singleInstanceDatabases_.erase(db->path());
  }
  databases_.erase(db->id());
}

void SqfliteSqlcipherAuroraPlugin::databaseAdd(std::shared_ptr<Database> db) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db->isSingleInstance()) {
    singleInstanceDatabases_.emplace(db->path(), db);
  }

  databases_.emplace(db->id(), db);
}

SqfliteSqlcipherAuroraPlugin::EncodableMap SqfliteSqlcipherAuroraPlugin::makeSqlErrorDetails(
    const std::string &sql, const EncodableList &sqlArgs) {
  return EncodableMap{{Arguments::Sql, sql}, {Arguments::SqlArguments, sqlArgs}};
}

std::string SqfliteSqlcipherAuroraPlugin::formatError(const std::string& error, const std::string& message, const std::string& desc) {
  return error + ": " + message + (desc.empty() ? "" : " (" + desc + ")");
}

SqfliteSqlcipherAuroraPlugin::EncodableMap SqfliteSqlcipherAuroraPlugin::makeOpenResult(std::int64_t dbID, bool recovered, bool recoveredInTransaction) {
  EncodableMap result = {{Arguments::Id, dbID}};
  if (recovered) {
    result.emplace(Arguments::Recovered, true);
  }
  if (recoveredInTransaction) {
    result.emplace(Arguments::RecoveredInTransaction, true);
  }
  return result;
}

void SqfliteSqlcipherAuroraPluginRegister(FlutterAuroraPluginContext* context) {
  auto* registrar = cw::PluginRegistrarManager::GetRegistrar(context);

  registrar->AddPlugin(std::make_unique<SqfliteSqlcipherAuroraPlugin>(registrar));
}
