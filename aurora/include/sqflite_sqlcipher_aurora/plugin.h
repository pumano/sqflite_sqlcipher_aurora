#pragma once

#include <flutter_aurora/engine_api.h>

#ifdef PLUGIN_IMPL
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define PLUGIN_EXPORT
#endif

#if defined(__cplusplus)
extern "C" {
#endif

PLUGIN_EXPORT void SqfliteSqlcipherAuroraPluginRegister(
    FlutterAuroraPluginContext* context);

#if defined(__cplusplus)
}  // extern "C"
#endif
