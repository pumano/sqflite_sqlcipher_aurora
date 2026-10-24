# CMAKE_SYSTEM_PROCESSOR - CPU architecture name (armv7l, aarch64, x86_64)
# CMAKE_BUILD_TYPE       - Build mode (debug, profile, release)
# PSDK_VERSION           - Build psdk exact version
# PSDK_MAJOR             - Build psdk major version

cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME sqflite_sqlcipher_aurora)
set(PLUGIN_NAME  sqflite_sqlcipher_aurora_platform_plugin)

project(${PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-psabi")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# SQLCipher is a drop-in replacement for SQLite: it provides the same
# C API (sqlite3.h) and the same sqlite3_* symbols, so the plugin links
# against the sqlcipher package (provided by conan, see conanfile.py).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher)

set(SOURCES
    async_queue.cpp
    database.cpp
    logger.cpp
    plugin.cpp
    src/async_queue.h
    src/constants.h
    src/database.h
    src/logger.h
    src/sqflite_sqlcipher_aurora_plugin.h
    src/utils.h
)

add_library(${PLUGIN_NAME} SHARED ${SOURCES})

# Add client_wrapper source
include(AuroraClientWrapper)
ADD_CLIENT_WRAPPER_SOURCE(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    # The plugin is loaded from <bundle>/lib at runtime together with
    # the conan-deployed SQLCipher/OpenSSL libraries, so it must find
    # them relative to its own location.
    BUILD_RPATH "\$ORIGIN"
    INSTALL_RPATH "\$ORIGIN"
)

# PUBLIC (not PRIVATE): the application executable links this plugin as a
# CMake target (flutter's generated_plugins.cmake), so the SQLCipher
# libraries must propagate to the executable's link line for the linker
# to resolve the plugin's DT_NEEDED entries (libsqlcipher, libssl,
# libcrypto, libz). At runtime the conan-deploy-libraries script copies
# these libraries next to the plugin, which is covered by the $ORIGIN
# rpath set above.
target_link_libraries(${PLUGIN_NAME} PUBLIC PkgConfig::SQLCIPHER)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(${PLUGIN_NAME} PRIVATE PLUGIN_IMPL)
