#pragma once

#include <vector>

#include "flowforge/domain/execution.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Boundary for recording and querying job execution history (the
/// `job_attempts` table once persistence is wired up). Separated from
/// `IExecutor` (which runs the work) so that execution *bookkeeping* can
/// be tested/implemented independently of the execution *mechanism*.
class IExecutionManager {
 public:
  IExecutionManager() = default;
  virtual ~IExecutionManager() = default;
  IExecutionManager(const IExecutionManager&) = delete;
  IExecutionManager& operator=(const IExecutionManager&) = delete;
  IExecutionManager(IExecutionManager&&) = delete;
  IExecutionManager& operator=(IExecutionManager&&) = delete;

  virtual Result<void> record(const domain::Execution& execution) = 0;
  [[nodiscard]] virtual Result<std::vector<domain::Execution>> history_for(
      const infra::JobId& job_id) const = 0;
};

}  // namespace flowforge::engine
