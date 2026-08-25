#pragma once

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
  std::size_t worker_count = 4;
  std::size_t queue_default_capacity = 1024;
  LogLevel log_level = LogLevel::Info;
  bool structured_logging = false;

  /// Reads configuration from environment variables:
  ///   FLOWFORGE_ENV                (development|test|staging|production, default: development)
  ///   FLOWFORGE_SERVER_HOST        (default: 0.0.0.0)
  ///   FLOWFORGE_SERVER_PORT        (default: 8080)
  ///   FLOWFORGE_DATABASE_URL       (required outside development)
  ///   FLOWFORGE_WORKER_COUNT       (default: 4)
  ///   FLOWFORGE_QUEUE_CAPACITY     (default: 1024)
  ///   FLOWFORGE_LOG_LEVEL          (trace|debug|info|warn|error|critical|off, default: info)
  ///   FLOWFORGE_STRUCTURED_LOGGING (true|false, default: false in development, true otherwise)
  using GetenvFn = std::function<std::optional<std::string>(std::string_view)>;
  [[nodiscard]] static Result<AppConfig> load(const GetenvFn& getenv_fn);

  /// Convenience overload that reads from the real process environment.
  [[nodiscard]] static Result<AppConfig> load_from_environment();
};

}  // namespace flowforge::infra
