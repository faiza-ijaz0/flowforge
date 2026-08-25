#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace flowforge::infra {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;
[[nodiscard]] LogLevel log_level_from_string(std::string_view level,
                                             LogLevel fallback = LogLevel::Info) noexcept;

/// A single structured key/value attached to a log line, e.g. {"job_id",
/// "1234"}. Kept as plain strings rather than a variant/json value: the
/// logging facade's job is to get the field to the sink, not to interpret
/// it, and every value FlowForge logs today is trivially stringifiable.
struct Field {
  std::string key;
  std::string value;
};

/// Logging facade decoupled from the concrete logging library (spdlog).
/// Application code depends on this interface, not on spdlog directly, so
/// the backend can be swapped (or a test double substituted to assert on
/// emitted log lines) without touching call sites. Every log call takes an
/// explicit `component` so logs from the engine, scheduler, API layer,
/// etc. remain attributable once multiple subsystems are running
/// concurrently.
class Logger {
 public:
  Logger() = default;
  virtual ~Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;

  virtual void log(LogLevel level, std::string_view component, std::string_view message,
                   const std::vector<Field>& fields = {}) const = 0;

  void trace(std::string_view component, std::string_view message,
             const std::vector<Field>& fields = {}) const {
    log(LogLevel::Trace, component, message, fields);
  }
  void debug(std::string_view component, std::string_view message,
             const std::vector<Field>& fields = {}) const {
    log(LogLevel::Debug, component, message, fields);
  }
  void info(std::string_view component, std::string_view message,
            const std::vector<Field>& fields = {}) const {
    log(LogLevel::Info, component, message, fields);
  }
  void warn(std::string_view component, std::string_view message,
            const std::vector<Field>& fields = {}) const {
    log(LogLevel::Warn, component, message, fields);
  }
  void error(std::string_view component, std::string_view message,
             const std::vector<Field>& fields = {}) const {
    log(LogLevel::Error, component, message, fields);
  }
  void critical(std::string_view component, std::string_view message,
                const std::vector<Field>& fields = {}) const {
    log(LogLevel::Critical, component, message, fields);
  }
};

/// Creates the production logger. `structured=true` emits one JSON object
/// per line (production-friendly, machine-parseable); `structured=false`
/// emits a human-friendly colorized line (development-friendly).
[[nodiscard]] std::shared_ptr<Logger> make_logger(LogLevel min_level, bool structured);

}  // namespace flowforge::infra
