# flowforge_engine: the core C++ engine library. No HTTP/transport
# dependencies -- this must remain testable and usable independently of
# apps/server (see docs/architecture/overview.md, "Dependency direction").

add_library(flowforge_engine STATIC
  src/infra/ids.cpp
  src/infra/logger.cpp
  src/infra/config.cpp
  src/infra/metrics.cpp
  src/infra/csv.cpp
  src/infra/subprocess.cpp
  src/infra/image_format.cpp
  src/infra/json_lite.cpp
  src/domain/job.cpp
  src/domain/workflow.cpp
  src/domain/worker.cpp
  src/domain/execution.cpp
  src/domain/execution_result.cpp
  src/domain/workload.cpp
  src/domain/user_record.cpp
  src/domain/product_record.cpp
  src/domain/category_record.cpp
  src/domain/input_source.cpp
  src/domain/processing_target.cpp
  src/engine/handler_registry.cpp
  src/engine/priority_scheduler.cpp
  src/engine/job_executor.cpp
  src/engine/local_worker_pool.cpp
  src/engine/retry_dispatcher.cpp
  src/handlers/echo_handler.cpp
  src/handlers/delay_handler.cpp
  src/handlers/transform_handler.cpp
  src/handlers/user_process_handler.cpp
  src/handlers/product_process_handler.cpp
  src/handlers/category_process_handler.cpp
  src/handlers/builtin_handlers.cpp
  src/extractors/csv_extractor.cpp
  src/extractors/image_extractor.cpp
  src/providers/tesseract_ocr_provider.cpp
  src/persistence/in_memory_repositories.cpp
  src/persistence/repository_factory.cpp
  src/services/job_service.cpp
  src/services/workload_service.cpp
  src/services/user_import_parser.cpp
  src/services/user_import.cpp
  src/services/user_mapping.cpp
  src/services/product_mapping.cpp
  src/services/category_mapping.cpp
  src/services/input_processing_service.cpp
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

# --- PostgreSQL persistence (Phase 2A) --------------------------------------
# See docs/architecture/overview.md, "PostgreSQL persistence architecture",
# for the connection pool / transaction / error-mapping design. Kept as a
# separate, conditionally-compiled block: FLOWFORGE_WITH_POSTGRES is decided
# once, at the top-level CMakeLists.txt, based on whether libpq was found.
if(FLOWFORGE_WITH_POSTGRES)
  target_sources(flowforge_engine PRIVATE
    src/persistence/postgres/connection_pool.cpp
    src/persistence/postgres/error_mapping.cpp
    src/persistence/postgres/postgres_job_repository.cpp
    src/persistence/postgres/postgres_workflow_repository.cpp
    src/persistence/postgres/postgres_worker_repository.cpp
    src/persistence/postgres/postgres_execution_repository.cpp
    src/persistence/postgres/postgres_workload_repository.cpp
    src/persistence/postgres/postgres_product_repository.cpp
    src/persistence/postgres/postgres_category_repository.cpp
  )
  target_link_libraries(flowforge_engine PRIVATE pqxx)
  # PUBLIC: repository_factory.hpp's #ifdef FLOWFORGE_WITH_POSTGRES branch
  # is compiled into flowforge_engine, but any translation unit including
  # that header (e.g. in apps/server) needs the same macro defined so its
  # own view of the header (there is none conditional today, but keeps
  # future call sites honest about which build they're in) stays consistent.
  target_compile_definitions(flowforge_engine PUBLIC FLOWFORGE_WITH_POSTGRES=1)
endif()

flowforge_set_project_warnings(flowforge_engine)
flowforge_enable_sanitizers(flowforge_engine)

if(FLOWFORGE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
