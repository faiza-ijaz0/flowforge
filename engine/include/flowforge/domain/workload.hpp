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

/// The three buckets a child job's `JobStatus` is classified into for
/// workload progress aggregation (see `classify_job_status_for_workload`).
enum class WorkloadItemOutcome : std::uint8_t { Active, Completed, Failed };

/// Classifies a child job's current `JobStatus` for workload aggregation:
///   - `Succeeded`                        -> Completed
///   - `Cancelled` / `DeadLetter`          -> Failed (both are terminal and unsuccessful)
///   - everything else (`Pending`/`Queued`/`Running`/`Failed`/`Retrying`)  -> Active
///
/// `JobStatus::Failed` is deliberately `Active`, not `Failed`, here: a
/// failed *attempt* is not terminal by itself (see `domain::is_terminal
/// (JobStatus)`) -- `RetryDispatcher` may still retry it, so it is not yet
/// a workload-level failure until the job reaches `Cancelled`/`DeadLetter`
/// (retries exhausted) or `Succeeded`. See docs/architecture/
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
  [[nodiscard]] std::size_t completed_items() const noexcept { return completed_items_; }
  [[nodiscard]] std::size_t failed_items() const noexcept { return failed_items_; }
  [[nodiscard]] WorkloadStatus status() const noexcept { return status_; }
  [[nodiscard]] infra::TimePoint created_at() const noexcept { return created_at_; }
  [[nodiscard]] infra::TimePoint updated_at() const noexcept { return updated_at_; }

  /// Applies a freshly-computed progress snapshot: sets completed_items()/
  /// failed_items() and re-derives status() via derive_workload_status().
  /// Deliberately does not touch updated_at() -- see class comment:
  /// progress is computed on demand, not persisted, so there is no
  /// meaningful "row last written" moment to advance here; updated_at()
  /// continues to reflect the persisted row (which this phase never
  /// mutates after creation).
  void apply_progress(std::size_t completed_items, std::size_t failed_items) noexcept {
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
  std::size_t completed_items_ = 0;
  std::size_t failed_items_ = 0;
  WorkloadStatus status_ = WorkloadStatus::Pending;
  infra::TimePoint created_at_;
  infra::TimePoint updated_at_;
};

}  // namespace flowforge::domain
