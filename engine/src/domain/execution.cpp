#include "flowforge/domain/execution.hpp"

namespace flowforge::domain {

std::string_view to_string(ExecutionOutcome outcome) noexcept {
  switch (outcome) {
    case ExecutionOutcome::Running:
      return "running";
    case ExecutionOutcome::Succeeded:
      return "succeeded";
    case ExecutionOutcome::Failed:
      return "failed";
    case ExecutionOutcome::TimedOut:
      return "timed_out";
    case ExecutionOutcome::Cancelled:
      return "cancelled";
  }
  return "unknown";
}

std::optional<ExecutionOutcome> execution_outcome_from_string(std::string_view value) noexcept {
  if (value == "running")
    return ExecutionOutcome::Running;
  if (value == "succeeded")
    return ExecutionOutcome::Succeeded;
  if (value == "failed")
    return ExecutionOutcome::Failed;
  if (value == "timed_out")
    return ExecutionOutcome::TimedOut;
  if (value == "cancelled")
    return ExecutionOutcome::Cancelled;
  return std::nullopt;
}

}  // namespace flowforge::domain
