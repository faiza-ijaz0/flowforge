#include "flowforge/engine/local_worker_pool.hpp"

#include <chrono>
#include <tuple>
#include <utility>

namespace flowforge::engine {

namespace {
constexpr std::string_view kComponent = "worker_pool";
}  // namespace

LocalWorkerPool::LocalWorkerPool(std::shared_ptr<IExecutor> executor,
                                 std::shared_ptr<persistence::IWorkerRepository> worker_repository,
                                 WorkerPoolConfig config, std::shared_ptr<infra::Logger> logger,
                                 std::shared_ptr<infra::MetricsRegistry> metrics)
    : executor_(std::move(executor)),
      worker_repository_(std::move(worker_repository)),
      config_(config),
      logger_(std::move(logger)),
      metrics_(std::move(metrics)),
      queue_(config_.queue_capacity) {}

LocalWorkerPool::~LocalWorkerPool() {
  std::lock_guard lock(state_mutex_);
  if (running_) {
    queue_.close();
    if (thread_pool_) {
      thread_pool_->stop();
    }
    running_ = false;
  }
}

Result<void> LocalWorkerPool::start() {
  std::lock_guard lock(state_mutex_);
  if (running_) {
    return std::unexpected(make_error(ErrorCode::Conflict, "worker pool is already running"));
  }
  if (ever_stopped_) {
    return std::unexpected(make_error(ErrorCode::Conflict,
                                      "worker pool was previously stopped and cannot be restarted -- "
                                      "construct a new instance"));
  }

  const auto now = std::chrono::system_clock::now();
  worker_ids_.clear();
  for (std::size_t i = 0; i < config_.worker_count; ++i) {
    domain::Worker worker(infra::WorkerId::generate(), "worker-" + std::to_string(i + 1), now);
    if (auto inserted = worker_repository_->insert(worker); !inserted) {
      return std::unexpected(inserted.error());
    }
    worker_ids_.push_back(worker.id());
  }

  thread_pool_ = std::make_unique<ThreadPool>(config_.worker_count);
  for (const auto& worker_id : worker_ids_) {
    std::ignore = thread_pool_->submit([this, worker_id] { worker_loop(worker_id); });
  }
  running_ = true;
  if (metrics_) {
    metrics_->set_gauge("flowforge_worker_pool_active_workers", static_cast<double>(config_.worker_count));
  }
  logger_->info(kComponent, "worker pool started",
                {{.key = "worker_count", .value = std::to_string(config_.worker_count)},
                 {.key = "queue_capacity", .value = std::to_string(config_.queue_capacity)}});
  return {};
}

Result<void> LocalWorkerPool::stop() {
  {
    std::lock_guard lock(state_mutex_);
    if (!running_) {
      return std::unexpected(make_error(ErrorCode::Conflict, "worker pool is already stopped"));
    }
    running_ = false;
    ever_stopped_ = true;
  }

  queue_.close();
  thread_pool_->stop();

  const auto now = std::chrono::system_clock::now();
  for (const auto& worker_id : worker_ids_) {
    auto worker = worker_repository_->find_by_id(worker_id);
    if (!worker) {
      continue;
    }
    worker->set_status(domain::WorkerStatus::Offline);
    worker->heartbeat(now);
    std::ignore = worker_repository_->update(*worker);
  }

  if (metrics_) {
    metrics_->set_gauge("flowforge_worker_pool_active_workers", 0.0);
  }
  logger_->info(kComponent, "worker pool stopped", {});
  return {};
}

Result<void> LocalWorkerPool::dispatch(const domain::Job& job) {
  {
    std::lock_guard lock(state_mutex_);
    if (!running_) {
      return std::unexpected(make_error(ErrorCode::Conflict, "worker pool is not running"));
    }
  }

  if (!queue_.try_push(job)) {
    if (metrics_) {
      metrics_->increment_counter("flowforge_worker_pool_dispatch_failures_total");
    }
    return std::unexpected(make_error(ErrorCode::Conflict, "worker pool queue is at capacity"));
  }
  if (metrics_) {
    metrics_->increment_counter("flowforge_worker_pool_jobs_submitted_total");
    metrics_->set_gauge("flowforge_worker_pool_queue_depth", static_cast<double>(queue_.size()));
  }
  return {};
}

std::size_t LocalWorkerPool::capacity() const {
  return config_.worker_count;
}

std::size_t LocalWorkerPool::active_count() const {
  return active_count_.load(std::memory_order_relaxed);
}

bool LocalWorkerPool::is_running() const {
  std::lock_guard lock(state_mutex_);
  return running_;
}

Result<void> LocalWorkerPool::request_cancellation(const infra::JobId& job_id) {
  std::lock_guard lock(active_mutex_);
  auto it = active_cancellation_flags_.find(job_id.value());
  if (it == active_cancellation_flags_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "job '" + job_id.value() + "' is not currently executing"));
  }
  it->second->store(true, std::memory_order_relaxed);
  logger_->info(kComponent, "cancellation requested for running job",
                {{.key = "job_id", .value = job_id.value()}});
  return {};
}

void LocalWorkerPool::worker_loop(const infra::WorkerId& worker_id) {
  while (auto job = queue_.pop()) {
    const std::string job_id = job->id().value();
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    {
      std::lock_guard lock(active_mutex_);
      active_cancellation_flags_[job_id] = cancel_flag;
    }
    active_count_.fetch_add(1, std::memory_order_relaxed);
    if (metrics_) {
      metrics_->set_gauge("flowforge_worker_pool_active_jobs", static_cast<double>(active_count_.load()));
    }

    auto result = executor_->execute(*job, worker_id, cancel_flag);
    if (!result) {
      logger_->error(
          kComponent, "executor failed to run job attempt",
          {{.key = "job_id", .value = job_id}, {.key = "error", .value = result.error().message()}});
    }

    {
      std::lock_guard lock(active_mutex_);
      active_cancellation_flags_.erase(job_id);
    }
    active_count_.fetch_sub(1, std::memory_order_relaxed);
    if (metrics_) {
      metrics_->increment_counter("flowforge_worker_pool_jobs_completed_total");
      metrics_->set_gauge("flowforge_worker_pool_active_jobs", static_cast<double>(active_count_.load()));
      metrics_->set_gauge("flowforge_worker_pool_queue_depth", static_cast<double>(queue_.size()));
    }
  }
}

}  // namespace flowforge::engine
