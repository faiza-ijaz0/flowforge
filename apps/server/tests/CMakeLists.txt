add_executable(flowforge_server_tests
  http_server_test.cpp
  health_routes_test.cpp
  error_response_test.cpp
  workload_routes_test.cpp
  user_import_routes_test.cpp
  process_routes_test.cpp
)

target_link_libraries(flowforge_server_tests
  PRIVATE
    flowforge_server_lib
    GTest::gtest_main
)

# See CMakeLists.txt (top-level), "FLOWFORGE_GENERATED_TEST_SUPPORT_DIR" --
# gives this test binary the same absolute fixtures-directory header
# engine/tests uses (Phase 3D-1's real-Tesseract preview test needs the
# committed fixture image regardless of ctest's working directory).
target_include_directories(flowforge_server_tests PRIVATE ${FLOWFORGE_GENERATED_TEST_SUPPORT_DIR})

flowforge_set_project_warnings(flowforge_server_tests)
flowforge_enable_sanitizers(flowforge_server_tests)

include(GoogleTest)
gtest_discover_tests(flowforge_server_tests)
