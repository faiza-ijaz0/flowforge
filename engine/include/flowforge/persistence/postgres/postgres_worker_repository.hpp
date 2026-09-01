#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/worker_repository.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of IWorkerRepository against the
/// `workers` table (database/migrations/0004_create_workers.sql).
class PostgresWorkerRepository final : public IWorkerRepository {
 public:
  PostgresWorkerRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                           std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> insert(const domain::Worker& worker) override;
  [[nodiscard]] Result<domain::Worker> find_by_id(const infra::WorkerId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Worker>> list() const override;
  Result<void> update(const domain::Worker& worker) override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
