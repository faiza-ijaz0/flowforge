#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/user_repository.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of IUserRepository against the
/// `users` table (database/migrations/0016_create_users.sql). Mirrors
/// `PostgresProductRepository`'s row<->domain mapping conventions exactly.
class PostgresUserRepository final : public IUserRepository {
 public:
  PostgresUserRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                         std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedUserRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::User>> find_by_email(const std::string& email) const override;
  [[nodiscard]] Result<std::vector<domain::User>> list(std::size_t limit, std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
