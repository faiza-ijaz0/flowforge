#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "flowforge/domain/job.hpp"
#include "flowforge/engine/handler_registry.hpp"
#include "flowforge/engine/priority_blocking_queue.hpp"
#include "flowforge/engine/scheduler.hpp"
#include "flowforge/engine/thread_pool.hpp"
#include "flowforge/engine/worker_pool.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

enum class SchedulerState : std::uint8_t { Stopped, Running, Stopping };

[[nodiscard]] std::string_view to_string(SchedulerState state) noexcept;

/// Typed scheduler settings, sourced from `infra::AppConfig`
/// (`scheduler_queue_capacity`/`scheduler_dispatch_workers`) -- see
/// docs/architecture/execution-model.md, "Scheduler configuration".
struct SchedulerConfig {
  std::size_t queue_capacity = 1024;
  std::size_t dispatch_worker_count = 2;
};

/// Real `IScheduler` implementation: a bounded, priority-ordered,
/// in-memory dispatch queue (`PriorityBlockingQueue`) drained by a small
/// pool of dispatch threads (`ThreadPool`) that resolve each job's
/// handler via `HandlerRegistry` and then stop -- see `dispatch_loop()`'s
/// definition for exactly where this phase's responsibility ends and the
/// future Executor's (Phase 2B-3) begins.
///
/// Deliberately NOT responsible for: HTTP (`apps/server` calls
/// `schedule()`), PostgreSQL (never touches a repository -- see
/// docs/architecture/execution-model.md, "Scheduler and persistence"),
/// concrete handler implementations (only ever calls through
/// `HandlerRegistry`/`IJobHandler`'s interface), worker management
/// (`IWorkerPool` is a distinct future component), retry policy
/// execution, or executing any handler.
///
/// Lifecycle: `Stopped -> Running` (`start()`) `-> Stopping -> Stopped`
/// (`stop()`). `stop()` is terminal -- the internal queue cannot be
/// reopened, so `start()` after a `stop()` fails with
/// `ErrorCode::Conflict` rather than silently no-op'ing; construct a new
/// instance instead. The destructor stops a still-running scheduler
/// automatically (mirrors `ThreadPool`'s RAII shutdown) but, like
/// `ThreadPool`, is not safe to run concurrently with an in-flight
/// `stop()` call from another thread -- that is caller misuse, not a
/// case this class defends against.
class PriorityScheduler final : public IScheduler {
 public:
  /// `worker_pool` is optional (default `nullptr`) so existing tests that
  /// only care about queueing/priority/backpressure behavior don't need a
  /// full `IWorkerPool`. When null, the dispatch loop resolves the
  /// handler (proving the job is routable) and stops there -- the
  /// Phase 2B-2 behavior -- instead of calling `dispatch()`.
  PriorityScheduler(std::shared_ptr<HandlerRegistry> handler_registry, SchedulerConfig config,
                    std::shared_ptr<infra::Logger> logger,
                    std::shared_ptr<infra::MetricsRegistry> metrics = nullptr,
                    std::shared_ptr<IWorkerPool> worker_pool = nullptr);
  ~PriorityScheduler() override;
  PriorityScheduler(const PriorityScheduler&) = delete;
  PriorityScheduler& operator=(const PriorityScheduler&) = delete;
  PriorityScheduler(PriorityScheduler&&) = delete;
  PriorityScheduler& operator=(PriorityScheduler&&) = delete;

  /// Starts the dispatch thread(s). Fails with `ErrorCode::Conflict` if
  /// already running/stopping, or if this instance was previously
  /// stopped.
  Result<void> start();

  /// Stops accepting dispatch of new work and joins dispatch threads
  /// after draining whatever was already queued (mirrors
  /// `ThreadPool::stop()`'s "drain, then join" semantics -- already-
  /// queued jobs still get their handler resolved and logged/metriced as
  /// dispatched before shutdown completes). Fails with
  /// `ErrorCode::Conflict` if already stopped.
  Result<void> stop();

  [[nodiscard]] SchedulerState state() const;

  /// Current number of jobs sitting in the internal priority queue,
  /// waiting to be dispatched. Not persisted -- an in-memory snapshot.
  [[nodiscard]] std::size_t queue_depth() const;

  /// Validates and enqueues `job`. Rejects (without enqueueing) a job
  /// that: is already terminal, has an empty `job_type()`, or has a
  /// `job_type()` with no registered handler (`ErrorCode::NotFound`).
  /// Rejects with `ErrorCode::Conflict` if the scheduler is not running
  /// or the queue is at capacity. Never executes the resolved handler --
  /// resolution here only proves the job is routable.
  Result<void> schedule(const domain::Job& job) override;

  /// Best-effort: succeeds and prevents dispatch only if `job_id` is
  /// currently sitting in the queue (not yet dispatched). Returns
  /// `ErrorCode::NotFound` otherwise (already dispatched, or never
  /// scheduled here) -- this is scheduler-local bookkeeping, independent
  /// of `services::JobService::cancel_job`'s persisted-status
  /// cancellation.
  Result<void> cancel(const infra::JobId& job_id) override;

 private:
  struct ScheduledJob {
    domain::Job job;
    std::uint64_t sequence;
  };

  /// Higher `job.priority()` dispatches first; equal priority preserves
  /// FIFO order via `sequence` (assigned by an atomic counter at
  /// `schedule()` time, not construction time, so it reflects admission
  /// order under concurrent submission).
  struct ScheduledJobOrder {
    [[nodiscard]] bool operator()(const ScheduledJob& a, const ScheduledJob& b) const noexcept {
      if (a.job.priority() != b.job.priority()) {
        return a.job.priority() < b.job.priority();
      }
      return a.sequence > b.sequence;
    }
  };

  void dispatch_loop();
  void shutdown_impl();

  std::shared_ptr<HandlerRegistry> handler_registry_;
  SchedulerConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<IWorkerPool> worker_pool_;

  PriorityBlockingQueue<ScheduledJob, ScheduledJobOrder> queue_;
  std::atomic<std::uint64_t> sequence_{0};

  mutable std::mutex state_mutex_;
  SchedulerState state_ = SchedulerState::Stopped;
  bool ever_stopped_ = false;
  std::unique_ptr<ThreadPool> dispatch_pool_;

  mutable std::mutex pending_mutex_;
  std::unordered_set<std::string> pending_ids_;
  std::unordered_set<std::string> cancelled_ids_;
};

}  // namespace flowforge::engine
