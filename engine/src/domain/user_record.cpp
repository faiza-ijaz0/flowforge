#include "flowforge/domain/user_record.hpp"

#include <algorithm>
#include <cctype>

#include "flowforge/infra/json_lite.hpp"

namespace flowforge::domain {

namespace {

constexpr std::size_t kMaxNameLength = 200;
constexpr std::size_t kMaxEmailLength = 320;  // RFC 5321's upper bound on a full email address.
constexpr std::size_t kMaxPhoneLength = 32;

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
/// input (see docs/architecture/user-import.md, "Known limitations").
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

}  // namespace

Result<NormalizedUserRecord> validate_and_normalize_user_record(std::string_view name, std::string_view email,
                                                                std::optional<std::string_view> phone) {
  const std::string trimmed_name = trim(name);
  if (trimmed_name.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' must not be blank"));
  }
  if (trimmed_name.size() > kMaxNameLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'name' must be <= " + std::to_string(kMaxNameLength) + " characters"));
  }

  const std::string normalized_email = to_lower(trim(email));
  if (normalized_email.empty() || normalized_email.size() > kMaxEmailLength ||
      !looks_like_email(normalized_email)) {
    return std::unexpected(make_error(ErrorCode::Validation, "'email' must be a valid email address"));
  }

  std::optional<std::string> normalized_phone;
  if (phone.has_value()) {
    std::string trimmed_phone = trim(*phone);
    if (trimmed_phone.size() > kMaxPhoneLength) {
      return std::unexpected(make_error(
          ErrorCode::Validation, "'phone' must be <= " + std::to_string(kMaxPhoneLength) + " characters"));
    }
    if (!trimmed_phone.empty()) {
      normalized_phone = std::move(trimmed_phone);
    }
  }

  return NormalizedUserRecord{
      .name = trimmed_name, .email = normalized_email, .phone = std::move(normalized_phone)};
}

std::string serialize_user_record_as_job_payload(const NormalizedUserRecord& record) {
  std::string payload = R"({"name":")" + infra::json_escape(record.name) + R"(","email":")" +
                        infra::json_escape(record.email) + R"(")";
  if (record.phone) {
    payload += R"(,"phone":")" + infra::json_escape(*record.phone) + R"(")";
  }
  payload += "}";
  return payload;
}

}  // namespace flowforge::domain
