#include "flowforge/domain/job.hpp"

namespace flowforge::domain {

std::string_view to_string(JobStatus status) noexcept {
  switch (status) {
    case JobStatus::Pending:
      return "pending";
    case JobStatus::Queued:
      return "queued";
    case JobStatus::Running:
      return "running";
    case JobStatus::Succeeded:
      return "succeeded";
    case JobStatus::Failed:
      return "failed";
    case JobStatus::Retrying:
      return "retrying";
    case JobStatus::Cancelled:
      return "cancelled";
    case JobStatus::DeadLetter:
      return "dead_letter";
  }
  return "unknown";
}

bool is_terminal(JobStatus status) noexcept {
  switch (status) {
    case JobStatus::Succeeded:
    case JobStatus::Cancelled:
    case JobStatus::DeadLetter:
      return true;
    default:
      return false;
  }
}

std::optional<JobStatus> job_status_from_string(std::string_view value) noexcept {
  if (value == "pending")
    return JobStatus::Pending;
  if (value == "queued")
    return JobStatus::Queued;
  if (value == "running")
    return JobStatus::Running;
  if (value == "succeeded")
    return JobStatus::Succeeded;
  if (value == "failed")
    return JobStatus::Failed;
  if (value == "retrying")
    return JobStatus::Retrying;
  if (value == "cancelled")
    return JobStatus::Cancelled;
  if (value == "dead_letter")
    return JobStatus::DeadLetter;
  return std::nullopt;
}

Job Job::restore(infra::JobId id, std::string queue_name, std::string payload, RetryPolicy retry_policy,
                 int priority, JobStatus status, std::uint32_t attempt_count,
                 std::optional<std::string> last_error, infra::TimePoint created_at,
                 infra::TimePoint updated_at, std::string job_type,
                 std::optional<infra::WorkloadId> workload_id) {
  Job job(std::move(id), std::move(queue_name), std::move(payload), retry_policy, created_at, priority,
          std::move(job_type), std::move(workload_id));
  job.status_ = status;
  job.attempt_count_ = attempt_count;
  job.last_error_ = std::move(last_error);
  job.updated_at_ = updated_at;
  return job;
}

}  // namespace flowforge::domain
