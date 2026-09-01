#include "flowforge/engine/priority_scheduler.hpp"

#include <tuple>
#include <utility>

namespace flowforge::engine {

std::string_view to_string(SchedulerState state) noexcept {
  switch (state) {
    case SchedulerState::Stopped:
      return "stopped";
    case SchedulerState::Running:
      return "running";
    case SchedulerState::Stopping:
      return "stopping";
  }
  return "unknown";
}

PriorityScheduler::PriorityScheduler(std::shared_ptr<HandlerRegistry> handler_registry,
                                     SchedulerConfig config, std::shared_ptr<infra::Logger> logger,
                                     std::shared_ptr<infra::MetricsRegistry> metrics,
                                     std::shared_ptr<IWorkerPool> worker_pool)
    : handler_registry_(std::move(handler_registry)),
      config_(config),
      logger_(std::move(logger)),
      metrics_(std::move(metrics)),
      worker_pool_(std::move(worker_pool)),
      queue_(config_.queue_capacity) {}

PriorityScheduler::~PriorityScheduler() {
  bool needs_shutdown = false;
  {
    std::lock_guard lock(state_mutex_);
    needs_shutdown = (state_ == SchedulerState::Running);
    if (needs_shutdown) {
      state_ = SchedulerState::Stopping;
    }
  }
  if (needs_shutdown) {
    shutdown_impl();
    std::lock_guard lock(state_mutex_);
    state_ = SchedulerState::Stopped;
  }
}

void PriorityScheduler::shutdown_impl() {
  queue_.close();
  if (dispatch_pool_) {
    dispatch_pool_->stop();
    dispatch_pool_.reset();
  }
}

Result<void> PriorityScheduler::start() {
  std::lock_guard lock(state_mutex_);
  if (state_ != SchedulerState::Stopped) {
    return std::unexpected(make_error(ErrorCode::Conflict, "scheduler is already running or stopping"));
  }
  if (ever_stopped_) {
    return std::unexpected(make_error(ErrorCode::Conflict,
                                      "scheduler was previously stopped and cannot be restarted -- "
                                      "construct a new instance"));
  }

  dispatch_pool_ = std::make_unique<ThreadPool>(config_.dispatch_worker_count);
  for (std::size_t i = 0; i < config_.dispatch_worker_count; ++i) {
    std::ignore = dispatch_pool_->submit([this] { dispatch_loop(); });
  }
  state_ = SchedulerState::Running;
  logger_->info("scheduler", "scheduler started",
                {{.key = "dispatch_workers", .value = std::to_string(config_.dispatch_worker_count)},
                 {.key = "queue_capacity", .value = std::to_string(config_.queue_capacity)}});
  return {};
}

Result<void> PriorityScheduler::stop() {
  {
    std::lock_guard lock(state_mutex_);
    if (state_ == SchedulerState::Stopped) {
      return std::unexpected(make_error(ErrorCode::Conflict, "scheduler is already stopped"));
    }
    state_ = SchedulerState::Stopping;
  }

  shutdown_impl();

  {
    std::lock_guard lock(state_mutex_);
    state_ = SchedulerState::Stopped;
    ever_stopped_ = true;
  }
  logger_->info("scheduler", "scheduler stopped", {});
  return {};
}

SchedulerState PriorityScheduler::state() const {
  std::lock_guard lock(state_mutex_);
  return state_;
}

std::size_t PriorityScheduler::queue_depth() const {
  return queue_.size();
}

Result<void> PriorityScheduler::schedule(const domain::Job& job) {
  {
    std::lock_guard lock(state_mutex_);
    if (state_ != SchedulerState::Running) {
      return std::unexpected(make_error(ErrorCode::Conflict, "scheduler is not running"));
    }
  }

  if (domain::is_terminal(job.status())) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "job '" + job.id().value() + "' is already in a terminal state"));
  }
  if (job.job_type().empty()) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "job '" + job.id().value() + "' has no job_type; cannot be scheduled for execution"));
  }

  auto handler = handler_registry_->resolve(job.job_type());
  if (!handler) {
    if (metrics_) {
      metrics_->increment_counter("flowforge_scheduler_rejections_total");
    }
    return std::unexpected(handler.error());
  }

  const auto sequence = sequence_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard lock(pending_mutex_);
    pending_ids_.insert(job.id().value());
  }

  if (!queue_.try_push(ScheduledJob{.job = job, .sequence = sequence})) {
    {
      std::lock_guard lock(pending_mutex_);
      pending_ids_.erase(job.id().value());
    }
    if (metrics_) {
      metrics_->increment_counter("flowforge_scheduler_rejections_total");
      // Distinct from the generic rejection counter above: this one
      // specifically answers "how many jobs were rejected because of
      // backpressure" (Phase 2B-5) -- the other rejection causes above
      // (unknown job_type, terminal job, empty job_type) are caller
      // errors, not capacity signals an operator would size the queue
      // against.
      metrics_->increment_counter("flowforge_scheduler_backpressure_rejections_total");
    }
    return std::unexpected(make_error(ErrorCode::Conflict, "scheduler queue is at capacity"));
  }

  if (metrics_) {
    metrics_->increment_counter("flowforge_scheduler_jobs_scheduled_total");
    metrics_->set_gauge("flowforge_scheduler_queue_depth", static_cast<double>(queue_.size()));
  }
  logger_->info("scheduler", "job scheduled",
                {{.key = "job_id", .value = job.id().value()},
                 {.key = "job_type", .value = job.job_type()},
                 {.key = "priority", .value = std::to_string(job.priority())}});
  return {};
}

Result<void> PriorityScheduler::cancel(const infra::JobId& job_id) {
  std::lock_guard lock(pending_mutex_);
  auto it = pending_ids_.find(job_id.value());
  if (it == pending_ids_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "job '" + job_id.value() + "' is not pending in the scheduler"));
  }
  pending_ids_.erase(it);
  cancelled_ids_.insert(job_id.value());
  return {};
}

void PriorityScheduler::dispatch_loop() {
  while (auto scheduled = queue_.pop()) {
    const std::string id = scheduled->job.id().value();

    bool was_cancelled = false;
    {
      std::lock_guard lock(pending_mutex_);
      if (auto it = cancelled_ids_.find(id); it != cancelled_ids_.end()) {
        cancelled_ids_.erase(it);
        was_cancelled = true;
      } else {
        pending_ids_.erase(id);
      }
    }

    if (was_cancelled) {
      logger_->info("scheduler", "job cancelled before dispatch", {{.key = "job_id", .value = id}});
      if (metrics_) {
        metrics_->increment_counter("flowforge_scheduler_jobs_cancelled_total");
      }
      continue;
    }

    if (!worker_pool_) {
      // No WorkerPool injected (e.g. a test exercising only queueing/
      // priority/backpressure behavior) -- fall back to the Phase 2B-2
      // behavior: re-resolve the handler to prove the job is routable,
      // then stop. HandlerRegistry has no unregister operation, so a
      // resolve() failure here would mean an invariant was violated, not
      // a normal operational outcome.
      auto handler = handler_registry_->resolve(scheduled->job.job_type());
      if (!handler) {
        logger_->error(
            "scheduler", "handler vanished between schedule and dispatch",
            {{.key = "job_id", .value = id}, {.key = "job_type", .value = scheduled->job.job_type()}});
        if (metrics_) {
          metrics_->increment_counter("flowforge_scheduler_dispatch_failures_total");
        }
        continue;
      }
      logger_->info(
          "scheduler", "job dispatched (handler resolved; no WorkerPool configured)",
          {{.key = "job_id", .value = id}, {.key = "job_type", .value = scheduled->job.job_type()}});
      if (metrics_) {
        metrics_->increment_counter("flowforge_scheduler_jobs_dispatched_total");
        metrics_->set_gauge("flowforge_scheduler_queue_depth", static_cast<double>(queue_.size()));
      }
      continue;
    }

    // Phase 2B-3: hand the job off to the real WorkerPool -> Executor ->
    // HandlerRegistry -> IJobHandler path. dispatch() is expected to be
    // fast/non-blocking (it enqueues onto the pool's own bounded queue);
    // a rejection here (pool not running, or its queue is full) does not
    // lose the job record -- it stays persisted as Queued -- but this
    // dispatch attempt is not retried automatically (see
    // docs/architecture/execution-model.md, "Dispatch failure handling").
    auto dispatched = worker_pool_->dispatch(scheduled->job);
    if (!dispatched) {
      logger_->error(
          "scheduler", "worker pool rejected dispatched job",
          {{.key = "job_id", .value = id}, {.key = "error", .value = dispatched.error().message()}});
      if (metrics_) {
        metrics_->increment_counter("flowforge_scheduler_dispatch_failures_total");
      }
      continue;
    }
    logger_->info("scheduler", "job dispatched to worker pool",
                  {{.key = "job_id", .value = id}, {.key = "job_type", .value = scheduled->job.job_type()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_scheduler_jobs_dispatched_total");
      metrics_->set_gauge("flowforge_scheduler_queue_depth", static_cast<double>(queue_.size()));
    }
  }
}

}  // namespace flowforge::engine
