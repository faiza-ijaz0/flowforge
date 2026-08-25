cmake_minimum_required(VERSION 3.24)

project(FlowForge
  VERSION 0.1.0
  DESCRIPTION "High-performance job processing and workflow orchestration platform"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Build options ---------------------------------------------------------
option(FLOWFORGE_BUILD_TESTS "Build unit tests" ON)
option(FLOWFORGE_BUILD_BENCHMARKS "Build benchmarks" ON)
option(FLOWFORGE_WARNINGS_AS_ERRORS "Treat warnings as errors on first-party targets" OFF)
option(FLOWFORGE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(FLOWFORGE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(FLOWFORGE_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(FLOWFORGE_ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)

include(CompilerWarnings)
include(Sanitizers)

# --- Third-party dependencies ----------------------------------------------
# All dependencies are fetched from source via FetchContent rather than
# assuming a system package manager (vcpkg/conan) is present. This keeps
# the project buildable on a bare machine with only CMake + a C++ compiler,
# at the cost of a slower first configure. PostgreSQL connectivity
# (libpqxx) is deliberately NOT included yet -- see docs/architecture/overview.md
# for why persistence is interface-only in this phase.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.5
  GIT_SHALLOW TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(HTTPLIB_COMPILE OFF CACHE INTERNAL "")
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(spdlog nlohmann_json httplib)

if(FLOWFORGE_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  include(CTest)
endif()

if(FLOWFORGE_BUILD_BENCHMARKS)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
    GIT_SHALLOW TRUE
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

# --- First-party targets ----------------------------------------------------
# clang-tidy is wired up here, not earlier: CMAKE_CXX_CLANG_TIDY is a global
# variable whose value is copied into each target's CXX_CLANG_TIDY property
# at the point the target is created. Including this before the
# FetchContent_MakeAvailable() calls above would run clang-tidy over
# spdlog/nlohmann_json/httplib/googletest/benchmark's own sources too --
# wasted time analyzing code FlowForge doesn't own or control.
include(StaticAnalysis)

add_subdirectory(engine)
add_subdirectory(apps/server)

if(FLOWFORGE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

message(STATUS "FlowForge ${PROJECT_VERSION} configured")
message(STATUS "  C++ standard:        ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:          ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests:         ${FLOWFORGE_BUILD_TESTS}")
message(STATUS "  Build benchmarks:    ${FLOWFORGE_BUILD_BENCHMARKS}")
message(STATUS "  Warnings as errors:  ${FLOWFORGE_WARNINGS_AS_ERRORS}")
message(STATUS "  ASan / UBSan / TSan: ${FLOWFORGE_ENABLE_ASAN} / ${FLOWFORGE_ENABLE_UBSAN} / ${FLOWFORGE_ENABLE_TSAN}")
