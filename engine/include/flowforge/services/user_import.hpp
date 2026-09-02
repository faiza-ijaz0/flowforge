#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/user_import_parser.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::services {

/// Result of a CSV user-import (Phase 3B -- see
/// docs/architecture/user-import.md). Wraps a regular
/// `CreateWorkloadResult` (one item per *valid* CSV row) with the CSV
/// parse's own summary, so a caller can tell "how many rows were even
/// attempted" from "how many became jobs" -- see
/// docs/architecture/user-import.md, "Bulk submission semantics": the
/// response must distinguish total rows / valid rows / invalid rows /
/// submitted jobs, never silently pretend every uploaded row became a
/// job.
struct UserImportResult {
  domain::Workload workload;
  std::vector<WorkloadItemDispatchOutcome> items;
  std::size_t total_rows = 0;
  std::size_t valid_rows = 0;
  std::size_t invalid_rows = 0;
  std::vector<RejectedImportRow> rejected_rows;
  bool rejected_rows_truncated = false;
};

/// Parses `csv_content` as a user-import CSV (`parse_user_import_csv`) and
/// creates a workload from the valid rows via
/// `workload_service.create_workload()` -- the exact same generic,
/// create-then-schedule path any other workload type uses. A
/// structurally-invalid CSV (bad header, malformed, oversized, not UTF-8,
/// too many rows) is rejected wholesale: no workload is created at all. A
/// CSV with some invalid *rows* still creates a workload for the valid
/// ones -- see docs/architecture/user-import.md, "Bulk submission
/// semantics".
///
/// Deliberately a free function, not a `WorkloadService` member: this is
/// the per-business-domain adapter that composes a domain-specific CSV
/// parser with the engine's generic workload-creation path, kept entirely
/// outside `WorkloadService` so that class never needs to know "user" or
/// "CSV" exist -- see docs/architecture/user-import.md, "Why this isn't a
/// WorkloadService method". A future `product.process`/`category.process`
/// CSV import follows the identical pattern: its own
/// `parse_product_import_csv`/`parse_category_import_csv` (reusing the
/// shared, domain-agnostic `infra::tokenize_csv`) and its own
/// `import_products_from_csv`/`import_categories_from_csv` function
/// alongside this one -- never a change to `WorkloadService` itself.
[[nodiscard]] Result<UserImportResult> import_users_from_csv(
    WorkloadService& workload_service, const std::string& csv_content,
    const std::shared_ptr<infra::Logger>& logger,
    const std::shared_ptr<infra::MetricsRegistry>& metrics = nullptr);

}  // namespace flowforge::services
