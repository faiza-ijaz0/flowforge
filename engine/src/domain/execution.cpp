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

}  // namespace flowforge::domain
