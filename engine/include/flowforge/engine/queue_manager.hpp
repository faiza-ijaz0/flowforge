#pragma once

#include <optional>

#include "flowforge/domain/job.hpp"
#include "flowforge/domain/queue.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Boundary for managing the set of logical job queues (see
/// domain::QueueConfig) and enqueueing/dequeueing jobs against them with
/// priority ordering and per-queue capacity limits. A concrete
/// implementation (Phase 2) is expected to be backed by
/// `BlockingQueue<domain::Job>` per logical queue plus `IJobRepository`
/// for durability.
class IQueueManager {
 public:
  IQueueManager() = default;
  virtual ~IQueueManager() = default;
  IQueueManager(const IQueueManager&) = delete;
  IQueueManager& operator=(const IQueueManager&) = delete;
  IQueueManager(IQueueManager&&) = delete;
  IQueueManager& operator=(IQueueManager&&) = delete;

  virtual Result<void> declare_queue(const domain::QueueConfig& config) = 0;
  virtual Result<void> enqueue(const domain::Job& job) = 0;
  virtual std::optional<domain::Job> dequeue(const std::string& queue_name) = 0;
};

}  // namespace flowforge::engine
