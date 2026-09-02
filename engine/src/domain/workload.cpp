#include "flowforge/domain/workload.hpp"

namespace flowforge::domain {

std::string_view to_string(WorkloadStatus status) noexcept {
  switch (status) {
    case WorkloadStatus::Pending:
      return "pending";
    case WorkloadStatus::Queued:
      return "queued";
    case WorkloadStatus::Running:
      return "running";
    case WorkloadStatus::Succeeded:
      return "succeeded";
    case WorkloadStatus::Failed:
      return "failed";
  }
  return "unknown";
}

bool is_terminal(WorkloadStatus status) noexcept {
  return status == WorkloadStatus::Succeeded || status == WorkloadStatus::Failed;
}

std::optional<WorkloadStatus> workload_status_from_string(std::string_view value) noexcept {
  if (value == "pending")
    return WorkloadStatus::Pending;
  if (value == "queued")
    return WorkloadStatus::Queued;
  if (value == "running")
    return WorkloadStatus::Running;
  if (value == "succeeded")
    return WorkloadStatus::Succeeded;
  if (value == "failed")
    return WorkloadStatus::Failed;
  return std::nullopt;
}

WorkloadStatus derive_workload_status(std::size_t total_items, std::size_t completed_items,
                                      std::size_t failed_items) noexcept {
  // A workload with nothing to do is vacuously complete -- there is no
  // meaningful "Running" state for zero items, and leaving it Pending
  // forever would be misleading (nothing will ever move it further).
  if (total_items == 0) {
    return WorkloadStatus::Succeeded;
  }
  // At least one item has not yet reached a terminal outcome.
  if (completed_items + failed_items < total_items) {
    return WorkloadStatus::Running;
  }
  // Every item is terminal: any failure makes the whole workload Failed,
  // matching the "terminal failures -> Failed" rule -- there is no
  // partial-success status in this phase (see workload.hpp's class
  // comment on WorkloadStatus).
  return failed_items > 0 ? WorkloadStatus::Failed : WorkloadStatus::Succeeded;
}

WorkloadItemOutcome classify_job_status_for_workload(JobStatus status) noexcept {
  switch (status) {
    case JobStatus::Succeeded:
      return WorkloadItemOutcome::Succeeded;
    case JobStatus::Cancelled:
    case JobStatus::DeadLetter:
      return WorkloadItemOutcome::Failed;
    case JobStatus::Running:
      return WorkloadItemOutcome::Running;
    case JobStatus::Pending:
    case JobStatus::Queued:
    case JobStatus::Retrying:
    case JobStatus::Failed:
      return WorkloadItemOutcome::Queued;
  }
  return WorkloadItemOutcome::Queued;
}

Workload Workload::restore(infra::WorkloadId id, std::string type, std::size_t total_items,
                           infra::TimePoint created_at, infra::TimePoint updated_at) {
  Workload workload(std::move(id), std::move(type), total_items, created_at);
  workload.updated_at_ = updated_at;
  return workload;
}

}  // namespace flowforge::domain
