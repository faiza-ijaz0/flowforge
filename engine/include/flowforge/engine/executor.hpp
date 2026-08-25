#pragma once

#include "flowforge/domain/execution.hpp"
#include "flowforge/domain/job.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Boundary for actually running a job's work. A real implementation
/// (Phase 2) will need a pluggable "job handler" registry (mapping a
/// job's queue/type to executable code), timeout enforcement, and
/// cancellation support -- none of which exist yet. Kept as an interface
/// so the scheduler/worker pool can be developed and tested against a
/// fake executor before the real execution model is designed.
class IExecutor {
 public:
  IExecutor() = default;
  virtual ~IExecutor() = default;
  IExecutor(const IExecutor&) = delete;
  IExecutor& operator=(const IExecutor&) = delete;
  IExecutor(IExecutor&&) = delete;
  IExecutor& operator=(IExecutor&&) = delete;

  virtual Result<domain::Execution> execute(const domain::Job& job) = 0;
};

}  // namespace flowforge::engine
