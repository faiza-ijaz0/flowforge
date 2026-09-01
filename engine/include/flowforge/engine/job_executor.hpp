#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/engine/executor.hpp"
#include "flowforge/engine/handler_registry.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"

namespace flowforge::engine {

/// Real `IExecutor` implementation (Phase 2B-3): the component that
/// actually calls `IJobHandler::execute()`. Owns the full
/// Queued->Running->Succeeded/Failed transition and `job_attempts`
/// persistence for one attempt -- see docs/architecture/execution-model.md
/// §10 for the full sequence and rationale.
///
/// Deliberately does NOT: know about HTTP, own or call into `IScheduler`,
/// open a PostgreSQL connection directly (goes through
/// `persistence::IJobRepository`/`IExecutionManager` like everything
/// else), contain a job-type switch statement (always resolves through
/// `HandlerRegistry`), or execute arbitrary code.
class JobExecutor final : public IExecutor {
 public:
  JobExecutor(std::shared_ptr<HandlerRegistry> handler_registry,
              std::shared_ptr<persistence::IJobRepository> job_repository,
              std::shared_ptr<IExecutionManager> execution_manager, std::shared_ptr<infra::Clock> clock,
              std::shared_ptr<infra::Logger> logger,
              std::shared_ptr<infra::MetricsRegistry> metrics = nullptr,
              std::chrono::milliseconds execution_timeout = std::chrono::milliseconds{60'000});

  /// Runs one execution attempt for `job` on behalf of `worker_id`, using
  /// `cancellation_flag` as the shared cooperative-cancellation channel
  /// for this attempt (see executor.hpp's class comment). Sequence:
  ///
  ///  1. Re-fetch the job's current persisted state; if it is already
  ///     terminal (e.g. cancelled by a racing HTTP request), do not
  ///     execute and do not overwrite that state -- return
  ///     `ErrorCode::Conflict`.
  ///  2. Transition Queued -> Running, persist.
  ///  3. Resolve the handler via `HandlerRegistry` (never a job-type
  ///     switch statement).
  ///  4. Build a fresh `ExecutionContext` for this attempt and call
  ///     `IJobHandler::execute()`, racing it against `execution_timeout`
  ///     (a watcher thread that cooperatively sets `cancellation_flag` if
  ///     the handler hasn't finished in time -- never a forced thread
  ///     kill; see class comment on timeout below).
  ///  5. Re-fetch once more: if the job was cancelled *during* execution
  ///     (a racing cancel request persisted `Cancelled` while the handler
  ///     was still running), respect that -- the job must not be flipped
  ///     to `Succeeded`/`Failed` after being cancelled.
  ///  6. Otherwise, transition Running -> Succeeded/Failed/Retrying/DeadLetter
  ///     based on the `ExecutionResult` (a failure is only Retrying/
  ///     DeadLetter -- per `RetryPolicy` -- if the handler declared it
  ///     `retryable()`; otherwise Failed, same as before Phase 2B-4),
  ///     persist, and record the `job_attempts` row (via
  ///     `IExecutionManager`) with outcome
  ///     Succeeded/Failed/Cancelled/TimedOut as appropriate -- the
  ///     attempt's own recorded outcome is always `Failed` for a failed
  ///     attempt regardless of whether the *job* goes on to retry; only
  ///     `domain::Job::status()` distinguishes "this attempt failed but
  ///     another will follow" from "this attempt failed for good".
  ///
  /// Timeout is cooperative only: if a handler ignores
  /// `ExecutionContext::is_cancelled()` (as `EchoHandler`/`TransformHandler`
  /// do -- they finish essentially instantly regardless), the watcher
  /// thread still returns/joins on its own once the handler eventually
  /// completes; nothing is ever forcibly terminated.
  [[nodiscard]] Result<domain::Execution> execute(
      const domain::Job& job, const infra::WorkerId& worker_id,
      std::shared_ptr<std::atomic<bool>> cancellation_flag) override;

 private:
  std::shared_ptr<HandlerRegistry> handler_registry_;
  std::shared_ptr<persistence::IJobRepository> job_repository_;
  std::shared_ptr<IExecutionManager> execution_manager_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::chrono::milliseconds execution_timeout_;
};

}  // namespace flowforge::engine
