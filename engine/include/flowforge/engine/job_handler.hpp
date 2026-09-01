#pragma once

#include <string>
#include <string_view>

#include "flowforge/domain/execution_result.hpp"
#include "flowforge/engine/execution_context.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Executable unit of work FlowForge dispatches a job to, keyed by
/// `job_type()` in `HandlerRegistry` (handler_registry.hpp). A handler
/// receives only an opaque payload plus a narrow `ExecutionContext` --
/// never a database connection, an HTTP request/response, or the
/// dashboard -- so it stays unit-testable in isolation and portable
/// across whatever eventually invokes it (a future Executor, or a test
/// calling it directly, as in engine/tests/handlers).
///
/// Handlers must not: execute arbitrary shell commands or user-supplied
/// code, access raw database credentials, access arbitrary filesystem
/// paths, or bypass application authorization. `payload` is untrusted
/// input (see docs/architecture/execution-model.md, "Security") --
/// implementations are expected to validate it and bound any resource
/// use it can influence (the built-in `DelayHandler`'s duration cap is
/// the model for this).
///
/// Return value contract: `execute()` returns an `Error` (the `Result`'s
/// error channel) only for conditions that mean the handler could not
/// meaningfully attempt the work at all -- e.g. a payload that fails a
/// hard structural/bounds check before any real work starts. Anything
/// that counts as "the job ran and did not succeed" (including
/// cooperative cancellation partway through) is reported as
/// `domain::ExecutionResult::failure(...)`, not as this `Result`'s error
/// channel. This mirrors how `JobService` already treats "caller input
/// rejected" (Result error) as distinct from "operation ran to a
/// terminal, unsuccessful state" (a domain value, e.g. `JobStatus`).
///
/// Thread-safety: once registered, a single `IJobHandler` instance is
/// shared (via the same `shared_ptr`) across every concurrent caller
/// that resolves it -- implementations MUST be safe to call concurrently
/// from multiple threads. The simplest way to satisfy this is to keep
/// handlers stateless (all three built-in handlers are); a handler that
/// needs per-invocation state should keep it in a local variable inside
/// `execute()`, never in a data member mutated without synchronization.
class IJobHandler {
 public:
  IJobHandler() = default;
  virtual ~IJobHandler() = default;
  IJobHandler(const IJobHandler&) = delete;
  IJobHandler& operator=(const IJobHandler&) = delete;
  IJobHandler(IJobHandler&&) = delete;
  IJobHandler& operator=(IJobHandler&&) = delete;

  /// Short, stable identifier for this handler's job type (e.g. "echo").
  /// This is what `HandlerRegistry` registers/looks it up by; it must be
  /// non-empty and should not change across the handler's lifetime.
  [[nodiscard]] virtual std::string_view job_type() const noexcept = 0;

  [[nodiscard]] virtual Result<domain::ExecutionResult> execute(const ExecutionContext& context,
                                                                const std::string& payload) = 0;
};

}  // namespace flowforge::engine
