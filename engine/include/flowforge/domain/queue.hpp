#pragma once

#include <cstddef>
#include <string>

namespace flowforge::domain {

/// Static configuration for a named job queue. Distinct from the
/// in-process `engine::BlockingQueue<T>` concurrency primitive: this type
/// describes a *logical* queue (e.g. "emails", "reports") that jobs are
/// submitted to, which the scheduler (Phase 2) will map onto one or more
/// in-process queues/workers.
struct QueueConfig {
  std::string name;
  std::size_t capacity = 1024;
  int priority = 0;  ///< Higher runs first when multiple queues compete for workers.
};

}  // namespace flowforge::domain
