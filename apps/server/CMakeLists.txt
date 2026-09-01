# flowforge_server: the HTTP API process. Depends on flowforge_engine for
# domain/service logic; owns the JSON <-> domain translation and the
# httplib-based transport so those concerns never leak into the engine
# (see docs/architecture/overview.md, "Dependency direction").

add_library(flowforge_server_lib STATIC
  src/time_format.cpp
  src/json/job_json.cpp
  src/http/error_response.cpp
  src/http/cors.cpp
  src/http/app.cpp
  src/http/routes/health_routes.cpp
  src/http/routes/job_routes.cpp
  src/http/routes/workflow_routes.cpp
  src/http/routes/worker_routes.cpp
)

target_include_directories(flowforge_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(flowforge_server_lib
  PUBLIC
    flowforge::engine
    nlohmann_json::nlohmann_json
    httplib::httplib
)

target_compile_features(flowforge_server_lib PUBLIC cxx_std_23)
flowforge_set_project_warnings(flowforge_server_lib)
flowforge_enable_sanitizers(flowforge_server_lib)

add_executable(flowforge_server src/main.cpp)
target_link_libraries(flowforge_server PRIVATE flowforge_server_lib)
flowforge_set_project_warnings(flowforge_server)
flowforge_enable_sanitizers(flowforge_server)

if(FLOWFORGE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
