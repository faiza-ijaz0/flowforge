#include "flowforge/infra/logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>

namespace flowforge::infra {

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
    case LogLevel::Critical:
      return "critical";
    case LogLevel::Off:
      return "off";
  }
  return "info";
}

LogLevel log_level_from_string(std::string_view level, LogLevel fallback) noexcept {
  std::string lowered{level};
  std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) { return std::tolower(c); });

  if (lowered == "trace")
    return LogLevel::Trace;
  if (lowered == "debug")
    return LogLevel::Debug;
  if (lowered == "info")
    return LogLevel::Info;
  if (lowered == "warn" || lowered == "warning")
    return LogLevel::Warn;
  if (lowered == "error")
    return LogLevel::Error;
  if (lowered == "critical" || lowered == "fatal")
    return LogLevel::Critical;
  if (lowered == "off")
    return LogLevel::Off;
  return fallback;
}

namespace {

spdlog::level::level_enum to_spdlog_level(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
    case LogLevel::Critical:
      return spdlog::level::critical;
    case LogLevel::Off:
      return spdlog::level::off;
  }
  return spdlog::level::info;
}

/// Escapes a string for embedding inside a JSON string literal. Minimal on
/// purpose: log messages/field values are not expected to contain
/// arbitrary binary data, so this covers control characters and quotes
/// rather than pulling in a JSON library for logging alone.
std::string json_escape(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u";
          static constexpr std::string_view kHex = "0123456789abcdef";
          out += kHex[static_cast<std::size_t>((c >> 4) & 0xF)];
          out += kHex[static_cast<std::size_t>(c & 0xF)];
        } else {
          out += c;
        }
    }
  }
  return out;
}

class SpdlogLogger final : public Logger {
 public:
  SpdlogLogger(std::shared_ptr<spdlog::logger> backend, bool structured)
      : backend_(std::move(backend)), structured_(structured) {}

  void log(LogLevel level, std::string_view component, std::string_view message,
           const std::vector<Field>& fields) const override {
    if (structured_) {
      std::ostringstream oss;
      oss << R"({"component":")" << json_escape(component) << R"(","message":")" << json_escape(message)
          << '"';
      for (const auto& field : fields) {
        oss << ",\"" << json_escape(field.key) << "\":\"" << json_escape(field.value) << '"';
      }
      oss << '}';
      backend_->log(to_spdlog_level(level), "{}", oss.str());
    } else {
      std::ostringstream oss;
      oss << '[' << component << "] " << message;
      for (const auto& field : fields) {
        oss << ' ' << field.key << '=' << field.value;
      }
      backend_->log(to_spdlog_level(level), "{}", oss.str());
    }
  }

 private:
  std::shared_ptr<spdlog::logger> backend_;
  bool structured_;
};

}  // namespace

std::shared_ptr<Logger> make_logger(LogLevel min_level, bool structured) {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  if (structured) {
    sink->set_pattern("%v");
  } else {
    sink->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%^%l%$] %v");
  }
  auto backend = std::make_shared<spdlog::logger>("flowforge", sink);
  backend->set_level(to_spdlog_level(min_level));
  backend->flush_on(spdlog::level::warn);
  return std::make_shared<SpdlogLogger>(std::move(backend), structured);
}

}  // namespace flowforge::infra
