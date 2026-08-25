#include "http/routes/health_routes.hpp"

#include <nlohmann/json.hpp>

namespace flowforge::server {

void register_health_routes(httplib::Server& server, std::shared_ptr<infra::MetricsRegistry> metrics,
                            const infra::AppConfig& config,
                            std::chrono::steady_clock::time_point process_start_time) {
  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    const nlohmann::json body{{"status", "ok"}};
    res.set_content(body.dump(), "application/json");
  });

  server.Get("/ready", [process_start_time, environment = std::string(infra::to_string(config.environment))](
                           const httplib::Request&, httplib::Response& res) {
    const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - process_start_time)
                                    .count();
    const nlohmann::json body{
        {"status", "ok"}, {"environment", environment}, {"uptime_seconds", uptime_seconds}};
    res.set_content(body.dump(), "application/json");
  });

  server.Get("/metrics", [metrics = std::move(metrics)](const httplib::Request&, httplib::Response& res) {
    res.set_content(infra::render_metrics_text(metrics->snapshot()), "text/plain; version=0.0.4");
  });
}

}  // namespace flowforge::server
