#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "flowforge/domain/user_record.hpp"
#include "flowforge/result.hpp"

namespace flowforge::services {

/// Maximum data rows a single user-import CSV (and therefore the workload
/// it creates) may contain -- identical to
/// `WorkloadService`'s own item-count bound (`kMaxWorkloadItems`), so a
/// CSV that fits this parser's limit can never be rejected again,
/// redundantly, by `WorkloadService::create_workload`. See
/// docs/architecture/user-import.md, "Limits".
inline constexpr std::size_t kMaxUserImportRows = 1000;

/// One CSV data row that failed validation. `row_number` is 1-indexed over
/// data rows only -- the header row is never counted and is never
/// `row_number 1`; the first row after the header is `row_number 1`.
struct RejectedImportRow {
  std::size_t row_number = 0;
  std::string reason;
};

/// Result of parsing and validating a user-import CSV file -- see
/// docs/architecture/user-import.md, "CSV contract", for the full,
/// authoritative rule set. `valid_rows` are ready to become one
/// `user.process` Job payload each (see
/// `domain::serialize_user_record_as_job_payload`).
///
/// `rejected_rows` is bounded (see .cpp) even though `total_rows` and
/// `rejected_row_count` are always exact -- a 1000-row CSV that is
/// entirely invalid must not force the caller to render 1000 error rows.
struct ParsedUserImport {
  std::size_t total_rows = 0;
  std::vector<domain::NormalizedUserRecord> valid_rows;
  std::vector<RejectedImportRow> rejected_rows;
  std::size_t rejected_row_count = 0;
  bool rejected_rows_truncated = false;
};

/// Parses and validates a user-import CSV. Returns a structural `Result`
/// error (never a per-row `RejectedImportRow`) only for a failure that
/// invalidates the *whole* file: empty/oversized input, invalid UTF-8, no
/// header row, a missing/unrecognized/duplicate header column, an
/// unterminated quoted field, or more data rows than `kMaxUserImportRows`
/// allows. Everything else -- a blank required field, a malformed email, a
/// field that's too long, a row with the wrong number of fields, a
/// duplicate email -- rejects only that one row (see `ParsedUserImport`)
/// and never aborts the parse. See docs/architecture/user-import.md,
/// "Bulk submission semantics" for the rationale behind this split.
[[nodiscard]] Result<ParsedUserImport> parse_user_import_csv(std::string_view csv_content);

}  // namespace flowforge::services
