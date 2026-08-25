#include "flowforge/domain/workflow.hpp"

namespace flowforge::domain {

std::string_view to_string(WorkflowStatus status) noexcept {
  switch (status) {
    case WorkflowStatus::Pending:
      return "pending";
    case WorkflowStatus::Running:
      return "running";
    case WorkflowStatus::Succeeded:
      return "succeeded";
    case WorkflowStatus::Failed:
      return "failed";
    case WorkflowStatus::Cancelled:
      return "cancelled";
  }
  return "unknown";
}

}  // namespace flowforge::domain
