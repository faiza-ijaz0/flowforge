#pragma once

#include <memory>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of `engine::IExecutionManager`
/// against the `job_attempts` table
/// (database/migrations/0006_create_job_attempts.sql). See
/// docs/architecture/execution-model.md, "Execution attempt persistence",
/// for the row<->domain mapping.
class PostgresExecutionRepository final : public engine::IExecutionManager {
 public:
  PostgresExecutionRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                              std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> record(const domain::Execution& execution) override;
  [[nodiscard]] Result<std::vector<domain::Execution>> history_for(const infra::JobId& job_id) const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
