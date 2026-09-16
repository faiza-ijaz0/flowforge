#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "flowforge/domain/job.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

/// See docs/architecture/workload-model.md for the full lifecycle. A
/// Workload is a logical grouping of related Jobs submitted as one unit
/// (e.g. one CSV import); this enum is intentionally smaller than
/// `JobStatus` -- workload-level status only needs to distinguish "not
/// started" / "in flight" / terminal outcomes, not every intermediate Job
/// state.
enum class WorkloadStatus : std::uint8_t {
  Pending,    ///< Created; no child jobs have been dispatched yet.
  Queued,     ///< Child jobs handed to the Scheduler; none has progressed further yet.
  Running,    ///< At least one child job has not yet reached a terminal outcome.
  Succeeded,  ///< Every child job succeeded (or total_items == 0 -- a vacuously complete workload).
  Failed,     ///< Every child job reached a terminal outcome and at least one did not succeed.
};

[[nodiscard]] std::string_view to_string(WorkloadStatus status) noexcept;
[[nodiscard]] bool is_terminal(WorkloadStatus status) noexcept;

/// Inverse of to_string(WorkloadStatus), mirroring job_status_from_string's
/// role for persistence implementations.
[[nodiscard]] std::optional<WorkloadStatus> workload_status_from_string(std::string_view value) noexcept;

/// Deterministically derives a workload's status from its item counts --
/// see docs/architecture/workload-model.md, "Status derivation" for the
/// bullet-by-bullet rationale. Pure function: no I/O, no locking, safe to
/// call against any point-in-time snapshot of child-job outcomes. The only
/// caller in this phase is `services::WorkloadService` (see
/// `Workload::apply_progress`).
///
/// `completed_items + failed_items` is expected to be `<= total_items` --
/// callers derive both by classifying every child Job's status exactly
/// once via `classify_job_status_for_workload`, so this invariant holds by
/// construction and is not re-validated here.
[[nodiscard]] WorkloadStatus derive_workload_status(std::size_t total_items, std::size_t completed_items,
                                                    std::size_t failed_items) noexcept;

/// The four buckets a child job's `JobStatus` is classified into for
/// workload progress aggregation (see `classify_job_status_for_workload`).
/// `Queued` and `Running` are both "not yet terminal"; kept as separate
/// buckets (Phase 3B) rather than one combined "Active" bucket so the UI
/// can render a real `queued`/`running`/`succeeded`/`failed` breakdown
/// (see docs/architecture/user-import.md, "Progress calculation") instead
/// of only a two-way completed/failed split (Phase 3A).
enum class WorkloadItemOutcome : std::uint8_t { Queued, Running, Succeeded, Failed };

/// Classifies a child job's current `JobStatus` for workload aggregation:
///   - `Pending` / `Queued` / `Retrying`     -> Queued  (not currently executing; will run (again) soon)
///   - `Running`                             -> Running (actively executing right now)
///   - `Succeeded`                           -> Succeeded
///   - `Failed` / `Cancelled` / `DeadLetter` -> Failed  (all three are, in practice, permanent)
///
/// `JobStatus::Retrying` is `Queued`, not `Failed`, here: `RetryDispatcher`
/// polls specifically for `Retrying` jobs (see retry_dispatcher.cpp) and
/// will re-submit it, so it is not yet a workload-level failure. By
/// contrast, `JobStatus::Failed` -- set by `Job::record_execution_failure`
/// for a handler's non-retryable error (see that method's doc comment) --
/// is never picked up by `RetryDispatcher` (which only ever queries
/// `Retrying`): nothing in this codebase moves a `Failed` job any further,
/// so it must count as a workload-level failure immediately, not sit
/// classified as "waiting for its turn" forever. See docs/architecture/
/// workload-model.md, "Status derivation".
[[nodiscard]] WorkloadItemOutcome classify_job_status_for_workload(JobStatus status) noexcept;

/// Domain representation of a workload: a logical grouping of related jobs
/// submitted as one unit (see docs/architecture/workload-model.md). Mirrors
/// `Job`'s shape/conventions: deliberately does not depend on any JSON
/// library, and identifiers/timestamps use the same `infra::Id<Tag>`/
/// `infra::TimePoint` conventions as every other domain type.
///
/// Unlike `Job`, `completed_items()`/`failed_items()`/`status()` are NOT
/// persisted fields that get written back to a repository -- they are
/// computed on demand from the workload's child `Job` rows (via
/// `classify_job_status_for_workload` + `derive_workload_status`), so they
/// can never drift out of sync with the `Job` rows that are the actual
/// source of truth for execution outcome. A freshly-constructed Workload
/// defaults to zero progress and `WorkloadStatus::Pending`;
/// `services::WorkloadService` calls `apply_progress()` before returning
/// one to any caller (see workload-model.md, "Why there is no update()").
class Workload {
 public:
  Workload(infra::WorkloadId id, std::string type, std::size_t total_items, infra::TimePoint created_at)
      : id_(std::move(id)),
        type_(std::move(type)),
        total_items_(total_items),
        created_at_(created_at),
        updated_at_(created_at) {}

  [[nodiscard]] const infra::WorkloadId& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& type() const noexcept { return type_; }
  [[nodiscard]] std::size_t total_items() const noexcept { return total_items_; }
  [[nodiscard]] std::size_t queued_items() const noexcept { return queued_items_; }
  [[nodiscard]] std::size_t running_items() const noexcept { return running_items_; }
  [[nodiscard]] std::size_t completed_items() const noexcept { return completed_items_; }
  [[nodiscard]] std::size_t failed_items() const noexcept { return failed_items_; }
  [[nodiscard]] WorkloadStatus status() const noexcept { return status_; }
  [[nodiscard]] infra::TimePoint created_at() const noexcept { return created_at_; }
  [[nodiscard]] infra::TimePoint updated_at() const noexcept { return updated_at_; }

  /// Applies a freshly-computed progress snapshot (Phase 3B: all four
  /// `WorkloadItemOutcome` buckets, not just completed/failed) and
  /// re-derives status() via derive_workload_status() (which only ever
  /// needed completed/failed -- see that function's doc comment; queued/
  /// running are purely additive display detail, invisible to status
  /// derivation). Deliberately does not touch updated_at() -- see class
  /// comment: progress is computed on demand, not persisted, so there is
  /// no meaningful "row last written" moment to advance here;
  /// updated_at() continues to reflect the persisted row (which this
  /// phase never mutates after creation).
  void apply_progress(std::size_t queued_items, std::size_t running_items, std::size_t completed_items,
                      std::size_t failed_items) noexcept {
    queued_items_ = queued_items;
    running_items_ = running_items;
    completed_items_ = completed_items;
    failed_items_ = failed_items;
    status_ = derive_workload_status(total_items_, completed_items_, failed_items_);
  }

  /// Reconstructs a Workload from an already-persisted row (id/type/
  /// total_items/timestamps only -- see class comment). Progress/status
  /// are left at their defaults (0, 0, Pending); callers apply real
  /// progress separately via apply_progress() once child-job counts are
  /// known. Mirrors `domain::Job::restore()`'s role for repository
  /// implementations.
  [[nodiscard]] static Workload restore(infra::WorkloadId id, std::string type, std::size_t total_items,
                                        infra::TimePoint created_at, infra::TimePoint updated_at);

 private:
  infra::WorkloadId id_;
  std::string type_;
  std::size_t total_items_;
  std::size_t queued_items_ = 0;
  std::size_t running_items_ = 0;
  std::size_t completed_items_ = 0;
  std::size_t failed_items_ = 0;
  WorkloadStatus status_ = WorkloadStatus::Pending;
  infra::TimePoint created_at_;
  infra::TimePoint updated_at_;
};

}  // namespace flowforge::domain
