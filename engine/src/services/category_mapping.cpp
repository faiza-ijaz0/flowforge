#include "flowforge/services/category_mapping.hpp"

#include <string>
#include <unordered_set>

namespace flowforge::services {

namespace {

/// Bounded the same way `domain::ExtractionResult::rejected_records` is --
/// see its class comment.
constexpr std::size_t kMaxReportedRejectedRecords = 200;

}  // namespace

MappedCategoryRecords map_structured_records_to_categories(
    const std::vector<domain::StructuredRecord>& records) {
  MappedCategoryRecords result;
  result.total_records = records.size();
  std::unordered_set<std::string> seen_slugs;

  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    const auto name = record.field_by_aliases({"name", "category name", "category_name", "title"});
    const auto slug = record.field_by_aliases({"slug", "category slug", "category_slug", "code"});
    const auto description = record.field_by_aliases({"description", "desc", "details"});
    const auto parent_slug = record.field_by_aliases(
        {"parent_slug", "parent slug", "parent", "parent category", "parent_category"});

    if (!name) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back(
            {.index = i + 1, .reason = "no 'name' column could be found for this record"});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    auto normalized = domain::validate_and_normalize_category_record(*name, slug, description, parent_slug);
    if (!normalized) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back({.index = i + 1, .reason = normalized.error().message()});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    // Same rule as the Users CSV import wizard (user-import.md, "Duplicate
    // rows"): within one submission the first occurrence of a natural key
    // wins; later ones are rejected rather than silently collapsing onto the
    // same row via upsert. Re-importing across submissions still upserts.
    if (!seen_slugs.insert(normalized->slug).second) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back(
            {.index = i + 1,
             .reason = "duplicate slug '" + normalized->slug +
                       "' in this submission -- only the first occurrence is kept"});
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
