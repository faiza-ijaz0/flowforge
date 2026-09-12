#include "flowforge/services/user_mapping.hpp"

namespace flowforge::services {

namespace {

/// Bounded the same way `domain::ExtractionResult::rejected_records` is --
/// see its class comment.
constexpr std::size_t kMaxReportedRejectedRecords = 200;

}  // namespace

MappedUserRecords map_structured_records_to_users(const std::vector<domain::StructuredRecord>& records) {
  MappedUserRecords result;
  result.total_records = records.size();

  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    const auto name = record.field_by_aliases({"name", "full name", "fullname", "full_name"});
    const auto email = record.field_by_aliases({"email", "email address", "e-mail", "email_address"});
    const auto phone =
        record.field_by_aliases({"phone", "phone number", "mobile", "phone_number", "mobile number"});

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
