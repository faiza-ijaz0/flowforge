#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of IJobRepository against the
/// `jobs` table (database/migrations/0005_create_jobs.sql). See
/// docs/architecture/overview.md, "PostgreSQL persistence", for the
/// row<->domain mapping decisions (payload representation, retry_policy
/// encoding, timestamp handling).
class PostgresJobRepository final : public IJobRepository {
 public:
  PostgresJobRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                        std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> insert(const domain::Job& job) override;
  [[nodiscard]] Result<domain::Job> find_by_id(const infra::JobId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Job>> list(std::size_t limit, std::size_t offset) const override;
  Result<void> update(const domain::Job& job) override;
  [[nodiscard]] Result<std::vector<domain::Job>> list_by_status(domain::JobStatus status,
                                                                std::size_t limit) const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
