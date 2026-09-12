#pragma once

#include <optional>
#include <vector>

#include "flowforge/domain/product.hpp"
#include "flowforge/domain/product_record.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// Persistence boundary for products (database/migrations/
/// 0014_create_products.sql). Mirrors `IWorkloadRepository`'s shape/
/// conventions, with one addition `upsert()` implies:
/// `handlers::ProductProcessHandler` is the only writer (see its class
/// comment), and a re-imported SKU is expected behavior (a supplier
/// re-sends the same catalog with updated prices/stock), not an error --
/// see docs/architecture/product-processing.md, "Why upsert, not
/// insert-or-conflict".
class IProductRepository {
 public:
  IProductRepository() = default;
  virtual ~IProductRepository() = default;
  IProductRepository(const IProductRepository&) = delete;
  IProductRepository& operator=(const IProductRepository&) = delete;
  IProductRepository(IProductRepository&&) = delete;
  IProductRepository& operator=(IProductRepository&&) = delete;

  /// Inserts a new product row, or updates the existing one with the
  /// same (case-normalized) `sku` in place -- `job_id` (and
  /// `updated_at`) always reflect the most recent write, `created_at`
  /// and `id` are preserved across an update. Never throws/raises a
  /// `Conflict` for a duplicate SKU -- that is the expected, handled
  /// case this method exists for.
  virtual Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedProductRecord& record) = 0;

  [[nodiscard]] virtual Result<std::optional<domain::Product>> find_by_sku(const std::string& sku) const = 0;

  [[nodiscard]] virtual Result<std::vector<domain::Product>> list(std::size_t limit,
                                                                  std::size_t offset) const = 0;

  [[nodiscard]] virtual Result<std::size_t> count() const = 0;
};

}  // namespace flowforge::persistence
