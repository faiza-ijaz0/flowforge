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

}  // namespace flowforge::domain
