#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/category_repository.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of `ICategoryRepository` against
/// the `categories` table (database/migrations/0015_create_categories.sql).
/// `upsert()` is a single `INSERT ... ON CONFLICT (slug) DO UPDATE`
/// statement -- see the interface's class comment for why a duplicate
/// slug is a normal, handled case, and `libpqxx`/`pqxx::*` types never
/// appear outside this translation unit (matches
/// `PostgresProductRepository`'s existing boundary).
class PostgresCategoryRepository final : public ICategoryRepository {
 public:
  PostgresCategoryRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                             std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedCategoryRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::Category>> find_by_slug(const std::string& slug) const override;
  [[nodiscard]] Result<std::vector<domain::Category>> list(std::size_t limit,
                                                           std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
