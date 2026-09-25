#include "flowforge/engine/retry_dispatcher.hpp"

#include <tuple>
#include <utility>

namespace flowforge::engine {

namespace {
constexpr std::string_view kComponent = "retry_dispatcher";
}  // namespace

RetryDispatcher::RetryDispatcher(std::shared_ptr<persistence::IJobRepository> job_repository,
                                 std::shared_ptr<IScheduler> scheduler, std::shared_ptr<infra::Clock> clock,
                                 RetryDispatcherConfig config, std::shared_ptr<infra::Logger> logger,
                                 std::shared_ptr<infra::MetricsRegistry> metrics)
    : job_repository_(std::move(job_repository)),
      scheduler_(std::move(scheduler)),
      clock_(std::move(clock)),
      config_(config),
      logger_(std::move(logger)),
      metrics_(std::move(metrics)) {}

RetryDispatcher::~RetryDispatcher() {
  std::unique_lock lock(state_mutex_);
  if (running_) {
    stop_requested_ = true;
    lock.unlock();
    wakeup_cv_.notify_all();
    if (poll_thread_.joinable()) {
      poll_thread_.join();
    }
  }
}

Result<void> RetryDispatcher::start() {
  std::lock_guard lock(state_mutex_);
  if (running_) {
    return std::unexpected(make_error(ErrorCode::Conflict, "retry dispatcher is already running"));
  }
  if (ever_stopped_) {
    return std::unexpected(make_error(ErrorCode::Conflict,
                                      "retry dispatcher was previously stopped and cannot be restarted -- "
                                      "construct a new instance"));
  }
  stop_requested_ = false;
  running_ = true;
  poll_thread_ = std::thread([this] { poll_loop(); });
  logger_->info(kComponent, "retry dispatcher started",
                {{.key = "poll_interval_ms", .value = std::to_string(config_.poll_interval.count())},
                 {.key = "batch_size", .value = std::to_string(config_.batch_size)}});
  return {};
}

Result<void> RetryDispatcher::stop() {
  {
    std::lock_guard lock(state_mutex_);
    if (!running_) {
      return std::unexpected(make_error(ErrorCode::Conflict, "retry dispatcher is already stopped"));
    }
    stop_requested_ = true;
  }
  wakeup_cv_.notify_all();
  if (poll_thread_.joinable()) {
    poll_thread_.join();
  }
  {
    std::lock_guard lock(state_mutex_);
    running_ = false;
    ever_stopped_ = true;
  }
  logger_->info(kComponent, "retry dispatcher stopped", {});
  return {};
}

bool RetryDispatcher::is_running() const {
  std::lock_guard lock(state_mutex_);
  return running_;
}

void RetryDispatcher::poll_loop() {
  std::unique_lock lock(state_mutex_);
  while (!stop_requested_) {
    lock.unlock();
    std::ignore = poll_once();
    lock.lock();
    wakeup_cv_.wait_for(lock, config_.poll_interval, [this] { return stop_requested_; });
  }
}

Result<std::size_t> RetryDispatcher::poll_once() {
  auto retrying = job_repository_->list_by_status(domain::JobStatus::Retrying, config_.batch_size);
  if (!retrying) {
    logger_->error(kComponent, "failed to list retrying jobs",
                   {{.key = "error", .value = retrying.error().message()}});
    return std::unexpected(retrying.error());
  }

  std::size_t retried_count = 0;
  const auto now = clock_->now();

  if (metrics_ && !retrying->empty()) {
    // "Candidates" (Phase 2B-5): how many Retrying jobs this tick saw at
    // all, distinct from how many were actually eligible/resubmitted --
    // the gap between the two reveals how much inventory is sitting in
    // backoff at any given moment.
    metrics_->increment_counter("flowforge_retry_dispatcher_candidates_total",
                                static_cast<std::int64_t>(retrying->size()));
  }

  for (const auto& candidate : *retrying) {
    const auto backoff = candidate.retry_policy().compute_backoff(candidate.attempt_count());
    if (now < candidate.updated_at() + backoff) {
      continue;  // Not yet eligible -- a later poll tick will pick it up.
    }

    // Re-fetch immediately before acting: closes the race window against a
    // concurrent JobService::cancel_job() (or any other racing transition)
    // that landed after list_by_status() read this row -- see class
    // comment. list_by_status() itself may already be milliseconds stale
    // by the time this loop reaches this candidate.
    auto fresh = job_repository_->find_by_id(candidate.id());
    if (!fresh || fresh->status() != domain::JobStatus::Retrying) {
      continue;  // Gone, or raced to a different status (e.g. Cancelled).
    }

    // Persist Retrying -> Queued *before* scheduling: once schedule()
    // accepts the job a worker may finish the retry immediately, and a
    // Queued write landing afterwards would overwrite that outcome
    // (see JobService::mark_queued). On rejection the original Retrying
    // row -- including its updated_at, so the backoff is unchanged -- is
    // written back.
    domain::Job queued = *fresh;
    queued.transition_to(domain::JobStatus::Queued, now);
    if (auto updated = job_repository_->update(queued); !updated) {
      logger_->error(kComponent, "failed to persist Retrying -> Queued transition before scheduling",
                     {{.key = "job_id", .value = queued.id().value()},
                      {.key = "error", .value = updated.error().message()}});
      continue;
    }

    auto scheduled = scheduler_->schedule(queued);
    if (!scheduled) {
      std::ignore = job_repository_->update(*fresh);
      // Scheduler at capacity / not running / handler no longer resolvable
      // -- leave the job Retrying so a later poll tick retries the
      // dispatch itself; nothing is lost. Mirrors the existing, documented
      // "dispatch failure" limitation for a job's very first schedule()
      // call (see execution-model.md §10.6), except here it self-heals.
      logger_->error(kComponent, "retry dispatch deferred: scheduler rejected job",
                     {{.key = "job_id", .value = fresh->id().value()},
                      {.key = "error", .value = scheduled.error().message()}});
      if (metrics_) {
        metrics_->increment_counter("flowforge_retry_dispatcher_deferred_total");
      }
      continue;
    }

    ++retried_count;
    logger_->info(kComponent, "job re-submitted for retry",
                  {{.key = "job_id", .value = queued.id().value()},
                   {.key = "attempt_count", .value = std::to_string(queued.attempt_count())}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_retry_dispatcher_jobs_retried_total");
    }
  }

  if (metrics_) {
    metrics_->increment_counter("flowforge_retry_dispatcher_poll_ticks_total");
  }
  return retried_count;
}

}  // namespace flowforge::engine
