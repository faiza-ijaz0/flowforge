# flowforge_engine: the core C++ engine library. No HTTP/transport
# dependencies -- this must remain testable and usable independently of
# apps/server (see docs/architecture/overview.md, "Dependency direction").

add_library(flowforge_engine STATIC
  src/infra/ids.cpp
  src/infra/logger.cpp
  src/infra/config.cpp
  src/infra/metrics.cpp
  src/domain/job.cpp
  src/domain/workflow.cpp
  src/domain/worker.cpp
  src/domain/execution.cpp
  src/persistence/in_memory_repositories.cpp
  src/services/job_service.cpp
)

add_library(flowforge::engine ALIAS flowforge_engine)

target_include_directories(flowforge_engine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(flowforge_engine
  PUBLIC
    spdlog::spdlog
  PRIVATE
)

target_compile_features(flowforge_engine PUBLIC cxx_std_23)

flowforge_set_project_warnings(flowforge_engine)
flowforge_enable_sanitizers(flowforge_engine)

if(FLOWFORGE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
