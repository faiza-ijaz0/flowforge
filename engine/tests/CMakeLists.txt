include(GoogleTest)

add_executable(flowforge_engine_tests
  infra/config_test.cpp
  infra/clock_test.cpp
  infra/metrics_test.cpp
  infra/ids_test.cpp
  domain/retry_policy_test.cpp
  domain/job_test.cpp
  engine/blocking_queue_test.cpp
  engine/thread_pool_test.cpp
  persistence/in_memory_job_repository_test.cpp
  services/job_service_test.cpp
)

target_link_libraries(flowforge_engine_tests
  PRIVATE
    flowforge::engine
    GTest::gtest_main
)

flowforge_set_project_warnings(flowforge_engine_tests)
flowforge_enable_sanitizers(flowforge_engine_tests)

gtest_discover_tests(flowforge_engine_tests)
