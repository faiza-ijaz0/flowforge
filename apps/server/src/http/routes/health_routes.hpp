#pragma once

#include <httplib.h>

#include <chrono>
#include <functional>
#include <memory>

#include "flowforge/infra/config.hpp"
#include "flowforge/infra/metrics.hpp"

namespace flowforge::server {

/// The critical dependencies `GET /ready` reports on (Phase 2B-5). Each
/// check is a cheap, non-blocking `std::function<bool()>` -- never a live
/// database query or anything else that could turn a readiness probe into
/// a slow or hanging request (see docs/architecture/execution-model.md,
/// "Health vs readiness"). Supplied by the composition root (`App::
/// create()`), which is the only place that actually holds references to
/// the scheduler/worker pool/retry dispatcher/connection pool.
struct ReadinessChecks {
  /// True for in-memory persistence (nothing to check); for PostgreSQL,
  /// `postgres::PgConnectionPool::is_available()`.
  std::function<bool()> database_healthy;
  std::function<bool()> scheduler_running;
  std::function<bool()> worker_pool_running;
  std::function<bool()> retry_dispatcher_running;
};

/// Registers GET /health, GET /ready, and GET /metrics.
///
/// - /health: liveness -- the process is up and the HTTP server is
///   functioning. Deliberately never consults `readiness` or any external
///   dependency: an unhealthy PostgreSQL must not take the load balancer's
///   liveness check down with it and trigger a pointless process restart
///   that wouldn't fix the actual (external) problem.
/// - /ready: readiness -- the process is actually capable of accepting
///   and processing work. Returns 503 (not 200) the moment any check in
///   `readiness` reports false; the response body says exactly which
///   check(s) failed. See docs/architecture/execution-model.md, "Health
///   vs readiness" for the full contract.
/// - /metrics: renders the current MetricsRegistry snapshot as plain
///   text (see infra/metrics.hpp for why this isn't Prometheus format yet).
void register_health_routes(httplib::Server& server, std::shared_ptr<infra::MetricsRegistry> metrics,
                            const infra::AppConfig& config,
                            std::chrono::steady_clock::time_point process_start_time,
                            ReadinessChecks readiness);

}  // namespace flowforge::server
