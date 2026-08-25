#pragma once

#include <vector>

#include "flowforge/domain/workflow.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

class IWorkflowRepository {
 public:
  IWorkflowRepository() = default;
  virtual ~IWorkflowRepository() = default;
  IWorkflowRepository(const IWorkflowRepository&) = delete;
  IWorkflowRepository& operator=(const IWorkflowRepository&) = delete;
  IWorkflowRepository(IWorkflowRepository&&) = delete;
  IWorkflowRepository& operator=(IWorkflowRepository&&) = delete;

  virtual Result<void> insert(const domain::Workflow& workflow) = 0;
  [[nodiscard]] virtual Result<domain::Workflow> find_by_id(const infra::WorkflowId& id) const = 0;
  [[nodiscard]] virtual Result<std::vector<domain::Workflow>> list(std::size_t limit,
                                                                   std::size_t offset) const = 0;
  virtual Result<void> update(const domain::Workflow& workflow) = 0;
};

}  // namespace flowforge::persistence
