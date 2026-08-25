#pragma once

#include <httplib.h>

#include <chrono>
#include <memory>

#include "flowforge/infra/config.hpp"
#include "flowforge/infra/metrics.hpp"

namespace flowforge::server {

/// Registers GET /health, GET /ready, and GET /metrics.
///
/// - /health: liveness -- the process is up and able to respond. Never
///   depends on external systems, so an unhealthy dependency doesn't take
///   the load balancer's liveness check down with it.
/// - /ready: readiness -- the process is able to serve real traffic.
///   Currently identical to /health because there are no external
///   dependencies to check yet (no database connection is held open in
///   this phase); the handler is structured so a future DB ping is a
///   localized change.
/// - /metrics: renders the current MetricsRegistry snapshot as plain
///   text (see infra/metrics.hpp for why this isn't Prometheus format yet).
void register_health_routes(httplib::Server& server, std::shared_ptr<infra::MetricsRegistry> metrics,
                            const infra::AppConfig& config,
                            std::chrono::steady_clock::time_point process_start_time);

}  // namespace flowforge::server
