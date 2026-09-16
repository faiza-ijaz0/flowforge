#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "flowforge/domain/category_record.hpp"
#include "flowforge/domain/structured_record.hpp"

namespace flowforge::services {

/// One `domain::StructuredRecord` that could not become a
/// `domain::NormalizedCategoryRecord` -- mirrors
/// `services::ProductMappingRejection` exactly.
struct CategoryMappingRejection {
  std::size_t index = 0;
  std::string reason;
};

/// Result of mapping generic `StructuredRecord`s onto the Categories
/// target -- mirrors `services::MappedProductRecords`'s shape/conventions.
struct MappedCategoryRecords {
  std::size_t total_records = 0;
  std::vector<domain::NormalizedCategoryRecord> valid_records;
  std::vector<CategoryMappingRejection> rejected_records;
  std::size_t rejected_record_count = 0;
  bool rejected_records_truncated = false;
};

/// The Categories target-specific adapter step in
/// `Image|Csv -> Extract -> Map -> Preview -> Confirm -> Workload`
/// (Phase 3F -- see docs/architecture/category-processing.md): turns each
/// source-agnostic `StructuredRecord` into a `domain::
/// NormalizedCategoryRecord` via the exact same `domain::
/// validate_and_normalize_category_record` that
/// `handlers::CategoryProcessHandler` uses at execution time -- mirrors
/// `services::map_structured_records_to_products`'s role/rationale
/// exactly, including case-insensitive, alias-tolerant column matching.
///
/// Deliberately a free function outside `WorkloadService` and outside
/// both extractors, for the same reason the Users/Products mapping
/// functions are: the generic extraction layer stays generic, and this
/// Categories-specific mapping step exists alongside
/// `map_structured_records_to_products`/`_users` without any of the three
/// touching each other, `WorkloadService`, or `InputProcessingService`.
[[nodiscard]] MappedCategoryRecords map_structured_records_to_categories(
    const std::vector<domain::StructuredRecord>& records);

}  // namespace flowforge::services
