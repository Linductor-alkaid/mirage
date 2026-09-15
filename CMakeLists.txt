cmake_minimum_required(VERSION 3.25)

project(Mirage
    VERSION 0.1.0
    DESCRIPTION "Mirage: Linux/Windows desktop host for the Mira agent runtime"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

option(MIRAGE_BUILD_TESTS "Build Mirage tests" ${BUILD_TESTING})
option(MIRAGE_WARNINGS_AS_ERRORS "Treat Mirage warnings as errors" ON)
option(MIRAGE_ENABLE_CLANG_TIDY "Run clang-tidy while compiling Mirage targets" OFF)
option(MIRAGE_VERIFY_DEPENDENCIES
    "Verify pinned third_party commits against dependencies.lock.json" ON)
option(MIRAGE_FETCH_DEPENDENCIES
    "Sync missing pinned submodules at configure time (OFF: verify-only)" ON)
option(MIRAGE_WITH_MIRA_MBEDTLS
    "Build Mira's pinned Mbed TLS channel adapter (needs third_party/mira/third_party/mbedtls)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MirageDependencies)
include(MirageWarnings)

if(MIRAGE_ENABLE_CLANG_TIDY)
    find_program(MIRAGE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${MIRAGE_CLANG_TIDY_EXECUTABLE}")
endif()

# Pinned dependencies are resolved and verified before any target is defined:
# a drifted checkout must fail configure, not the build (DEC-001).
mirage_resolve_pinned_dependencies()

# ---- pinned third_party consumption -----------------------------------------
# Keep dependency tests/benchmarks/examples out of the Mirage build; Mirage
# tests its own contracts in tests/. TLS channel adapters stay out until a
# milestone needs real model-gateway transport (DEC-002).
set(MIRA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MIRA_WITH_MBEDTLS "${MIRAGE_WITH_MIRA_MBEDTLS}" CACHE BOOL "" FORCE)
add_subdirectory(third_party/mira EXCLUDE_FROM_ALL)

set(MIRADOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MIRADOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MIRADOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/mirador EXCLUDE_FROM_ALL)

# ---- Mirage targets ----------------------------------------------------------
add_subdirectory(desktop)
add_subdirectory(runtime)
add_subdirectory(integration)
add_subdirectory(platform)
add_subdirectory(apps)

if(MIRAGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- developer conveniences --------------------------------------------------
add_custom_target(mirage-format-check
    COMMAND "${CMAKE_COMMAND}"
            -DROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckFormat.cmake"
    COMMENT "Checking Mirage C++ formatting"
    VERBATIM)
