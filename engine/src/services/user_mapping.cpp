#include "flowforge/services/user_mapping.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace flowforge::services {

namespace {

/// Bounded the same way `domain::ExtractionResult::rejected_records` is --
/// see its class comment.
constexpr std::size_t kMaxReportedRejectedRecords = 200;

std::string to_lower_trimmed(std::string_view value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  std::string result(value.substr(begin, end - begin));
  std::ranges::transform(result, result.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

/// Finds the first field in `record` whose (case-insensitive, trimmed)
/// column name matches one of `aliases` -- see .hpp's class comment for
/// why this is alias-/case-insensitive rather than the exact-match
/// `parse_user_import_csv` uses for typed CSV headers.
std::optional<std::string_view> find_by_aliases(const domain::StructuredRecord& record,
                                                std::initializer_list<std::string_view> aliases) {
  for (const auto& [key, value] : record.fields) {
    const std::string normalized_key = to_lower_trimmed(key);
    for (const auto& alias : aliases) {
      if (normalized_key == alias) {
        return std::string_view(value);
      }
    }
  }
  return std::nullopt;
}

}  // namespace

MappedUserRecords map_structured_records_to_users(const std::vector<domain::StructuredRecord>& records) {
  MappedUserRecords result;
  result.total_records = records.size();

  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    const auto name = find_by_aliases(record, {"name", "full name", "fullname", "full_name"});
    const auto email = find_by_aliases(record, {"email", "email address", "e-mail", "email_address"});
    const auto phone =
        find_by_aliases(record, {"phone", "phone number", "mobile", "phone_number", "mobile number"});

    if (!name || !email) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back(
            {.index = i + 1,
             .reason = !name ? "no 'name' column could be found for this record"
                             : "no 'email' column could be found for this record"});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    auto normalized = domain::validate_and_normalize_user_record(*name, *email, phone);
    if (!normalized) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back({.index = i + 1, .reason = normalized.error().message()});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    result.valid_records.push_back(std::move(*normalized));
  }

  return result;
}

}  // namespace flowforge::services
