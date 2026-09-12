#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

/// The persisted representation of a product (database/migrations/
/// 0014_create_products.sql) -- the read-model counterpart of
/// `NormalizedProductRecord` (product_record.hpp), which is the
/// input/validation-side type. Mirrors the split `domain::Job`/
/// `services::CreateJobRequest` and `domain::Workload`/
/// `services::CreateWorkloadRequest` already use: what gets validated on
/// the way in is not the same shape as what gets read back out (a
/// persisted row has an `id`/timestamps/provenance a normalized input
/// record does not).
///
/// A plain aggregate, not a class with invariants to encapsulate (unlike
/// `Workload`, which derives `status()` from child jobs): every field
/// here is exactly what's in the row, nothing computed -- see
/// docs/architecture/product-processing.md, "Product persistence".
///
/// `job_id` is how a product's provenance -- "which import batch created
/// or last updated this row" -- is recoverable without duplicating
/// `workload_id` onto this table: `job_id` -> `jobs.workload_id` already
/// answers "which workload" via a join, and `jobs` is already the source
/// of truth for a job's own workload association (see
/// docs/architecture/workload-model.md). `std::nullopt` for a product
/// that predates this column meaning anything, or was written outside
/// the import path (defensive; nothing in this codebase does that today).
struct Product {
  infra::ProductId id;
  std::string sku;
  std::string name;
  double price = 0.0;
  std::string currency;
  std::optional<std::string> category;
  std::optional<std::string> description;
  std::int64_t stock_quantity = 0;
  std::optional<infra::JobId> job_id;
  infra::TimePoint created_at;
  infra::TimePoint updated_at;
};

}  // namespace flowforge::domain
