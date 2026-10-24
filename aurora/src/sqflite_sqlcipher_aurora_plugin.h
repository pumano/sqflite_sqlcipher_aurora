/**
 * SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <client_wrapper/application_wrapper.h>
#include <client_wrapper/basic_plugin.h>
#include <client_wrapper/encodable_value.h>
#include <client_wrapper/plugin_registrar.h>

#include "async_queue.h"
#include "constants.h"
#include "database.h"

class SqfliteSqlcipherAuroraPlugin final : public cw::BasicPlugin<> {
 public:
  using EncodableValue = cw::EncodableValue;
  using EncodableMap = cw::EncodableMap;
  using EncodableList = cw::EncodableList;

  using MethodCall = cw::MethodCall<>;
  using MethodResult = cw::MethodResult<>;

  explicit SqfliteSqlcipherAuroraPlugin(cw::PluginRegistrar* registrar);

 private:
  bool Preprocess(const Call& call, MethodResultPtr& result) final;
  Result HandleMethodCall(const std::string& name,
                          const EncodableValue* arguments) final;

  Result onPlatformVersionCall();
  Result onDatabaseExistsCall(const EncodableValue* arguments);
  Result onGetDatabasesPathCall();
  Result onOptionsCall(const EncodableValue* arguments);
  Result onDebugCall(const EncodableValue* arguments);

  void onOpenDatabaseCall(const MethodCall& call,
                          std::unique_ptr<MethodResult> result);
  void onCloseDatabaseCall(const MethodCall& call,
                           std::unique_ptr<MethodResult> result);
  void onDeleteDatabaseCall(const MethodCall& call,
                            std::unique_ptr<MethodResult> result);
  void onExecuteCall(const MethodCall& call,
                     std::unique_ptr<MethodResult> result);
  void onQueryCall(const MethodCall& call,
                   std::unique_ptr<MethodResult> result);
  void onQueryCursorNextCall(const MethodCall& call,
                             std::unique_ptr<MethodResult> result);
  void onUpdateCall(const MethodCall& call,
                    std::unique_ptr<MethodResult> result);
  void onInsertCall(const MethodCall& call,
                    std::unique_ptr<MethodResult> result);
  void onBatchCall(const MethodCall& call,
                   std::unique_ptr<MethodResult> result);

  std::shared_ptr<Database> databaseByPath(const std::string& path);
  std::shared_ptr<Database> databaseByID(int64_t id);

  void databaseRemove(std::shared_ptr<Database> db);
  void databaseAdd(std::shared_ptr<Database> db);
  EncodableMap makeSqlErrorDetails(const std::string& sql,
                                   const EncodableList& sqlArgs);

  std::string formatError(const std::string& error, const std::string& message,
                          const std::string& desc = "");

  EncodableMap makeOpenResult(std::int64_t dbID, bool recovered,
                              bool recoveredInTransaction);

  cw::ApplicationWrapper* app_ = nullptr;
  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Database>>
      singleInstanceDatabases_;
  std::unordered_map<int64_t, std::shared_ptr<Database>> databases_;
  int64_t dbID_ = 0;
  Logger logger_;
  bool queryAsMapList_ = false;
  AsyncQueue asyncQueue_;
};
