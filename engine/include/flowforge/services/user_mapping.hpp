#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "flowforge/domain/structured_record.hpp"
#include "flowforge/domain/user_record.hpp"

namespace flowforge::services {

/// One `domain::StructuredRecord` that could not become a
/// `domain::NormalizedUserRecord` -- either it has no recognizable
/// name/email column, or `domain::validate_and_normalize_user_record`
/// itself rejected its values. `index` is 1-indexed over the input
/// records, mirroring `domain::RejectedRecord::index`.
struct UserMappingRejection {
  std::size_t index = 0;
  std::string reason;
};

/// Result of mapping generic `StructuredRecord`s onto the Users target.
/// `rejected_records` is bounded (see .cpp) even though `total_records`/
/// `rejected_record_count` remain exact -- same convention as
/// `domain::ExtractionResult`.
struct MappedUserRecords {
  std::size_t total_records = 0;
  std::vector<domain::NormalizedUserRecord> valid_records;
  std::vector<UserMappingRejection> rejected_records;
  std::size_t rejected_record_count = 0;
  bool rejected_records_truncated = false;
};

/// The target-specific adapter step in `Image -> Extract -> Map -> Preview
/// -> Confirm -> Workload` (Phase 3D-1 -- see docs/architecture/
/// input-processing.md, "User mapping"): turns each source-agnostic
/// `StructuredRecord` (produced by `extractors::ImageExtractor`, or in the
/// future `extractors::CsvExtractor`) into a `domain::NormalizedUserRecord`
/// via the exact same `domain::validate_and_normalize_user_record` that
/// `services::parse_user_import_csv` and `handlers::UserProcessHandler`
/// already use -- "what makes a valid user record" cannot drift between
/// this new path and the existing ones.
///
/// A record's `name`/`email`/`phone` columns are located by a
/// case-insensitive, trimmed match against a small alias set (e.g. a
/// header cell OCR'd as "Email Address" still maps to `email`) --
/// deliberately looser than `parse_user_import_csv`'s exact-lowercase CSV
/// header match, because an image's header text is read by OCR, not typed
/// as a machine-oriented column name (see docs/architecture/
/// input-processing.md, "Why image header matching is case-insensitive").
///
/// Deliberately a free function outside `WorkloadService` and outside
/// `extractors::ImageExtractor` itself, for the same reason
/// `import_users_from_csv` is a free function (see its own class
/// comment): the generic extraction layer stays generic, and this
/// Users-specific mapping step could be swapped for a
/// `map_structured_records_to_products` alongside it without either
/// touching the other.
[[nodiscard]] MappedUserRecords map_structured_records_to_users(
    const std::vector<domain::StructuredRecord>& records);

}  // namespace flowforge::services
