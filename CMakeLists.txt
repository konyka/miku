cmake_minimum_required(VERSION 3.16)

project(miku VERSION 0.1.0 LANGUAGES C)

# ── C Standard ──────────────────────────────────────────────
# Compile with the highest available C standard (up to C23)
# while maintaining C99 compatibility in the source code.
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Platform Detection ──────────────────────────────────────
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(MIKU_PLATFORM "linux")
    set(MIKU_LINUX TRUE)
elseif(APPLE)
    set(MIKU_PLATFORM "macos")
    set(MIKU_MACOS TRUE)
elseif(WIN32)
    set(MIKU_PLATFORM "windows")
    set(MIKU_WINDOWS TRUE)
else()
    set(MIKU_PLATFORM "unknown")
endif()

message(STATUS "Miku IM Server v${PROJECT_VERSION}")
message(STATUS "Platform: ${MIKU_PLATFORM}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_ID})")

# ── Git Hash ────────────────────────────────────────────────
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE MIKU_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT MIKU_GIT_HASH)
    set(MIKU_GIT_HASH "unknown")
endif()

# ── Compiler Flags ──────────────────────────────────────────
add_library(miku_compiler_flags INTERFACE)

target_compile_definitions(miku_compiler_flags INTERFACE
    _POSIX_C_SOURCE=200809L
    _GNU_SOURCE
    MIKU_GIT_HASH="${MIKU_GIT_HASH}"
)

target_compile_options(miku_compiler_flags INTERFACE
    # Warnings as errors in CI, strong warnings always
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes
    # Useful warnings
    -Wdouble-promotion -Wformat=2 -Wno-unused-parameter
)

# Architecture-specific optimizations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    target_compile_options(miku_compiler_flags INTERFACE -march=native)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    target_compile_options(miku_compiler_flags INTERFACE -march=armv8-a+crc)
endif()

# ── Build Types ─────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -flto")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG -flto")

# ── Output Directories ──────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Dependencies ────────────────────────────────────────────
# Optional: only required when building storage/network layers
option(MIKU_ENABLE_MONGO    "Enable MongoDB support"     ON)
option(MIKU_ENABLE_REDIS    "Enable Redis support"       ON)
option(MIKU_ENABLE_KAFKA    "Enable Kafka support"       ON)
option(MIKU_ENABLE_S3       "Enable S3/MinIO support"    ON)
option(MIKU_ENABLE_TESTS    "Build test suite"           ON)
option(MIKU_ENABLE_BENCHMARKS "Build benchmarks"         OFF)
option(MIKU_USE_ASAN        "Enable AddressSanitizer"    OFF)

if(MIKU_USE_ASAN AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "ASAN works best with Debug build type")
endif()

# Find packages
find_package(Threads REQUIRED)

if(MIKU_ENABLE_MONGO)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MONGOC IMPORTED_TARGET libmongoc-1.0)
    if(NOT MONGOC_FOUND)
        message(STATUS "MongoDB (libmongoc) not found - MongoDB support disabled")
        set(MIKU_ENABLE_MONGO OFF)
    endif()
endif()

if(MIKU_ENABLE_REDIS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIREDIS IMPORTED_TARGET hiredis)
    if(NOT HIREDIS_FOUND)
        message(STATUS "hiredis not found - Redis support disabled")
        set(MIKU_ENABLE_REDIS OFF)
    endif()
endif()

if(MIKU_ENABLE_KAFKA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RDKAFKA IMPORTED_TARGET rdkafka)
    if(NOT RDKAFKA_FOUND)
        message(STATUS "librdkafka not found - Kafka support disabled")
        set(MIKU_ENABLE_KAFKA OFF)
    endif()
endif()

find_package(ZLIB)
find_package(CURL)

# ── Subdirectories ──────────────────────────────────────────
add_subdirectory(src/foundation)
add_subdirectory(src/runtime)
add_subdirectory(src/protocol)
add_subdirectory(src/storage)
add_subdirectory(src/discovery)
add_subdirectory(src/models)
add_subdirectory(src/services)
add_subdirectory(src/gateway)
add_subdirectory(cmd)

if(MIKU_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Summary ─────────────────────────────────────────────────
message(STATUS "")
message(STATUS "── Miku Build Configuration ─────────────────────")
message(STATUS "  Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:      ${MIKU_PLATFORM}")
message(STATUS "  C Standard:    C${CMAKE_C_STANDARD}")
message(STATUS "  MongoDB:       ${MIKU_ENABLE_MONGO}")
message(STATUS "  Redis:         ${MIKU_ENABLE_REDIS}")
message(STATUS "  Kafka:         ${MIKU_ENABLE_KAFKA}")
message(STATUS "  Tests:         ${MIKU_ENABLE_TESTS}")
message(STATUS "─────────────────────────────────────────────────")
