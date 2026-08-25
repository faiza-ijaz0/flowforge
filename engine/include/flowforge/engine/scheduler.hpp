#pragma once

#include "flowforge/domain/job.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Boundary for job scheduling: deciding which queued job runs next,
/// applying priority ordering, and re-queuing failed attempts after their
/// computed backoff delay. Left as an interface in this phase --
/// implementing it requires the QueueManager and ExecutionManager below,
/// which are themselves not implemented yet. See
/// docs/architecture/overview.md ("Deferred to Phase 2") for the planned
/// design (a priority queue per logical queue, fed by BlockingQueue/
/// ThreadPool from engine/thread_pool.hpp, backed by IJobRepository for
/// durability across restarts).
class IScheduler {
 public:
  IScheduler() = default;
  virtual ~IScheduler() = default;
  IScheduler(const IScheduler&) = delete;
  IScheduler& operator=(const IScheduler&) = delete;
  IScheduler(IScheduler&&) = delete;
  IScheduler& operator=(IScheduler&&) = delete;

  virtual Result<void> schedule(const domain::Job& job) = 0;
  virtual Result<void> cancel(const infra::JobId& job_id) = 0;
};

}  // namespace flowforge::engine
