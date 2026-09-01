#include "flowforge/engine/job_executor.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

namespace flowforge::engine {

namespace {
constexpr std::string_view kComponent = "executor";
}  // namespace

JobExecutor::JobExecutor(std::shared_ptr<HandlerRegistry> handler_registry,
                         std::shared_ptr<persistence::IJobRepository> job_repository,
                         std::shared_ptr<IExecutionManager> execution_manager,
                         std::shared_ptr<infra::Clock> clock, std::shared_ptr<infra::Logger> logger,
                         std::shared_ptr<infra::MetricsRegistry> metrics,
                         std::chrono::milliseconds execution_timeout)
    : handler_registry_(std::move(handler_registry)),
      job_repository_(std::move(job_repository)),
      execution_manager_(std::move(execution_manager)),
      clock_(std::move(clock)),
      logger_(std::move(logger)),
      metrics_(std::move(metrics)),
      execution_timeout_(execution_timeout) {}

Result<domain::Execution> JobExecutor::execute(const domain::Job& job, const infra::WorkerId& worker_id,
                                               std::shared_ptr<std::atomic<bool>> cancellation_flag) {
  const std::uint32_t attempt_number = job.attempt_count() + 1;
  const infra::ExecutionId attempt_id = infra::ExecutionId::generate();

  auto current = job_repository_->find_by_id(job.id());
  if (!current) {
    return std::unexpected(current.error());
  }
  if (domain::is_terminal(current->status())) {
    logger_->info(kComponent, "skipping execution: job already in a terminal state",
                  {{.key = "job_id", .value = job.id().value()},
                   {.key = "status", .value = std::string(domain::to_string(current->status()))}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_skipped_terminal_total");
    }
    return std::unexpected(make_error(
        ErrorCode::Conflict, "job '" + job.id().value() + "' is already in a terminal state; not executing"));
  }

  const auto start_time = clock_->now();
  // Monotonic wall-clock-independent timer for the execution-duration
  // metric/log (Phase 2B-5) -- `start_time`/`finish_time` below stay on
  // `clock_` (system_clock in production) because they are persisted as
  // calendar timestamps (`domain::Execution::started_at`/`finished_at`,
  // `Job::updated_at`) where a real date/time is what's wanted. Duration
  // *measurement* must never be derived from those: system_clock is not
  // guaranteed monotonic (an NTP step or manual clock adjustment could
  // make `finish_time - start_time` negative or wrong), so a separate
  // `steady_clock` reading is taken purely to measure elapsed time.
  const auto monotonic_start = std::chrono::steady_clock::now();
  domain::Job running_job = *current;
  running_job.transition_to(domain::JobStatus::Running, start_time);
  if (auto updated = job_repository_->update(running_job); !updated) {
    return std::unexpected(updated.error());
  }
  if (metrics_) {
    metrics_->increment_counter("flowforge_executor_jobs_started_total");
  }
  logger_->info(kComponent, "job started",
                {{.key = "job_id", .value = job.id().value()},
                 {.key = "worker_id", .value = worker_id.value()},
                 {.key = "job_type", .value = job.job_type()},
                 {.key = "attempt_number", .value = std::to_string(attempt_number)}});

  auto handler = handler_registry_->resolve(job.job_type());
  if (!handler) {
    const auto now = clock_->now();
    const auto resolution_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - monotonic_start);
    const std::string message = "no handler registered for job type '" + job.job_type() + "'";
    domain::Job failed_job = running_job;
    failed_job.record_execution_failure(message, now);
    std::ignore = job_repository_->update(failed_job);

    domain::Execution execution{.id = attempt_id,
                                .job_id = job.id(),
                                .worker_id = worker_id,
                                .attempt_number = attempt_number,
                                .outcome = domain::ExecutionOutcome::Failed,
                                .started_at = start_time,
                                .finished_at = now,
                                .error_message = message};
    std::ignore = execution_manager_->record(execution);

    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_failures_total");
      metrics_->increment_counter("flowforge_executor_jobs_failed_total");
      metrics_->observe_histogram("flowforge_executor_execution_duration_ms",
                                  static_cast<double>(resolution_duration.count()));
    }
    logger_->error(kComponent, "handler resolution failed at execution time",
                   {{.key = "job_id", .value = job.id().value()},
                    {.key = "job_type", .value = job.job_type()},
                    {.key = "attempt_number", .value = std::to_string(attempt_number)},
                    {.key = "worker_id", .value = worker_id.value()}});
    return execution;
  }

  ExecutionContext context(job.id(), attempt_id, worker_id.value(), logger_, metrics_, cancellation_flag);

  std::atomic<bool> handler_done{false};
  std::atomic<bool> timed_out{false};
  std::mutex done_mutex;
  std::condition_variable done_cv;

  // Cooperative timeout: never a forced thread kill. If the handler
  // hasn't signaled `handler_done` within `execution_timeout_`, this
  // watcher sets the shared cancellation flag -- the same channel an
  // external IWorkerPool::request_cancellation() call would use -- and
  // records that *this* attempt's failure (if any) was a timeout rather
  // than an external cancellation. A handler that ignores the flag simply
  // keeps running; the watcher has already returned/joined by then.
  std::thread watcher([&] {
    std::unique_lock lock(done_mutex);
    const bool finished_in_time =
        done_cv.wait_for(lock, execution_timeout_, [&] { return handler_done.load(); });
    if (!finished_in_time) {
      timed_out.store(true, std::memory_order_relaxed);
      cancellation_flag->store(true, std::memory_order_relaxed);
    }
  });

  // Handlers are expected to report failure via Result/ExecutionResult,
  // never an exception (see IJobHandler's contract) -- but a bug in a
  // future handler throwing must not permanently kill this worker's
  // dispatch loop, so an unexpected exception is converted to a normal
  // failure result here rather than left to propagate.
  Result<domain::ExecutionResult> handler_result = [&]() -> Result<domain::ExecutionResult> {
    try {
      return (*handler)->execute(context, job.payload());
    } catch (const std::exception& e) {
      return std::unexpected(
          make_error(ErrorCode::Internal, std::string("handler threw an exception: ") + e.what()));
    } catch (...) {
      return std::unexpected(make_error(ErrorCode::Internal, "handler threw a non-standard exception"));
    }
  }();

  {
    std::lock_guard lock(done_mutex);
    handler_done.store(true, std::memory_order_relaxed);
  }
  done_cv.notify_all();
  watcher.join();

  const auto finish_time = clock_->now();
  const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - monotonic_start);

  // Guard against a race with a concurrent cancel request: re-check
  // persisted state before deciding the final job status. A cancel that
  // landed while the handler was running must win -- the job must never
  // be flipped to Succeeded/Failed after being cancelled.
  auto latest = job_repository_->find_by_id(job.id());
  const bool already_cancelled = latest.has_value() && latest->status() == domain::JobStatus::Cancelled;
  domain::Job final_job = latest.has_value() ? *latest : running_job;

  domain::ExecutionOutcome outcome{};
  std::optional<std::string> error_message;

  if (already_cancelled) {
    outcome = domain::ExecutionOutcome::Cancelled;
    error_message = "job was cancelled during execution";
    logger_->info(kComponent, "job was cancelled during execution; not overwriting status",
                  {{.key = "job_id", .value = job.id().value()},
                   {.key = "worker_id", .value = worker_id.value()},
                   {.key = "attempt_number", .value = std::to_string(attempt_number)}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_jobs_cancelled_total");
    }
  } else if (handler_result.has_value() && handler_result->succeeded()) {
    outcome = domain::ExecutionOutcome::Succeeded;
    final_job.record_attempt_success(finish_time);
    std::ignore = job_repository_->update(final_job);
    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_jobs_succeeded_total");
    }
    logger_->info(kComponent, "job succeeded",
                  {{.key = "job_id", .value = job.id().value()},
                   {.key = "worker_id", .value = worker_id.value()},
                   {.key = "attempt_number", .value = std::to_string(attempt_number)},
                   {.key = "duration_ms", .value = std::to_string(duration.count())}});
  } else if (timed_out.load()) {
    // A timeout is system-initiated (nobody asked to cancel this job) --
    // lands on Failed, distinct from an explicit external cancellation
    // below. Tracked as its own counter, not folded into the generic
    // "jobs failed" one (Phase 2B-5): an operator investigating timeouts
    // needs a signal that isn't diluted by ordinary handler failures.
    outcome = domain::ExecutionOutcome::TimedOut;
    error_message = "execution timed out after " + std::to_string(execution_timeout_.count()) + "ms";
    final_job.record_execution_failure(*error_message, finish_time);
    std::ignore = job_repository_->update(final_job);
    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_jobs_timed_out_total");
    }
    logger_->error(kComponent, "job timed out",
                   {{.key = "job_id", .value = job.id().value()},
                    {.key = "worker_id", .value = worker_id.value()},
                    {.key = "attempt_number", .value = std::to_string(attempt_number)},
                    {.key = "duration_ms", .value = std::to_string(duration.count())}});
  } else if (cancellation_flag->load()) {
    // The shared cancellation flag was set by something other than our
    // own timeout watcher -- i.e. an external
    // IWorkerPool::request_cancellation() call. Mirrors
    // JobService::cancel_job()'s own Cancelled transition (plain
    // transition_to(), no attempt_count bump, no last_error) for
    // consistency regardless of whether the cancellation landed while
    // the job was still Queued or while it was Running.
    outcome = domain::ExecutionOutcome::Cancelled;
    error_message = handler_result.has_value()
                        ? handler_result->error_message().value_or("execution cancelled")
                        : handler_result.error().message();
    final_job.transition_to(domain::JobStatus::Cancelled, finish_time);
    std::ignore = job_repository_->update(final_job);
    if (metrics_) {
      metrics_->increment_counter("flowforge_executor_jobs_cancelled_total");
    }
    logger_->info(kComponent, "job cancelled during execution",
                  {{.key = "job_id", .value = job.id().value()},
                   {.key = "worker_id", .value = worker_id.value()},
                   {.key = "attempt_number", .value = std::to_string(attempt_number)}});
  } else {
    outcome = domain::ExecutionOutcome::Failed;
    error_message = handler_result.has_value() ? handler_result->error_message().value_or("execution failed")
                                               : handler_result.error().message();
    // Phase 2B-4 retry engine: a handler-declared failure is only ever a
    // candidate for retry if the handler itself said so
    // (ExecutionResult::retryable() -- the existing hint from Phase 2B-1,
    // never consumed until now). A handler.execute() call that returned a
    // Result error instead of an ExecutionResult (handler_result has no
    // value -- an unexpected exception, per the class comment above) has
    // no retryable() to consult and is treated as non-retryable, the safe
    // default. record_attempt_failure() lands on Retrying/DeadLetter per
    // retry_policy (existing domain logic, untouched); record_execution_
    // failure() goes straight to Failed, exactly as before this phase for
    // every failure that isn't retryable.
    const bool retryable = handler_result.has_value() && handler_result->retryable();
    if (retryable) {
      final_job.record_attempt_failure(*error_message, finish_time);
    } else {
      final_job.record_execution_failure(*error_message, finish_time);
    }
    std::ignore = job_repository_->update(final_job);
    if (metrics_) {
      if (retryable) {
        // Phase 2B-5: distinguish "this attempt failed but will retry" /
        // "this attempt failed and the job is now dead-lettered" from a
        // genuinely permanent failure -- folding all three into one
        // counter (as before Phase 2B-5) hid exactly the signal an
        // operator watching the retry engine needs.
        metrics_->increment_counter("flowforge_executor_retryable_failures_total");
        if (final_job.status() == domain::JobStatus::DeadLetter) {
          metrics_->increment_counter("flowforge_executor_jobs_dead_letter_total");
        } else {
          metrics_->increment_counter("flowforge_executor_jobs_retrying_total");
        }
      } else {
        metrics_->increment_counter("flowforge_executor_jobs_failed_total");
      }
    }
    logger_->info(kComponent, retryable ? "job attempt failed; eligible for retry" : "job failed permanently",
                  {{.key = "job_id", .value = job.id().value()},
                   {.key = "worker_id", .value = worker_id.value()},
                   {.key = "attempt_number", .value = std::to_string(attempt_number)},
                   {.key = "duration_ms", .value = std::to_string(duration.count())},
                   {.key = "error", .value = *error_message},
                   {.key = "resulting_status", .value = std::string(domain::to_string(final_job.status()))}});
  }

  if (metrics_) {
    metrics_->observe_histogram("flowforge_executor_execution_duration_ms",
                                static_cast<double>(duration.count()));
  }

  domain::Execution execution{.id = attempt_id,
                              .job_id = job.id(),
                              .worker_id = worker_id,
                              .attempt_number = attempt_number,
                              .outcome = outcome,
                              .started_at = start_time,
                              .finished_at = finish_time,
                              .error_message = error_message};
  if (auto recorded = execution_manager_->record(execution); !recorded) {
    logger_->error(kComponent, "failed to persist execution attempt",
                   {{.key = "job_id", .value = job.id().value()},
                    {.key = "error", .value = recorded.error().message()}});
  }

  return execution;
}

}  // namespace flowforge::engine
