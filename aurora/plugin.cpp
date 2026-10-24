#include <sqflite_sqlcipher_aurora/plugin.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <client_wrapper/method_channel.h>
#include <client_wrapper/plugin_registrar.h>

#include "src/sqflite_sqlcipher_aurora_plugin.h"

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

    if (std::filesystem::exists(dbPath)) {
      logger_.verb() << "delete not opened database " << dbPath << std::endl;
      std::filesystem::remove(dbPath);
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
  const auto inTransactionChange =
      args->GetValue<bool>(Arguments::InTransaction).value_or(false);
  const auto sqlArgs = args->GetValue<EncodableList>(Arguments::SqlArguments).value_or(EncodableList());
  const auto transactionID =
      args->GetValue<std::int64_t>(Arguments::TransactionId).value_or(DatabaseTransaction::ID::None);

  const auto enteringTransaction =
      inTransactionChange == true &&
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
        db->leaveTransaction();
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
      if (inTransactionChange == false) {
        db->leaveTransaction();
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
