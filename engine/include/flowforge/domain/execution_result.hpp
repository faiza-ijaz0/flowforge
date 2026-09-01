#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "flowforge/error.hpp"

namespace flowforge::domain {

enum class ExecutionResultStatus : std::uint8_t { Succeeded, Failed };

[[nodiscard]] std::string_view to_string(ExecutionResultStatus status) noexcept;

/// Outcome of a single `engine::IJobHandler::execute()` invocation --
/// distinct from `domain::Execution` (execution.hpp), which is the
/// persisted `job_attempts` row a future Executor will write once
/// Phase 2B-2 exists. `ExecutionResult` is the handler layer's own return
/// value: richer (output, structured metadata, a retryability
/// classification) because it is produced and consumed entirely in
/// memory, with no schema to keep minimal. Mapping an `ExecutionResult`
/// to a persisted `Execution` record is the future Executor's job, not
/// this type's.
///
/// Like `domain::Job::payload()`, `output` is an opaque, already-
/// serialized string rather than a JSON value: the domain layer must not
/// depend on a JSON library (see docs/architecture/overview.md,
/// "Dependency direction"), so a handler that wants to return structured
/// output serializes it itself before returning.
///
/// There is deliberately no public default constructor -- every
/// `ExecutionResult` is built via `success()`/`failure()` so its fields
/// can never be in an inconsistent combination (e.g. `retryable() ==
/// true` on a result with `status() == Succeeded`).
class ExecutionResult {
 public:
  [[nodiscard]] static ExecutionResult success(std::string output, std::chrono::milliseconds duration,
                                               std::map<std::string, std::string> metadata = {});

  /// `error_code` classifies *why* the handler's own business logic
  /// failed (reusing the existing `ErrorCode` taxonomy -- `JobExecution`
  /// is the natural default for "the job ran and its work failed").
  /// `retryable` is a hint for the future Executor/retry engine: a
  /// handler is best positioned to know whether its own failure is
  /// transient (e.g. a downstream timeout) or permanent (e.g. the
  /// payload can never succeed), even though no retry engine consumes
  /// this hint yet (see docs/architecture/overview.md, "Deferred to
  /// Phase 2").
  [[nodiscard]] static ExecutionResult failure(ErrorCode error_code, std::string error_message,
                                               bool retryable, std::chrono::milliseconds duration,
                                               std::map<std::string, std::string> metadata = {});

  [[nodiscard]] ExecutionResultStatus status() const noexcept { return status_; }
  [[nodiscard]] bool succeeded() const noexcept { return status_ == ExecutionResultStatus::Succeeded; }
  [[nodiscard]] const std::string& output() const noexcept { return output_; }
  [[nodiscard]] const std::optional<ErrorCode>& error_code() const noexcept { return error_code_; }
  [[nodiscard]] const std::optional<std::string>& error_message() const noexcept { return error_message_; }
  [[nodiscard]] bool retryable() const noexcept { return retryable_; }
  [[nodiscard]] std::chrono::milliseconds duration() const noexcept { return duration_; }
  [[nodiscard]] const std::map<std::string, std::string>& metadata() const noexcept { return metadata_; }

 private:
  ExecutionResult() = default;

  ExecutionResultStatus status_ = ExecutionResultStatus::Succeeded;
  std::string output_;
  std::optional<ErrorCode> error_code_;
  std::optional<std::string> error_message_;
  bool retryable_ = false;
  std::chrono::milliseconds duration_{0};
  std::map<std::string, std::string> metadata_;
};

}  // namespace flowforge::domain
