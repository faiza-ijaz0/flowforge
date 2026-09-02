add_executable(flowforge_server_tests
  http_server_test.cpp
  health_routes_test.cpp
  error_response_test.cpp
  workload_routes_test.cpp
)

target_link_libraries(flowforge_server_tests
  PRIVATE
    flowforge_server_lib
    GTest::gtest_main
)

flowforge_set_project_warnings(flowforge_server_tests)
flowforge_enable_sanitizers(flowforge_server_tests)

include(GoogleTest)
gtest_discover_tests(flowforge_server_tests)
