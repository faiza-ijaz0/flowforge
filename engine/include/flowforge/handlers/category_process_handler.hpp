#pragma once

#include <memory>

#include "flowforge/engine/job_handler.hpp"
#include "flowforge/persistence/category_repository.hpp"

namespace flowforge::handlers {

/// The Category domain's workload-processing handler (Phase 3F), mirroring
/// `ProductProcessHandler`'s conventions -- constructor-injected
/// repository, never reached through `engine::ExecutionContext` (see that
/// class's own doc comment for why) -- with one addition: this handler
/// also validates `parent_slug` references before writing (see "Parent
/// validation" below), which `ProductProcessHandler` has no equivalent of.
/// Registered under job_type "category.process", the same string
/// `domain::job_type_for_processing_target(ProcessingTarget::Categories)`
/// and `services::CreateWorkloadRequest::type` use for a category-import
/// workload.
///
/// Validates and normalizes one category record's payload -- a flat JSON
/// object `{"name","slug","description"?,"parent_slug"?}`, every value a
/// JSON string (see `domain::serialize_category_record_as_job_payload`)
/// -- via the same `domain::validate_and_normalize_category_record()` the
/// Processing Center's preview/confirm path already ran at import time
/// (see `services::map_structured_records_to_categories`).
///
/// **Parent validation.** `validate_and_normalize_category_record()` is
/// pure (no database access), so it can only catch a record referencing
/// *itself* as parent -- it cannot know whether `parent_slug` refers to a
/// category that actually exists, or whether accepting it would create a
/// multi-hop cycle (A's parent is B, B's parent becomes A). Both of those
/// require a database read, so this handler -- which, unlike
/// `InputProcessingService::preview()`/`confirm()`, owns a repository --
/// performs them here, at job-execution time, before calling `upsert()`:
/// walking the chain of `parent_slug` references via `find_by_slug()`,
/// rejecting (non-retryable) if any link is missing or if the record's
/// own slug reappears in the chain. This means a record whose parent
/// doesn't exist yet is accepted at preview/confirm time (structurally
/// valid) and only fails when its job actually executes -- see
/// docs/architecture/category-processing.md, "Why parent-existence is
/// checked at execution time, not preview/confirm time", and "Parent
/// semantics" for the two-stage import strategy this implies (import
/// parent categories first, then children that reference them).
///
/// Retryability: a validation failure -- malformed/missing fields, a
/// self-referencing parent, a missing parent, or a cyclic parent chain --
/// is non-retryable: none of those can be fixed by simply retrying the
/// same job. One exception (Phase 3H follow-up): when the payload carries
/// `parent_in_submission` (set by `InputProcessingService::confirm()`) and
/// the *immediate* parent is missing, the failure is retryable -- that
/// parent's own job, from the same submission, runs in parallel and may
/// simply not have committed yet. If it never appears, the job exhausts
/// its retry policy and ends in dead_letter. A `categories` table write failure
/// (`ErrorCode::Database`/`Infrastructure`) is retryable=true, identical
/// to `ProductProcessHandler`.
class CategoryProcessHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "category.process";

  explicit CategoryProcessHandler(std::shared_ptr<persistence::ICategoryRepository> category_repository)
      : category_repository_(std::move(category_repository)) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;

 private:
  std::shared_ptr<persistence::ICategoryRepository> category_repository_;
};

}  // namespace flowforge::handlers
