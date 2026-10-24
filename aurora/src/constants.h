/**
 * SPDX-FileCopyrightText: Copyright 2025 Open Mobile Platform LLC <community@omp.ru>
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <string>

namespace Channels {
constexpr auto Methods = "com.davidmartos96.sqflite_sqlcipher";
}  // namespace Channels

namespace Methods {
constexpr auto PlatformVersion = "getPlatformVersion";
constexpr auto DatabasesPath = "getDatabasesPath";
constexpr auto Debug = "debug";
constexpr auto Options = "options";
constexpr auto OpenDatabase = "openDatabase";
constexpr auto CloseDatabase = "closeDatabase";
constexpr auto Insert = "insert";
constexpr auto Execute = "execute";
constexpr auto Query = "query";
constexpr auto QueryCursorNext = "queryCursorNext";
constexpr auto Update = "update";
constexpr auto Batch = "batch";
constexpr auto DeleteDatabase = "deleteDatabase";
constexpr auto DatabaseExists = "databaseExists";
}  // namespace Methods

namespace Arguments {
constexpr auto Id = "id";
constexpr auto Path = "path";
constexpr auto ReadOnly = "readOnly";
constexpr auto Password = "password";
constexpr auto SingleInstance = "singleInstance";
constexpr auto LogLevel = "logLevel";
constexpr auto TransactionId = "transactionId";
constexpr auto InTransaction = "inTransaction";
constexpr auto Recovered = "recovered";
constexpr auto RecoveredInTransaction = "recoveredInTransaction";
constexpr auto Sql = "sql";
constexpr auto SqlArguments = "arguments";
constexpr auto NoResult = "noResult";
constexpr auto ContinueOnError = "continueOnError";
constexpr auto Columns = "columns";
constexpr auto Rows = "rows";
constexpr auto Databases = "databases";
constexpr auto Command = "cmd";
constexpr auto Operations = "operations";
constexpr auto Method = "method";
constexpr auto Result = "result";
constexpr auto Error = "error";
constexpr auto ErrorCode = "code";
constexpr auto ErrorMessage = "message";
constexpr auto ErrorData = "data";
constexpr auto CursorPageSize = "cursorPageSize";
constexpr auto CursorId = "cursorId";
constexpr auto Cancel = "cancel";
}  // namespace Arguments

namespace Errors {
constexpr auto Sqflite = "sqlite_error";
constexpr auto Open = "open_failed";
constexpr auto Close = "close_failed";
constexpr auto Closed = "database_closed";
constexpr auto BadParam = "bad_param";
constexpr auto BadArgs = "bad_arguments";
constexpr auto Internal = "internal";
}  // namespace Errors
