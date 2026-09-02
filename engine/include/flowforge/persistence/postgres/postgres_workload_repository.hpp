#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/workload_repository.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of IWorkloadRepository against the
/// `workloads` table (database/migrations/0012_create_workloads.sql). See
/// docs/architecture/workload-model.md for the row<->domain mapping
/// decisions (why there is no `status`/`completed_items`/`failed_items`
/// column).
class PostgresWorkloadRepository final : public IWorkloadRepository {
 public:
  PostgresWorkloadRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                             std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> insert(const domain::Workload& workload) override;
  [[nodiscard]] Result<domain::Workload> find_by_id(const infra::WorkloadId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Workload>> list(std::size_t limit,
                                                           std::size_t offset) const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
