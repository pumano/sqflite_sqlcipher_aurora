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

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-psabi -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "-O1")
set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=address")
set(CMAKE_SHARED_LINKER_FLAGS "-fsanitize=address")

# SQLCipher is a drop-in replacement for SQLite: it provides the same
# C API (sqlite3.h, vendored in include/) and the same sqlite3_* symbols,
# so the plugin simply links against the prebuilt libsqlcipher.so.0.
#
# The prebuilt libraries in prebuilt/<arch>/ were built from the official
# sqlcipher 4.6.1 sources (see prebuilt/BUILD.md) against the system
# OpenSSL 1.1.1w, which is part of the Aurora OS base system
# (libssl.so.1.1 / libcrypto.so.1.1), so nothing else needs to be
# bundled or installed.
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7hl")
    set(_SQLCIPHER_ARCH armv7)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(_SQLCIPHER_ARCH aarch64)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(_SQLCIPHER_ARCH x86_64)
else()
    message(FATAL_ERROR "Unsupported CPU architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_library(sqlcipher_prebuilt SHARED IMPORTED)
set_target_properties(sqlcipher_prebuilt PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${_SQLCIPHER_ARCH}/libsqlcipher.so.0
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
    # The plugin is loaded from <bundle>/lib at runtime, and
    # libsqlcipher.so.0 is copied there as well, so it must find the
    # library relative to its own location.
    BUILD_RPATH "\$ORIGIN"
    INSTALL_RPATH "\$ORIGIN"
)

# PUBLIC (not PRIVATE): the application executable links this plugin as a
# CMake target (flutter's generated_plugins.cmake), so libsqlcipher must
# propagate to the executable's link line for the linker to resolve the
# plugin's DT_NEEDED entry (libsqlcipher.so.0). At runtime the library is
# found next to the plugin via the $ORIGIN rpath set above.
target_link_libraries(${PLUGIN_NAME} PUBLIC sqlcipher_prebuilt)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(${PLUGIN_NAME} PRIVATE PLUGIN_IMPL)

# Ship libsqlcipher.so.0 in the application bundle next to the plugin,
# so the $ORIGIN rpath resolves it at runtime.
if(DEFINED ROOT_PROJECT_BINARY_DIR)
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${_SQLCIPHER_ARCH}/libsqlcipher.so.0
            ${ROOT_PROJECT_BINARY_DIR}/bundle/lib/
        COMMENT "Copying libsqlcipher.so.0 to the application bundle"
    )
endif()
