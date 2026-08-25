#include "flowforge/infra/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace flowforge::infra {

std::string_view to_string(Environment env) noexcept {
  switch (env) {
    case Environment::Development:
      return "development";
    case Environment::Test:
      return "test";
    case Environment::Staging:
      return "staging";
    case Environment::Production:
      return "production";
  }
  return "development";
}

namespace {

std::string lower(std::string_view s) {
  std::string out{s};
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

Result<Environment> parse_environment(const std::string& raw) {
  const std::string value = lower(raw);
  if (value == "development" || value == "dev")
    return Environment::Development;
  if (value == "test")
    return Environment::Test;
  if (value == "staging")
    return Environment::Staging;
  if (value == "production" || value == "prod")
    return Environment::Production;
  return std::unexpected(
      make_error(ErrorCode::Configuration,
                 "FLOWFORGE_ENV must be one of: development, test, staging, production (got '" + raw + "')"));
}

template <typename Int>
Result<Int> parse_positive_int(const std::string& raw, std::string_view var_name) {
  Int value{};
  const auto* begin = raw.data();
  const auto* end = raw.data() + raw.size();
  auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end || value <= 0) {
    return std::unexpected(make_error(
        ErrorCode::Configuration, std::string(var_name) + " must be a positive integer (got '" + raw + "')"));
  }
  return value;
}

Result<bool> parse_bool(const std::string& raw, std::string_view var_name) {
  const std::string value = lower(raw);
  if (value == "true" || value == "1" || value == "yes")
    return true;
  if (value == "false" || value == "0" || value == "no")
    return false;
  return std::unexpected(
      make_error(ErrorCode::Configuration, std::string(var_name) + " must be a boolean (got '" + raw + "')"));
}

}  // namespace

Result<AppConfig> AppConfig::load(const GetenvFn& getenv_fn) {
  AppConfig config{};

  if (auto env_raw = getenv_fn("FLOWFORGE_ENV")) {
    auto parsed = parse_environment(*env_raw);
    if (!parsed)
      return std::unexpected(parsed.error());
    config.environment = *parsed;
  }

  if (auto host = getenv_fn("FLOWFORGE_SERVER_HOST")) {
    if (host->empty()) {
      return std::unexpected(make_error(ErrorCode::Configuration, "FLOWFORGE_SERVER_HOST must not be empty"));
    }
    config.server_host = *host;
  }

  if (auto port_raw = getenv_fn("FLOWFORGE_SERVER_PORT")) {
    auto parsed = parse_positive_int<int>(*port_raw, "FLOWFORGE_SERVER_PORT");
    if (!parsed)
      return std::unexpected(parsed.error());
    if (*parsed > 65535) {
      return std::unexpected(make_error(ErrorCode::Configuration, "FLOWFORGE_SERVER_PORT must be <= 65535"));
    }
    config.server_port = static_cast<std::uint16_t>(*parsed);
  }

  if (auto worker_count_raw = getenv_fn("FLOWFORGE_WORKER_COUNT")) {
    auto parsed = parse_positive_int<long>(*worker_count_raw, "FLOWFORGE_WORKER_COUNT");
    if (!parsed)
      return std::unexpected(parsed.error());
    config.worker_count = static_cast<std::size_t>(*parsed);
  }

  if (auto queue_capacity_raw = getenv_fn("FLOWFORGE_QUEUE_CAPACITY")) {
    auto parsed = parse_positive_int<long>(*queue_capacity_raw, "FLOWFORGE_QUEUE_CAPACITY");
    if (!parsed)
      return std::unexpected(parsed.error());
    config.queue_default_capacity = static_cast<std::size_t>(*parsed);
  }

  if (auto log_level_raw = getenv_fn("FLOWFORGE_LOG_LEVEL")) {
    config.log_level = log_level_from_string(*log_level_raw, LogLevel::Info);
  }

  // Structured logging defaults to on outside development, off inside it,
  // unless explicitly overridden.
  config.structured_logging = config.environment != Environment::Development;
  if (auto structured_raw = getenv_fn("FLOWFORGE_STRUCTURED_LOGGING")) {
    auto parsed = parse_bool(*structured_raw, "FLOWFORGE_STRUCTURED_LOGGING");
    if (!parsed)
      return std::unexpected(parsed.error());
    config.structured_logging = *parsed;
  }

  if (auto db_url = getenv_fn("FLOWFORGE_DATABASE_URL")) {
    config.database_url = *db_url;
  }
  if (config.database_url.empty() && config.environment != Environment::Development &&
      config.environment != Environment::Test) {
    return std::unexpected(
        make_error(ErrorCode::Configuration,
                   "FLOWFORGE_DATABASE_URL is required when FLOWFORGE_ENV is 'staging' or 'production'"));
  }

  return config;
}

Result<AppConfig> AppConfig::load_from_environment() {
  return load([](std::string_view name) -> std::optional<std::string> {
    std::string name_str{name};
    const char* value = std::getenv(name_str.c_str());
    if (value == nullptr)
      return std::nullopt;
    return std::string(value);
  });
}

}  // namespace flowforge::infra
