#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "flowforge/domain/product_record.hpp"
#include "flowforge/domain/structured_record.hpp"

namespace flowforge::services {

/// One `domain::StructuredRecord` that could not become a
/// `domain::NormalizedProductRecord` -- mirrors
/// `services::UserMappingRejection` exactly (see its own class comment).
struct ProductMappingRejection {
  std::size_t index = 0;
  std::string reason;
};

/// Result of mapping generic `StructuredRecord`s onto the Products
/// target -- mirrors `services::MappedUserRecords`'s shape/conventions.
struct MappedProductRecords {
  std::size_t total_records = 0;
  std::vector<domain::NormalizedProductRecord> valid_records;
  std::vector<ProductMappingRejection> rejected_records;
  std::size_t rejected_record_count = 0;
  bool rejected_records_truncated = false;
};

/// The Products target-specific adapter step in
/// `Image|Csv -> Extract -> Map -> Preview -> Confirm -> Workload`
/// (Phase 3E -- see docs/architecture/product-processing.md, "Product
/// import adapter"): turns each source-agnostic `StructuredRecord`
/// (produced by `extractors::CsvExtractor` *or*
/// `extractors::ImageExtractor` -- this function does not know or care
/// which) into a `domain::NormalizedProductRecord` via the exact same
/// `domain::validate_and_normalize_product_record` that
/// `handlers::ProductProcessHandler` uses at execution time -- mirrors
/// `services::map_structured_records_to_users`'s role/rationale exactly,
/// including the case-insensitive, alias-tolerant column matching (a CSV
/// column typed as `sku` and an OCR'd header cell read as `"SKU"` or
/// `"Product Code"` both resolve to the same field).
///
/// Deliberately a free function outside `WorkloadService` and outside
/// both extractors, for the same reason `map_structured_records_to_users`
/// is: the generic extraction layer stays generic, and this Products-
/// specific mapping step can exist alongside a future
/// `map_structured_records_to_categories` without either touching the
/// other, `WorkloadService`, or `InputProcessingService`.
[[nodiscard]] MappedProductRecords map_structured_records_to_products(
    const std::vector<domain::StructuredRecord>& records);

}  // namespace flowforge::services
