#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

#include "flowforge/engine/scheduler.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Typed retry-dispatcher settings, sourced from `infra::AppConfig`
/// (`retry_poll_interval_ms`/`retry_batch_size` -- see
/// docs/architecture/execution-model.md, "Retry engine").
struct RetryDispatcherConfig {
  std::chrono::milliseconds poll_interval{500};
  std::size_t batch_size = 50;
};

/// Phase 2B-4's retry engine: the component that actually acts on a job
/// `domain::Job::record_attempt_failure()` already left in `JobStatus::
/// Retrying` (see job.hpp -- that method has existed since Phase 1/2A but
/// nothing called it until Phase 2B-4's `JobExecutor` change, and nothing
/// ever re-dispatched a `Retrying` job until this class).
///
/// Deliberately a *poll-based*, persisted-state-driven design rather than
/// an in-memory timer per retry: a `Retrying` job's eligibility
/// (`updated_at() + retry_policy().compute_backoff(attempt_count())`) is
/// fully computable from already-persisted fields, so a background thread
/// can simply re-scan `IJobRepository::list_by_status(Retrying, ...)` on
/// an interval and re-submit whatever has become eligible. This is
/// restart-safe by construction -- unlike a sleeping-thread-per-retry
/// design, a `flowforge_server` restart does not lose a pending retry,
/// since the next process's own `RetryDispatcher` picks the same
/// `Retrying` row back up from PostgreSQL on its very first poll tick.
///
/// Ordering mirrors the existing `PriorityScheduler` dispatch convention
/// (see `apps/server/src/http/routes/job_routes.cpp`): the job is handed
/// to `IScheduler::schedule()` *before* its `Queued` transition is
/// persisted, so a `schedule()` rejection (scheduler at capacity, not
/// running) never leaves a job stuck claiming a state it never actually
/// reached -- it simply stays `Retrying` and is retried again on a later
/// poll tick, which doubles as automatic recovery from transient
/// scheduler backpressure.
///
/// Concurrency: exactly one poll tick runs at a time (`poll_loop()` never
/// starts tick N+1 before tick N's whole batch finishes), so this class by
/// itself cannot double-schedule the same job. The one external race it
/// must guard against -- a `JobService::cancel_job()` call landing on the
/// same job between this class reading it (via `list_by_status`) and
/// acting on it -- is closed the same way `JobExecutor` closes its own
/// analogous races (see execution-model.md §14.3): re-fetch the job's
/// current persisted state immediately before scheduling it, and skip it
/// if it is no longer `Retrying`.
///
/// Not responsible for: HTTP, deciding *whether* a failure is retryable
/// (that is `JobExecutor`'s -- via `domain::ExecutionResult::retryable()`
/// -- and `RetryPolicy::exhausted()`'s job, both upstream of this class),
/// or executing any handler.
class RetryDispatcher {
 public:
  RetryDispatcher(std::shared_ptr<persistence::IJobRepository> job_repository,
                  std::shared_ptr<IScheduler> scheduler, std::shared_ptr<infra::Clock> clock,
                  RetryDispatcherConfig config, std::shared_ptr<infra::Logger> logger,
                  std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);
  ~RetryDispatcher();
  RetryDispatcher(const RetryDispatcher&) = delete;
  RetryDispatcher& operator=(const RetryDispatcher&) = delete;
  RetryDispatcher(RetryDispatcher&&) = delete;
  RetryDispatcher& operator=(RetryDispatcher&&) = delete;

  /// Starts the background poll thread. Fails with `ErrorCode::Conflict`
  /// if already running, or if this instance was previously stopped
  /// (mirrors `PriorityScheduler`/`LocalWorkerPool`'s lifecycle contract:
  /// construct a new instance instead of restarting).
  Result<void> start();

  /// Signals the poll thread to stop (interrupting any in-progress sleep
  /// immediately -- never waits out a full poll interval) and joins it.
  /// A poll tick already in flight finishes its current batch before
  /// observing the stop signal, exactly like `PriorityScheduler::stop()`
  /// draining its queue before joining -- never interrupted mid-job.
  /// Fails with `ErrorCode::Conflict` if already stopped.
  Result<void> stop();

  [[nodiscard]] bool is_running() const;

  /// Runs exactly one poll tick synchronously: scans up to
  /// `RetryDispatcherConfig::batch_size` `Retrying` jobs, re-submits every
  /// one whose backoff has elapsed and that is still `Retrying` at
  /// submission time, and persists its transition to `Queued` only after
  /// `IScheduler::schedule()` accepts it. Returns the number of jobs
  /// successfully re-submitted. Public (not just invoked by the
  /// background thread) so tests can drive retry timing deterministically
  /// via an injected `infra::ManualClock` instead of sleeping in real
  /// time -- `start()`/`stop()` only wrap this in a sleep loop.
  [[nodiscard]] Result<std::size_t> poll_once();

 private:
  void poll_loop();

  std::shared_ptr<persistence::IJobRepository> job_repository_;
  std::shared_ptr<IScheduler> scheduler_;
  std::shared_ptr<infra::Clock> clock_;
  RetryDispatcherConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;

  mutable std::mutex state_mutex_;
  std::condition_variable wakeup_cv_;
  bool running_ = false;
  bool ever_stopped_ = false;
  bool stop_requested_ = false;
  std::thread poll_thread_;
};

}  // namespace flowforge::engine
