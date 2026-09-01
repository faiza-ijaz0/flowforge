#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"

namespace flowforge::engine {

/// Everything an `IJobHandler` is allowed to see about the outside world
/// while it runs. Deliberately narrow: no database connection, no HTTP
/// request/response object, no direct access to `JobService` or any
/// repository (see job_handler.hpp's class comment and
/// docs/architecture/execution-model.md, "Security"). A handler that
/// needs infrastructure access beyond logging/metrics/cancellation is a
/// signal that the capability belongs here as an explicit, reviewed
/// addition -- not that the handler should reach around this type.
///
/// `metrics` is nullable (tests/simple callers can omit it); `logger` is
/// not -- every handler invocation is expected to be attributable in
/// logs. `attempt_id`/`worker_id` are forward-looking fields: nothing in
/// this phase persists a `job_attempts` row or runs handlers on a real
/// worker pool, but shaping the context now means the future
/// Scheduler/Executor/WorkerPool (Phase 2B-2) can populate them without
/// changing `IJobHandler`'s signature.
///
/// Cancellation is cooperative, not preemptive: `cancelled` is a shared
/// flag (not a value) so the component that invoked a handler can
/// request cancellation of a handler already running on another thread.
/// A handler is not required to poll `is_cancelled()` -- it is
/// best-effort, and a handler that ignores it simply runs to completion.
class ExecutionContext {
 public:
  ExecutionContext(infra::JobId job_id, infra::ExecutionId attempt_id, std::string worker_id,
                   std::shared_ptr<infra::Logger> logger,
                   std::shared_ptr<infra::MetricsRegistry> metrics = nullptr,
                   std::shared_ptr<std::atomic<bool>> cancelled = nullptr)
      : job_id_(std::move(job_id)),
        attempt_id_(std::move(attempt_id)),
        worker_id_(std::move(worker_id)),
        logger_(std::move(logger)),
        metrics_(std::move(metrics)),
        cancelled_(cancelled ? std::move(cancelled) : std::make_shared<std::atomic<bool>>(false)) {}

  [[nodiscard]] const infra::JobId& job_id() const noexcept { return job_id_; }
  [[nodiscard]] const infra::ExecutionId& attempt_id() const noexcept { return attempt_id_; }
  [[nodiscard]] const std::string& worker_id() const noexcept { return worker_id_; }
  [[nodiscard]] infra::Logger& logger() const noexcept { return *logger_; }
  [[nodiscard]] infra::MetricsRegistry* metrics() const noexcept { return metrics_.get(); }

  [[nodiscard]] bool is_cancelled() const noexcept { return cancelled_->load(std::memory_order_relaxed); }
  /// Requests cooperative cancellation. Safe to call from a different
  /// thread than the one running `IJobHandler::execute()`.
  void request_cancellation() const noexcept { cancelled_->store(true, std::memory_order_relaxed); }

 private:
  infra::JobId job_id_;
  infra::ExecutionId attempt_id_;
  std::string worker_id_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<std::atomic<bool>> cancelled_;
};

}  // namespace flowforge::engine
