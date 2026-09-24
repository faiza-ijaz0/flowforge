#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "flowforge/result.hpp"

namespace flowforge::domain {

/// A validated, normalized category record ready to become a
/// `category.process` job payload -- the single, shared representation
/// both `handlers::CategoryProcessHandler` (validating one job's payload
/// at execution time) and the Processing Center's CSV/image import path
/// (validating one imported record at preview/confirm time, see
/// `services::map_structured_records_to_categories`) produce via the same
/// function below, mirroring `domain::NormalizedProductRecord`'s pattern
/// (docs/architecture/product-processing.md) exactly -- see
/// docs/architecture/category-processing.md, "Category domain".
///
/// Field choice (Phase 3F): `name` is the only required input field.
/// `slug` is always present in the *output* (a `NormalizedCategoryRecord`
/// can never have a blank slug), but is not required as *input* --
/// provide it explicitly to pin an exact machine-readable identifier, or
/// leave it blank/absent to have it deterministically derived from `name`
/// (see `slugify()` in the .cpp and category-processing.md, "Slug
/// semantics"). `description`/`parent_slug` are optional, mirroring
/// `NormalizedProductRecord::description`. Deliberately excludes a
/// recursive category-tree model (child lists, materialized paths, depth
/// counters, etc.) -- the brief calls for "reliable bulk category
/// ingestion, not a full CMS", and a single optional `parent_slug`
/// reference is the minimum shape that satisfies "hierarchy" without one.
struct NormalizedCategoryRecord {
  std::string name;
  std::string slug;
  std::optional<std::string> description;
  std::optional<std::string> parent_slug;
};

/// Trims `name`; derives (or normalizes an explicitly-provided) `slug` via
/// `slugify()` -- lowercase, collapse any run of non-alphanumeric
/// characters into a single `-`, trim leading/trailing `-`; trims optional
/// `description`; normalizes optional `parent_slug` through the exact
/// same `slugify()` step, and rejects it if it would equal this record's
/// own slug (a category cannot be its own parent -- see
/// category-processing.md, "Parent semantics"). Returns
/// `ErrorCode::Validation` describing the first rule that failed --
/// deterministic, not accumulating every violation, exactly like
/// `validate_and_normalize_product_record`.
///
/// Deliberately does NOT check whether `parent_slug` refers to an
/// existing category -- that requires a database read, and this function
/// (like every `validate_and_normalize_*_record` in this codebase) is
/// pure/stateless, callable from preview (which never touches the
/// database) as well as from job execution. The existence check happens
/// in `handlers::CategoryProcessHandler`, which owns a repository -- see
/// category-processing.md, "Why parent-existence is not checked here".
[[nodiscard]] Result<NormalizedCategoryRecord> validate_and_normalize_category_record(
    std::string_view name, std::optional<std::string_view> slug, std::optional<std::string_view> description,
    std::optional<std::string_view> parent_slug);

/// Deterministically derives a URL/identifier-safe slug from arbitrary
/// text: lowercases, collapses any run of characters that are neither
/// ASCII letters nor digits into a single `-`, and never produces a
/// leading or trailing `-`. Returns an empty string if `value` contains no
/// letters or digits at all (e.g. `"---"` or `"???"`) -- callers must
/// treat that as "no valid slug could be derived", not silently accept an
/// empty identifier. Exposed (not file-local) so
/// `services::map_structured_records_to_categories` and tests can both
/// reason about slug derivation without duplicating the algorithm -- see
/// category-processing.md, "Slug semantics" for the exact rules and
/// worked examples.
[[nodiscard]] std::string slugify(std::string_view value);

/// Serializes `record` as the flat JSON object `category.process` expects
/// as its job payload -- every field a JSON string, `description`/
/// `parent_slug` omitted entirely when unset (mirrors
/// `serialize_product_record_as_job_payload`'s "no JSON library in
/// engine/" rationale): hand-rolled, not `nlohmann::json`.
///
/// `parent_in_submission` adds `"parent_in_submission":"true"`: set by
/// `InputProcessingService::confirm()` when `parent_slug` names another
/// record of the same submission, so `CategoryProcessHandler` treats a
/// not-yet-persisted parent as retryable (sibling jobs execute in
/// parallel, so the parent's job may simply not have committed yet).
[[nodiscard]] std::string serialize_category_record_as_job_payload(const NormalizedCategoryRecord& record,
                                                                   bool parent_in_submission = false);

}  // namespace flowforge::domain
