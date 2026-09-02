#include "flowforge/handlers/user_process_handler.hpp"

#include <chrono>
#include <optional>

#include "flowforge/domain/user_record.hpp"

namespace flowforge::handlers {

namespace {

constexpr std::size_t kMaxPayloadBytes = std::size_t{16} * 1024;

/// Extracts the string value of `"key": "..."` from a flat JSON object
/// `payload`. Supports only `\"` and `\\` escapes (see the header's class
/// comment) -- returns std::nullopt if `key` is not present, is not
/// followed by a JSON string value, or the string is unterminated.
/// Deliberately not a general-purpose JSON parser -- see
/// UserProcessHandler's class comment for the rationale (no JSON library
/// in engine/).
std::optional<std::string> extract_json_string_field(std::string_view payload, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const auto key_pos = payload.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t pos = key_pos + needle.size();
  while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t')) {
    ++pos;
  }
  if (pos >= payload.size() || payload[pos] != ':') {
    return std::nullopt;
  }
  ++pos;
  while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t')) {
    ++pos;
  }
  if (pos >= payload.size() || payload[pos] != '"') {
    return std::nullopt;
  }
  ++pos;

  std::string value;
  while (pos < payload.size() && payload[pos] != '"') {
    if (payload[pos] == '\\') {
      if (pos + 1 >= payload.size()) {
        return std::nullopt;
      }
      const char escaped = payload[pos + 1];
      if (escaped != '"' && escaped != '\\') {
        return std::nullopt;  // Unsupported escape -- see class comment.
      }
      value.push_back(escaped);
      pos += 2;
      continue;
    }
    value.push_back(payload[pos]);
    ++pos;
  }
  if (pos >= payload.size()) {
    return std::nullopt;  // Unterminated string.
  }
  return value;
}

/// Escapes '"' and '\\' so a normalized field can be safely embedded back
/// into the small hand-built JSON output object below.
std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

Result<domain::ExecutionResult> UserProcessHandler::execute(const engine::ExecutionContext& context,
                                                            const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "user.process payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();

  if (context.is_cancelled()) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return domain::ExecutionResult::failure(ErrorCode::JobExecution,
                                            "user.process cancelled before execution",
                                            /*retryable=*/false, duration);
  }

  auto raw_name = extract_json_string_field(payload, "name");
  if (!raw_name) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' is required and must be a JSON string"));
  }
  auto raw_email = extract_json_string_field(payload, "email");
  if (!raw_email) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'email' is required and must be a JSON string"));
  }
  auto raw_phone = extract_json_string_field(payload, "phone");

  // Shared with services::WorkloadService's CSV import path (see
  // domain/user_record.hpp's class comment) -- "what makes a valid user
  // record" is defined exactly once.
  auto normalized = domain::validate_and_normalize_user_record(
      *raw_name, *raw_email, raw_phone ? std::optional<std::string_view>(*raw_phone) : std::nullopt);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  context.logger().debug("user_process_handler", "normalized user record",
                         {{.key = "job_id", .value = context.job_id().value()}});

  std::string output = R"({"name":")" + json_escape(normalized->name) + R"(","email":")" +
                       json_escape(normalized->email) + R"(")";
  if (normalized->phone) {
    output += R"(,"phone":")" + json_escape(*normalized->phone) + R"(")";
  }
  output += R"(,"valid":true})";

  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(std::move(output), duration, {{"operation", "user_process"}});
}

}  // namespace flowforge::handlers
