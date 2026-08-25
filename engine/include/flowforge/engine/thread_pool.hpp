#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <thread>
#include <vector>

#include "flowforge/engine/blocking_queue.hpp"

namespace flowforge::engine {

/// Fixed-size pool of worker threads consuming `std::function<void()>`
/// tasks from an internal `BlockingQueue`. This is the concrete,
/// production concurrency primitive the higher-level job engine
/// (WorkerPool/Executor, Phase 2) will schedule work onto; it has no
/// notion of jobs, retries, or priorities on its own.
///
/// Shutdown is graceful: `stop()` (also called from the destructor) closes
/// the task queue so no new tasks are accepted, then joins every worker
/// thread after it finishes draining already-queued tasks.
class ThreadPool {
 public:
  explicit ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
      thread_count = 1;
    }
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
      workers_.emplace_back([this] { worker_loop(); });
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  // Move is deleted, not defaulted: worker threads capture `this` in their
  // loop lambda at construction time, so relocating the ThreadPool object
  // would leave them running against a dangling pointer.
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  ~ThreadPool() { stop(); }

  /// Submits a task and returns a future for its result. Throws if the
  /// pool has already been stopped -- submitting after shutdown is a
  /// programming error, not a recoverable runtime condition, so this uses
  /// an exception rather than `Result<T>` (see docs/architecture/overview.md).
  template <typename F, typename R = std::invoke_result_t<F>>
  [[nodiscard]] std::future<R> submit(F&& f) {
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> future = task->get_future();
    const bool accepted = tasks_.push([task] { (*task)(); });
    if (!accepted) {
      throw std::runtime_error("ThreadPool::submit called after stop()");
    }
    return future;
  }

  /// Stops accepting new work and waits for in-flight/queued tasks to
  /// finish. Idempotent.
  void stop() {
    if (stopped_.exchange(true)) {
      return;
    }
    tasks_.close();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  [[nodiscard]] std::size_t thread_count() const noexcept { return workers_.size(); }

 private:
  void worker_loop() {
    while (auto task = tasks_.pop()) {
      (*task)();
    }
  }

  BlockingQueue<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  std::atomic<bool> stopped_{false};
};

}  // namespace flowforge::engine
