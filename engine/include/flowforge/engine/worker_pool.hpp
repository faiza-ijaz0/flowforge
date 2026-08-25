#pragma once

#include <cstddef>

#include "flowforge/domain/job.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Job-aware worker pool boundary: distinct from the concrete
/// `ThreadPool` primitive (thread_pool.hpp), which knows nothing about
/// jobs. `IWorkerPool` is what the scheduler will dispatch jobs to; a
/// concrete implementation (Phase 2) is expected to wrap a `ThreadPool`
/// plus an `IExecutor`, translating "run this job" into a submitted task
/// and recording the resulting `Execution` via the persistence layer.
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
};

}  // namespace flowforge::engine
