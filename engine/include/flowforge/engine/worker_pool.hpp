#pragma once

#include <cstddef>

#include "flowforge/domain/job.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Job-aware worker pool boundary: distinct from the concrete
/// `ThreadPool` primitive (thread_pool.hpp), which knows nothing about
/// jobs. `IWorkerPool` is what the scheduler dispatches jobs to; the
/// Phase 2B-3 concrete implementation (`LocalWorkerPool`) wraps a
/// `ThreadPool` plus an `IExecutor`, translating "run this job" into a
/// submitted task and recording the resulting `Execution` via the
/// persistence layer.
class IWorkerPool {
 public:
  IWorkerPool() = default;
  virtual ~IWorkerPool() = default;
  IWorkerPool(const IWorkerPool&) = delete;
  IWorkerPool& operator=(const IWorkerPool&) = delete;
  IWorkerPool(IWorkerPool&&) = delete;
  IWorkerPool& operator=(IWorkerPool&&) = delete;

  virtual Result<void> dispatch(const domain::Job& job) = 0;
  [[nodiscard]] virtual std::size_t capacity() const = 0;
  [[nodiscard]] virtual std::size_t active_count() const = 0;

  /// Requests cooperative cancellation of `job_id` if it is currently
  /// executing on one of this pool's workers. Returns `ErrorCode::NotFound`
  /// if it isn't (already finished, or never dispatched here) -- this is
  /// a best-effort signal (see `ExecutionContext::request_cancellation()`),
  /// not a guarantee the handler stops promptly or at all.
  virtual Result<void> request_cancellation(const infra::JobId& job_id) = 0;
};

}  // namespace flowforge::engine
