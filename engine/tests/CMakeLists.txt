include(GoogleTest)

add_executable(flowforge_engine_tests
  infra/config_test.cpp
  infra/clock_test.cpp
  infra/metrics_test.cpp
  infra/ids_test.cpp
  domain/retry_policy_test.cpp
  domain/job_test.cpp
  domain/execution_result_test.cpp
  domain/execution_test.cpp
  domain/workload_test.cpp
  domain/user_record_test.cpp
  domain/product_record_test.cpp
  domain/input_source_test.cpp
  domain/processing_target_test.cpp
  domain/structured_record_test.cpp
  infra/image_format_test.cpp
  engine/blocking_queue_test.cpp
  engine/thread_pool_test.cpp
  engine/handler_registry_test.cpp
  engine/execution_model_acceptance_test.cpp
  engine/priority_blocking_queue_test.cpp
  engine/priority_scheduler_test.cpp
  engine/job_executor_test.cpp
  engine/local_worker_pool_test.cpp
  engine/retry_dispatcher_test.cpp
  engine/retry_engine_acceptance_test.cpp
  handlers/echo_handler_test.cpp
  handlers/transform_handler_test.cpp
  handlers/delay_handler_test.cpp
  handlers/user_process_handler_test.cpp
  handlers/product_process_handler_test.cpp
  extractors/csv_extractor_test.cpp
  extractors/image_extractor_test.cpp
  providers/tesseract_ocr_provider_test.cpp
  persistence/in_memory_job_repository_test.cpp
  persistence/in_memory_execution_repository_test.cpp
  persistence/in_memory_workload_repository_test.cpp
  persistence/in_memory_product_repository_test.cpp
  services/job_service_test.cpp
  services/workload_service_test.cpp
  services/user_import_parser_test.cpp
  services/user_import_test.cpp
  services/user_mapping_test.cpp
  services/product_mapping_test.cpp
  services/input_processing_service_test.cpp
)

target_link_libraries(flowforge_engine_tests
  PRIVATE
    flowforge::engine
    GTest::gtest_main
)

# See CMakeLists.txt (top-level), "FLOWFORGE_GENERATED_TEST_SUPPORT_DIR" --
# absolute path to engine/tests/fixtures/ for the real-Tesseract OCR
# integration tests (providers/tesseract_ocr_provider_test.cpp).
target_include_directories(flowforge_engine_tests PRIVATE ${FLOWFORGE_GENERATED_TEST_SUPPORT_DIR})

# PostgreSQL integration tests: compiled only when this build has
# PostgreSQL support (FLOWFORGE_WITH_POSTGRES, decided in the top-level
# CMakeLists.txt), and skipped at runtime (GTEST_SKIP, not silently
# reported as passing) unless FLOWFORGE_TEST_DATABASE_URL/
# FLOWFORGE_DATABASE_URL points at a real, reachable PostgreSQL instance --
# see engine/tests/persistence/postgres/postgres_test_support.hpp and
# docs/development/getting-started.md.
if(FLOWFORGE_WITH_POSTGRES)
  target_sources(flowforge_engine_tests PRIVATE
    persistence/postgres/connection_pool_test.cpp
    persistence/postgres/postgres_job_repository_test.cpp
    persistence/postgres/postgres_workflow_repository_test.cpp
    persistence/postgres/postgres_worker_repository_test.cpp
    persistence/postgres/postgres_execution_repository_test.cpp
    persistence/postgres/postgres_retry_engine_acceptance_test.cpp
    persistence/postgres/postgres_workload_repository_test.cpp
    persistence/postgres/postgres_product_repository_test.cpp
  )
  target_include_directories(flowforge_engine_tests PRIVATE persistence/postgres)
  target_link_libraries(flowforge_engine_tests PRIVATE pqxx)
endif()

flowforge_set_project_warnings(flowforge_engine_tests)
flowforge_enable_sanitizers(flowforge_engine_tests)

gtest_discover_tests(flowforge_engine_tests)
