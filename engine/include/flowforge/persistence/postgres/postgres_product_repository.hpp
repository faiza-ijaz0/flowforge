#pragma once

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/product_repository.hpp"

namespace flowforge::persistence::postgres {

/// Real, libpqxx-backed implementation of `IProductRepository` against
/// the `products` table (database/migrations/0014_create_products.sql).
/// `upsert()` is a single `INSERT ... ON CONFLICT (sku) DO UPDATE`
/// statement -- see the interface's class comment for why a duplicate
/// SKU is a normal, handled case rather than a `Conflict` error, and
/// `libpqxx`/`pqxx::*` types never appear outside this translation unit
/// (matches `PostgresWorkloadRepository`'s existing boundary).
class PostgresProductRepository final : public IProductRepository {
 public:
  PostgresProductRepository(std::shared_ptr<PgConnectionPool> pool, std::shared_ptr<infra::Logger> logger,
                            std::shared_ptr<infra::MetricsRegistry> metrics = nullptr);

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedProductRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::Product>> find_by_sku(const std::string& sku) const override;
  [[nodiscard]] Result<std::vector<domain::Product>> list(std::size_t limit,
                                                          std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<PgConnectionPool> pool_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
};

}  // namespace flowforge::persistence::postgres
