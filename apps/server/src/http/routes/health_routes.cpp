#include "http/routes/health_routes.hpp"

#include <nlohmann/json.hpp>

namespace flowforge::server {

void register_health_routes(httplib::Server& server, std::shared_ptr<infra::MetricsRegistry> metrics,
                            const infra::AppConfig& config,
                            std::chrono::steady_clock::time_point process_start_time,
                            ReadinessChecks readiness) {
  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    const nlohmann::json body{{"status", "ok"}};
    res.set_content(body.dump(), "application/json");
  });

  server.Get("/ready", [process_start_time, environment = std::string(infra::to_string(config.environment)),
                        readiness = std::move(readiness)](const httplib::Request&, httplib::Response& res) {
    const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - process_start_time)
                                    .count();

    // Every check here is a cheap in-memory/non-blocking read (see
    // ReadinessChecks's class comment) -- deliberately never a live
    // database query, so polling this endpoint frequently (as a real
    // load balancer would) never costs a connection-pool checkout or a
    // round trip to PostgreSQL.
    const bool database_ok = !readiness.database_healthy || readiness.database_healthy();
    const bool scheduler_ok = !readiness.scheduler_running || readiness.scheduler_running();
    const bool worker_pool_ok = !readiness.worker_pool_running || readiness.worker_pool_running();
    const bool retry_dispatcher_ok =
        !readiness.retry_dispatcher_running || readiness.retry_dispatcher_running();
    const bool all_ok = database_ok && scheduler_ok && worker_pool_ok && retry_dispatcher_ok;

    const nlohmann::json body{
        {"status", all_ok ? "ok" : "unavailable"},
        {"environment", environment},
        {"uptime_seconds", uptime_seconds},
        {"checks",
         {{"database", database_ok ? "ok" : "unavailable"},
          {"scheduler", scheduler_ok ? "ok" : "unavailable"},
          {"worker_pool", worker_pool_ok ? "ok" : "unavailable"},
          {"retry_dispatcher", retry_dispatcher_ok ? "ok" : "unavailable"}}},
    };
    // 503, not 200-with-a-lying-body: a caller that only checks the HTTP
    // status code (the common case for load balancers/orchestrators) must
    // never be told "ok" while the process genuinely cannot do work.
    res.status = all_ok ? 200 : 503;
    res.set_content(body.dump(), "application/json");
  });

  server.Get("/metrics", [metrics = std::move(metrics)](const httplib::Request&, httplib::Response& res) {
    res.set_content(infra::render_metrics_text(metrics->snapshot()), "text/plain; version=0.0.4");
  });
}

}  // namespace flowforge::server
