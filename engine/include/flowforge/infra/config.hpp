#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "flowforge/infra/logger.hpp"
#include "flowforge/result.hpp"

namespace flowforge::infra {

enum class Environment : std::uint8_t { Development, Test, Staging, Production };

[[nodiscard]] std::string_view to_string(Environment env) noexcept;

/// Typed, validated application configuration loaded from environment
/// variables. There is exactly one way to obtain a valid `AppConfig`:
/// `AppConfig::load(...)`, which returns an `Error` describing precisely
/// what is missing/invalid rather than letting the process start in a
/// half-configured state. Defaults are only provided for values that are
/// safe to default in development (ports, worker counts); there is no
/// default for `database_url` in non-development environments and no
/// default credential of any kind is ever baked in.
struct AppConfig {
  Environment environment = Environment::Development;
  std::string server_host = "0.0.0.0";
  std::uint16_t server_port = 8080;
  std::string database_url;
  std::size_t database_pool_size = 8;
  std::size_t worker_count = 4;
  std::size_t queue_default_capacity = 1024;
  std::size_t scheduler_queue_capacity = 1024;
  std::size_t scheduler_dispatch_workers = 2;
  std::size_t worker_pool_size = 4;
  std::size_t worker_pool_queue_capacity = 1024;
  std::size_t execution_timeout_ms = 60'000;
  /// Browser origin allowed to make cross-origin requests to this API
  /// (see apps/server/src/http/cors.hpp). Compared for an exact match
  /// against the request's `Origin` header -- never a blanket wildcard.
  /// Defaults to the Next.js dashboard's local dev origin; override for
  /// other environments (e.g. a deployed dashboard's real origin) via
  /// the environment variable below. Empty disables CORS headers
  /// entirely (every browser cross-origin request will then be
  /// rejected by the browser itself, which is a safe default for an
  /// environment with no known browser client).
  std::string cors_allowed_origin = "http://localhost:3000";
  /// How often `engine::RetryDispatcher` re-scans for `Retrying` jobs
  /// whose backoff has elapsed (see docs/architecture/execution-model.md,
  /// "Retry engine"). Independent of any individual job's own backoff
  /// delay -- this is the poll granularity, not the delay itself.
  std::chrono::milliseconds retry_poll_interval_ms{500};
  /// Maximum `Retrying` jobs re-submitted per poll tick, bounding the cost
  /// of one `list_by_status()` query/scan.
  std::size_t retry_batch_size = 50;
  LogLevel log_level = LogLevel::Info;
  bool structured_logging = false;

  /// Reads configuration from environment variables:
  ///   FLOWFORGE_ENV                (development|test|staging|production, default: development)
  ///   FLOWFORGE_SERVER_HOST        (default: 0.0.0.0)
  ///   FLOWFORGE_SERVER_PORT        (default: 8080)
  ///   FLOWFORGE_DATABASE_URL       (required outside development)
  ///   FLOWFORGE_DB_POOL_SIZE       (default: 8; PostgreSQL connection pool size)
  ///   FLOWFORGE_WORKER_COUNT       (default: 4)
  ///   FLOWFORGE_QUEUE_CAPACITY     (default: 1024)
  ///   FLOWFORGE_SCHEDULER_QUEUE_CAPACITY   (default: 1024; bounded priority dispatch queue capacity)
  ///   FLOWFORGE_SCHEDULER_DISPATCH_WORKERS (default: 2; dispatch thread count)
  ///   FLOWFORGE_WORKER_POOL_SIZE           (default: 4; local execution worker thread count)
  ///   FLOWFORGE_WORKER_POOL_QUEUE_CAPACITY (default: 1024; bounded worker-pool queue capacity)
  ///   FLOWFORGE_EXECUTION_TIMEOUT_MS       (default: 60000; cooperative per-attempt execution timeout)
  ///   FLOWFORGE_CORS_ALLOWED_ORIGIN (default: http://localhost:3000; empty disables CORS headers)
  ///   FLOWFORGE_RETRY_POLL_INTERVAL_MS (default: 500; retry dispatcher re-scan interval, in ms)
  ///   FLOWFORGE_RETRY_BATCH_SIZE   (default: 50; max Retrying jobs re-submitted per poll tick)
  ///   FLOWFORGE_LOG_LEVEL          (trace|debug|info|warn|error|critical|off, default: info)
  ///   FLOWFORGE_STRUCTURED_LOGGING (true|false, default: false in development, true otherwise)
  using GetenvFn = std::function<std::optional<std::string>(std::string_view)>;
  [[nodiscard]] static Result<AppConfig> load(const GetenvFn& getenv_fn);

  /// Convenience overload that reads from the real process environment.
  [[nodiscard]] static Result<AppConfig> load_from_environment();
};

}  // namespace flowforge::infra
