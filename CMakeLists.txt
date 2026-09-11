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
option(FLOWFORGE_WITH_POSTGRES
  "Build the libpqxx-backed PostgreSQL persistence layer (requires a system libpq -- \
see docs/development/getting-started.md, 'PostgreSQL setup')" ON)

include(CompilerWarnings)
include(Sanitizers)

# --- Third-party dependencies ----------------------------------------------
# All dependencies are fetched from source via FetchContent rather than
# assuming a system package manager (vcpkg/conan) is present. This keeps
# the project buildable on a bare machine with only CMake + a C++ compiler,
# at the cost of a slower first configure.
#
# PostgreSQL is the one exception: libpqxx (the C++ client) is fetched via
# FetchContent like everything else, but it links against libpq, the C
# client library, which libpqxx does NOT vendor or build from source --
# it must already exist on the machine (installed via the PostgreSQL
# distribution, libpq-dev, or vcpkg). This is a deliberate, documented
# exception to the "no system package manager required" rule (see
# docs/development/getting-started.md, "PostgreSQL setup"): building a full
# libpq (which itself depends on OpenSSL/zlib) from source via FetchContent
# is a materially different, much heavier undertaking than the header-only
# / self-contained libraries below, and every real PostgreSQL deployment
# environment already has libpq available one way or another.
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

# libpq must be found on the system before we attempt to build libpqxx
# against it. FLOWFORGE_WITH_POSTGRES defaults ON but degrades to OFF (with
# a loud, non-silent warning -- never a silent skip) when libpq isn't
# present, per the "don't force a dependency the environment can't support"
# rule. Explicitly passing -DFLOWFORGE_WITH_POSTGRES=ON keeps the hard
# requirement (configure fails instead of degrading) for CI/production
# builds that must not silently ship without PostgreSQL support.
if(FLOWFORGE_WITH_POSTGRES)
  find_package(PostgreSQL QUIET)
  if(NOT PostgreSQL_FOUND)
    message(WARNING
      "FLOWFORGE_WITH_POSTGRES is ON but no system PostgreSQL installation (libpq) was found by "
      "find_package(PostgreSQL). Disabling PostgreSQL-backed persistence for this configure -- the "
      "server will build with in-memory repositories only. Install PostgreSQL (Windows: the official "
      "installer or 'winget install PostgreSQL.PostgreSQL.17'; Linux: 'apt-get install libpq-dev') and "
      "re-run CMake configure to enable it, or pass -DFLOWFORGE_WITH_POSTGRES=OFF to silence this "
      "warning. See docs/development/getting-started.md, 'PostgreSQL setup'.")
    set(FLOWFORGE_WITH_POSTGRES OFF)
  elseif(WIN32 AND TARGET PostgreSQL::PostgreSQL)
    # The official Windows PostgreSQL distribution ships two link targets
    # for libpq: libpq.a (a full static archive that additionally pulls in
    # OpenSSL's internal symbols, which this project's MinGW/WinLibs
    # toolchain has no linkable, ABI-compatible build of) and libpq.lib (a
    # thin MSVC-style import library for libpq.dll, which needs nothing
    # else -- MinGW's ld can link straight against it). CMake's
    # FindPostgreSQL module prefers the .a; override it to the .lib import
    # library instead. This is Windows/MinGW-specific -- Linux/macOS CI and
    # local builds link the "real" libpq normally, since apt/brew's libpq
    # already resolves its own OpenSSL dependency via the system linker.
    # See docs/development/getting-started.md, "PostgreSQL setup (Windows)".
    get_filename_component(_flowforge_pq_libdir "${PostgreSQL_LIBRARY}" DIRECTORY)
    get_filename_component(_flowforge_pq_libext "${PostgreSQL_LIBRARY}" EXT)
    if(_flowforge_pq_libext STREQUAL ".a" AND EXISTS "${_flowforge_pq_libdir}/libpq.lib")
      message(STATUS "Windows: linking libpq.lib (dynamic import library) instead of the static libpq.a")
      set_target_properties(PostgreSQL::PostgreSQL PROPERTIES IMPORTED_LOCATION "${_flowforge_pq_libdir}/libpq.lib")
      set(PostgreSQL_LIBRARY "${_flowforge_pq_libdir}/libpq.lib")
      set(PostgreSQL_LIBRARIES "${_flowforge_pq_libdir}/libpq.lib")
    endif()
    unset(_flowforge_pq_libdir)
    unset(_flowforge_pq_libext)
  endif()
endif()

if(FLOWFORGE_WITH_POSTGRES)
  set(SKIP_BUILD_TEST ON CACHE INTERNAL "")
  set(PQXX_LIBRARIES "" CACHE INTERNAL "")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    libpqxx
    GIT_REPOSITORY https://github.com/jtv/libpqxx.git
    GIT_TAG 7.9.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(libpqxx)
endif()

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

  # Gives test binaries (engine/tests, apps/server/tests) an absolute path
  # to engine/tests/fixtures/ regardless of ctest's working directory --
  # see cmake/fixtures_path.hpp.in. Defined once at this level since both
  # test binaries need it (Phase 3D-1: the real-Tesseract image extraction
  # integration tests).
  set(FLOWFORGE_TEST_FIXTURES_DIR "${CMAKE_SOURCE_DIR}/engine/tests/fixtures")
  set(FLOWFORGE_GENERATED_TEST_SUPPORT_DIR "${CMAKE_BINARY_DIR}/generated")
  configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/fixtures_path.hpp.in
    ${FLOWFORGE_GENERATED_TEST_SUPPORT_DIR}/flowforge/test_support/fixtures_path.hpp
    @ONLY)
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
message(STATUS "  PostgreSQL support:  ${FLOWFORGE_WITH_POSTGRES}")
