#include "flowforge/handlers/user_process_handler.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>

namespace flowforge::handlers {

namespace {

constexpr std::size_t kMaxPayloadBytes = std::size_t{16} * 1024;
constexpr std::size_t kMaxNameLength = 200;
constexpr std::size_t kMaxEmailLength = 320;  // RFC 5321's upper bound on a full email address.
constexpr std::size_t kMaxPhoneLength = 32;

/// Extracts the string value of `"key": "..."` from a flat JSON object
/// `payload`. Supports only `\"` and `\\` escapes (see the header's class
/// comment) -- returns std::nullopt if `key` is not present, is not
/// followed by a JSON string value, or the string is unterminated.
/// Deliberately not a general-purpose JSON parser -- see
/// UserProcessHandler's class comment for why the engine hand-rolls this
/// instead of linking a JSON library.
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

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string to_lower(std::string value) {
  std::ranges::transform(value, value.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

/// Structural email check: exactly one '@', a non-empty local part, and a
/// domain part containing an interior '.'. Not RFC 5322-complete --
/// deliberately bounded to what's needed to reject obviously-malformed
/// input (see docs/architecture/workload-model.md, "Known limitations").
bool looks_like_email(std::string_view email) {
  const auto at_pos = email.find('@');
  if (at_pos == std::string_view::npos || at_pos == 0 || at_pos == email.size() - 1) {
    return false;
  }
  if (email.find('@', at_pos + 1) != std::string_view::npos) {
    return false;
  }
  const auto domain = email.substr(at_pos + 1);
  const auto dot_pos = domain.find('.');
  return dot_pos != std::string_view::npos && dot_pos != 0 && dot_pos != domain.size() - 1;
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
  const std::string name = trim(*raw_name);
  if (name.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' must not be blank"));
  }
  if (name.size() > kMaxNameLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'name' must be <= " + std::to_string(kMaxNameLength) + " characters"));
  }

  auto raw_email = extract_json_string_field(payload, "email");
  if (!raw_email) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'email' is required and must be a JSON string"));
  }
  const std::string email = to_lower(trim(*raw_email));
  if (email.empty() || email.size() > kMaxEmailLength || !looks_like_email(email)) {
    return std::unexpected(make_error(ErrorCode::Validation, "'email' must be a valid email address"));
  }

  std::optional<std::string> phone;
  if (auto raw_phone = extract_json_string_field(payload, "phone")) {
    std::string trimmed_phone = trim(*raw_phone);
    if (trimmed_phone.size() > kMaxPhoneLength) {
      return std::unexpected(make_error(
          ErrorCode::Validation, "'phone' must be <= " + std::to_string(kMaxPhoneLength) + " characters"));
    }
    if (!trimmed_phone.empty()) {
      phone = std::move(trimmed_phone);
    }
  }

  context.logger().debug("user_process_handler", "normalized user record",
                         {{.key = "job_id", .value = context.job_id().value()}});

  std::string output = R"({"name":")" + json_escape(name) + R"(","email":")" + json_escape(email) + R"(")";
  if (phone) {
    output += R"(,"phone":")" + json_escape(*phone) + R"(")";
  }
  output += R"(,"valid":true})";

  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(std::move(output), duration, {{"operation", "user_process"}});
}

}  // namespace flowforge::handlers
