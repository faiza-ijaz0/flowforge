#include "flowforge/domain/execution_result.hpp"

namespace flowforge::domain {

std::string_view to_string(ExecutionResultStatus status) noexcept {
  switch (status) {
    case ExecutionResultStatus::Succeeded:
      return "succeeded";
    case ExecutionResultStatus::Failed:
      return "failed";
  }
  return "unknown";
}

ExecutionResult ExecutionResult::success(std::string output, std::chrono::milliseconds duration,
                                         std::map<std::string, std::string> metadata) {
  ExecutionResult result;
  result.status_ = ExecutionResultStatus::Succeeded;
  result.output_ = std::move(output);
  result.duration_ = duration;
  result.metadata_ = std::move(metadata);
  return result;
}

ExecutionResult ExecutionResult::failure(ErrorCode error_code, std::string error_message, bool retryable,
                                         std::chrono::milliseconds duration,
                                         std::map<std::string, std::string> metadata) {
  ExecutionResult result;
  result.status_ = ExecutionResultStatus::Failed;
  result.error_code_ = error_code;
  result.error_message_ = std::move(error_message);
  result.retryable_ = retryable;
  result.duration_ = duration;
  result.metadata_ = std::move(metadata);
  return result;
}

}  // namespace flowforge::domain
