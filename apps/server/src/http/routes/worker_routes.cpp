#include "http/routes/worker_routes.hpp"

#include <nlohmann/json.hpp>

#include "http/error_response.hpp"
#include "time_format.hpp"

namespace flowforge::server {

namespace {

nlohmann::json to_json(const domain::Worker& worker) {
  return nlohmann::json{
      {"id", worker.id().value()},
      {"hostname", worker.hostname()},
      {"status", std::string(domain::to_string(worker.status()))},
      {"registered_at", to_iso8601(worker.registered_at())},
      {"last_heartbeat", to_iso8601(worker.last_heartbeat())},
  };
}

}  // namespace

void register_worker_routes(httplib::Server& server,
                            const std::shared_ptr<persistence::IWorkerRepository>& repository) {
  server.Get("/api/v1/workers", [repository](const httplib::Request&, httplib::Response& res) {
    auto workers = repository->list();
    if (!workers) {
      res.status = http_status_for(workers.error().code());
      res.set_content(to_error_body(workers.error()).dump(), "application/json");
      return;
    }
    nlohmann::json items = nlohmann::json::array();
    for (const auto& worker : *workers) {
      items.push_back(to_json(worker));
    }
    res.set_content(nlohmann::json{{"workers", items}}.dump(), "application/json");
  });
}

}  // namespace flowforge::server
