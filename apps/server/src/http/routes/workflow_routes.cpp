#include "http/routes/workflow_routes.hpp"

#include <nlohmann/json.hpp>

#include "http/error_response.hpp"
#include "time_format.hpp"

namespace flowforge::server {

namespace {

nlohmann::json to_json(const domain::Workflow& workflow) {
  nlohmann::json steps = nlohmann::json::array();
  for (const auto& step : workflow.steps()) {
    nlohmann::json depends_on = nlohmann::json::array();
    for (const auto& dep : step.depends_on) {
      depends_on.push_back(dep.value());
    }
    steps.push_back({{"id", step.id.value()},
                     {"name", step.name},
                     {"job_id", step.job_id.value()},
                     {"depends_on", depends_on}});
  }
  return nlohmann::json{
      {"id", workflow.id().value()},
      {"name", workflow.name()},
      {"status", std::string(domain::to_string(workflow.status()))},
      {"steps", steps},
      {"created_at", to_iso8601(workflow.created_at())},
      {"updated_at", to_iso8601(workflow.updated_at())},
  };
}

}  // namespace

void register_workflow_routes(httplib::Server& server,
                              const std::shared_ptr<persistence::IWorkflowRepository>& repository) {
  server.Get("/api/v1/workflows", [repository](const httplib::Request&, httplib::Response& res) {
    auto workflows = repository->list(500, 0);
    if (!workflows) {
      res.status = http_status_for(workflows.error().code());
      res.set_content(to_error_body(workflows.error()).dump(), "application/json");
      return;
    }
    nlohmann::json items = nlohmann::json::array();
    for (const auto& workflow : *workflows) {
      items.push_back(to_json(workflow));
    }
    res.set_content(nlohmann::json{{"workflows", items}}.dump(), "application/json");
  });
}

}  // namespace flowforge::server
