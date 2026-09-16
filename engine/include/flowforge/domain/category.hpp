#pragma once

#include <optional>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

/// The persisted representation of a category (database/migrations/
/// 0015_create_categories.sql) -- the read-model counterpart of
/// `NormalizedCategoryRecord` (category_record.hpp), mirroring the
/// `Product`/`NormalizedProductRecord` split exactly (see product.hpp's
/// class comment for the general rationale).
///
/// A plain aggregate, not a class with invariants: every field here is
/// exactly what's in the row, nothing computed. `parent_slug` is a bare
/// string reference to another category's `slug` (or `std::nullopt` for a
/// top-level category) -- deliberately not a resolved pointer/id to a
/// parent `Category`, since a persisted row must remain independently
/// meaningful even if this type is read without also loading its parent
/// (see docs/architecture/category-processing.md, "Parent semantics").
///
/// `job_id` mirrors `Product::job_id`'s provenance rationale exactly:
/// which import batch created or last updated this row, recoverable via
/// `job_id -> jobs.workload_id`.
struct Category {
  infra::CategoryId id;
  std::string name;
  std::string slug;
  std::optional<std::string> description;
  std::optional<std::string> parent_slug;
  std::optional<infra::JobId> job_id;
  infra::TimePoint created_at;
  infra::TimePoint updated_at;
};

}  // namespace flowforge::domain
