#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/workflow_repository.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of IWorkflowRepository against the
/// `workflows` / `workflow_steps` / `workflow_step_dependencies` tables
/// (database/migrations/0007-0008, 0010). A workflow and all of its steps
/// and dependency edges are written in one transaction: insert() either
/// persists the whole DAG or none of it.
class PostgresWorkflowRepository final : public IWorkflowRepository {
 public:
  PostgresWorkflowRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                             std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> insert(const domain::Workflow& workflow) override;
  [[nodiscard]] Result<domain::Workflow> find_by_id(const infra::WorkflowId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Workflow>> list(std::size_t limit,
                                                           std::size_t offset) const override;
  Result<void> update(const domain::Workflow& workflow) override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
