/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC
 * <community@omp.ru> SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <client_wrapper/encodable_value.h>

#include <cstdint>
#include <functional>
#include <queue>
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "utils.h"

class Database final {
public:
  using EncodableValue = cw::EncodableValue;
  using EncodableMap = cw::EncodableMap;
  using EncodableList = cw::EncodableList;

  struct Cursor {
    int64_t id;
    sqlite3_stmt *stmt;
    int64_t pageSize;
  };

  struct Operation {
    std::string method;
    std::string sql;
    EncodableList arguments;
  };

  Database(int id, const std::string &path, bool singleInstance,
           const Logger &logger);
  ~Database();

  utils::error open(const std::string &password = "");
  utils::error openReadOnly(const std::string &password = "");
  utils::error close();
  utils::error execute(const std::string &sql, const EncodableList &args);
  utils::error queryCursorNext(int cursorID, bool cancel, EncodableMap &result);
  utils::error query(const std::string &sql, const EncodableList &args,
                     EncodableMap &result);
  utils::error queryWithPageSize(const std::string &sql,
                                 const EncodableList &args, int64_t pageSize,
                                 EncodableMap &result);
  utils::error insert(const std::string &sql, const EncodableList &args,
                      int64_t &insertID);
  utils::error update(const std::string &sql, const EncodableList &args,
                      int &updated);
  utils::error batch(const std::vector<Operation> &operations,
                     bool continueOnError, EncodableList &results);

  void enterInTransaction();
  void leaveTransaction();
  int currentTransactionID();

  using SqlCommandCallback = std::function<void()>;
  struct PendingSqlCommand {
    int transactionID;
    SqlCommandCallback callback;
  };
  void processSqlCommand(int transactionID, const SqlCommandCallback &callback);
  void drainPendingSqlCallbacks();
  // Align the transaction bookkeeping with the actual state of the SQLite
  // handle (sqlite3_get_autocommit): if the handle is not in a transaction,
  // the bookkeeping must not claim one either.
  void resyncTransactionState();
  // End the transaction if the handle is still inside one (best-effort
  // ROLLBACK) and clear the bookkeeping. Used after a failed COMMIT/
  // ROLLBACK: the Dart client marks the transaction closed and will never
  // reference this id again, so keeping it would block the queued
  // non-transactional commands forever.
  void abortTransaction();

  bool isOpen() const;
  bool isSingleInstance() const;
  bool isInTransaction() const;
  bool isInMemory() const;
  bool isReadOnly() const;

  const std::string &path() const;
  int64_t id() const;
  Logger::Level logLevel() const;

  std::string currentErrorMessage();
  int currentErrorCode();
  utils::error setKey(const std::string &password);
  utils::error createParentDir();
  utils::error validateOpenDatabase();
  utils::error bindStmtArgs(sqlite3_stmt *stmt, const EncodableList &args);

  void closeCursor(const Cursor &cursor);
  utils::error resultFromCursor(const Cursor &cursor, EncodableMap &result);

  int64_t id_;
  std::string path_;
  bool isSingleInstance_;
  bool isReadOnly_;
  Logger logger_;
  int transactionID_;
  int currentTransactionID_;
  std::queue<PendingSqlCommand> pendingSqlCallbacks_;
  int64_t cursorID_;
  std::unordered_map<int64_t, Cursor> cursors_;
  sqlite3 *db_;
};
