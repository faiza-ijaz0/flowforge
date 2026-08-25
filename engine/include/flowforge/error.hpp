#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace flowforge {

/// Coarse-grained classification of everything that can go wrong in
/// FlowForge. Callers switch on this to decide how to react (e.g. map to
/// an HTTP status code, decide whether a job attempt is retryable, or
/// decide whether a failure should crash the process at startup).
enum class ErrorCode : std::uint8_t {
  Validation,      ///< Caller-supplied input failed a validation rule.
  Configuration,   ///< Startup/environment configuration is missing or invalid.
  Infrastructure,  ///< A dependency of the process (disk, OS, thread creation...) failed.
  Database,        ///< A persistence-layer operation failed.
  Network,         ///< An outbound or inbound network operation failed.
  JobExecution,    ///< A job/workflow failed while executing user work.
  NotFound,        ///< A requested entity does not exist.
  Conflict,        ///< The operation conflicts with the current state of an entity.
  Internal,        ///< An invariant was violated; indicates a FlowForge bug.
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Validation:
      return "validation_error";
    case ErrorCode::Configuration:
      return "configuration_error";
    case ErrorCode::Infrastructure:
      return "infrastructure_error";
    case ErrorCode::Database:
      return "database_error";
    case ErrorCode::Network:
      return "network_error";
    case ErrorCode::JobExecution:
      return "job_execution_error";
    case ErrorCode::NotFound:
      return "not_found";
    case ErrorCode::Conflict:
      return "conflict";
    case ErrorCode::Internal:
      return "internal_error";
  }
  return "unknown_error";
}

/// Lightweight, allocation-cheap error value used throughout FlowForge as
/// the error type of `Result<T>` (see result.hpp). FlowForge does not use
/// exceptions for expected/recoverable failure paths -- see
/// docs/architecture/overview.md ("Error Handling Strategy") for the
/// rationale. Exceptions are still permitted for truly exceptional,
/// unrecoverable conditions (e.g. std::bad_alloc), which are allowed to
/// propagate and terminate the process.
class Error {
 public:
  Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }

 private:
  ErrorCode code_;
  std::string message_;
};

[[nodiscard]] inline Error make_error(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

}  // namespace flowforge
