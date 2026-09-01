#pragma once

#include <atomic>
#include <memory>

#include "flowforge/domain/execution.hpp"
#include "flowforge/domain/job.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Boundary for actually running a job's work. Phase 2B-3 gives this its
/// first real implementation (`JobExecutor`) and, in doing so, extends the
/// Phase 1 signature (which took only a `Job`) with two things a real
/// executor genuinely needs and a bare `Job` cannot carry:
///
///   - `worker_id`: which `IWorkerPool` worker is running this attempt
///     (`domain::Execution::worker_id` -- see execution.hpp -- needs this
///     to populate the persisted `job_attempts.worker_id` column).
///   - `cancellation_flag`: the shared cooperative-cancellation channel
///     for *this specific attempt*. Owned by the caller (`IWorkerPool`,
///     which is what tracks "which job is running where" and services
///     external cancellation/timeout requests) and threaded down into the
///     `ExecutionContext` the executor builds -- see
///     docs/architecture/execution-model.md, "Cancellation".
class IExecutor {
 public:
  IExecutor() = default;
  virtual ~IExecutor() = default;
  IExecutor(const IExecutor&) = delete;
  IExecutor& operator=(const IExecutor&) = delete;
  IExecutor(IExecutor&&) = delete;
  IExecutor& operator=(IExecutor&&) = delete;

  virtual Result<domain::Execution> execute(const domain::Job& job, const infra::WorkerId& worker_id,
                                            std::shared_ptr<std::atomic<bool>> cancellation_flag) = 0;
};

}  // namespace flowforge::engine
