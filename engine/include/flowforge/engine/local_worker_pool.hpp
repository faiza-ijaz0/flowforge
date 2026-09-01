#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "flowforge/engine/blocking_queue.hpp"
#include "flowforge/engine/executor.hpp"
#include "flowforge/engine/thread_pool.hpp"
#include "flowforge/engine/worker_pool.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/worker_repository.hpp"

namespace flowforge::engine {

struct WorkerPoolConfig {
  std::size_t worker_count = 4;
  std::size_t queue_capacity = 1024;
};

/// Real `IWorkerPool` implementation (Phase 2B-3): a fixed set of named
/// local workers (`worker-1`..`worker-N`), each backed by one real,
/// persisted `domain::Worker` row (via `IWorkerRepository`) so
/// `job_attempts.worker_id` refers to something real rather than an
/// invented identifier -- see docs/architecture/execution-model.md,
/// "Worker identity". These are local, in-process workers; nothing here
/// claims distributed worker identity/registration.
///
/// Built entirely from existing primitives, per the phase brief: a plain
/// `BlockingQueue<domain::Job>` for the work queue (FIFO is correct here
/// -- `PriorityScheduler` already did priority ordering before calling
/// `dispatch()`) and `ThreadPool` for the worker threads themselves. No
/// new thread-management code.
class LocalWorkerPool final : public IWorkerPool {
 public:
  LocalWorkerPool(std::shared_ptr<IExecutor> executor,
                  std::shared_ptr<persistence::IWorkerRepository> worker_repository, WorkerPoolConfig config,
                  std::shared_ptr<infra::Logger> logger,
                  std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);
  ~LocalWorkerPool() override;
  LocalWorkerPool(const LocalWorkerPool&) = delete;
  LocalWorkerPool& operator=(const LocalWorkerPool&) = delete;
  LocalWorkerPool(LocalWorkerPool&&) = delete;
  LocalWorkerPool& operator=(LocalWorkerPool&&) = delete;

  /// Registers `config.worker_count` `domain::Worker` rows (`worker-1`..)
  /// and starts that many worker threads. Fails with `ErrorCode::Conflict`
  /// if already started.
  Result<void> start();

  /// Stops accepting new work, drains and executes whatever was already
  /// queued (mirrors `ThreadPool`/`PriorityScheduler`'s "close, drain,
  /// join" shutdown), and marks every registered worker `Offline`. Fails
  /// with `ErrorCode::Conflict` if already stopped.
  Result<void> stop();

  Result<void> dispatch(const domain::Job& job) override;
  [[nodiscard]] std::size_t capacity() const override;
  [[nodiscard]] std::size_t active_count() const override;
  Result<void> request_cancellation(const infra::JobId& job_id) override;

  /// Whether `start()` has succeeded and `stop()` has not yet been called
  /// -- the readiness signal `GET /ready` consults (Phase 2B-5): a pool
  /// that isn't running can't accept `dispatch()` calls, so the process
  /// isn't genuinely ready to process work even if it's otherwise alive.
  [[nodiscard]] bool is_running() const;

 private:
  void worker_loop(const infra::WorkerId& worker_id);

  std::shared_ptr<IExecutor> executor_;
  std::shared_ptr<persistence::IWorkerRepository> worker_repository_;
  WorkerPoolConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;

  BlockingQueue<domain::Job> queue_;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::vector<infra::WorkerId> worker_ids_;

  mutable std::mutex state_mutex_;
  bool running_ = false;
  bool ever_stopped_ = false;

  std::atomic<std::size_t> active_count_{0};

  mutable std::mutex active_mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> active_cancellation_flags_;
};

}  // namespace flowforge::engine
