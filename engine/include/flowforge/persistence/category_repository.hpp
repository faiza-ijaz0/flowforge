#pragma once

#include <optional>
#include <vector>

#include "flowforge/domain/category.hpp"
#include "flowforge/domain/category_record.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// Persistence boundary for categories (database/migrations/
/// 0015_create_categories.sql). Mirrors `IProductRepository`'s shape and
/// `upsert()` semantics exactly: `handlers::CategoryProcessHandler` is the
/// only writer, and a re-imported slug is expected behavior (re-running
/// the same import, or deliberately updating a category's name/
/// description/parent), not an error -- see
/// docs/architecture/category-processing.md, "Duplicate semantics".
///
/// `find_by_slug()` is not just a read API: `CategoryProcessHandler` also
/// uses it to walk a record's parent chain (existence + cycle check)
/// before writing -- see that handler's class comment.
class ICategoryRepository {
 public:
  ICategoryRepository() = default;
  virtual ~ICategoryRepository() = default;
  ICategoryRepository(const ICategoryRepository&) = delete;
  ICategoryRepository& operator=(const ICategoryRepository&) = delete;
  ICategoryRepository(ICategoryRepository&&) = delete;
  ICategoryRepository& operator=(ICategoryRepository&&) = delete;

  /// Inserts a new category row, or updates the existing one with the
  /// same `slug` in place -- `job_id` (and `updated_at`) always reflect
  /// the most recent write, `created_at` and `id` are preserved across an
  /// update. Never raises `Conflict` for a duplicate slug -- that is the
  /// expected, handled case this method exists for.
  virtual Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedCategoryRecord& record) = 0;

  [[nodiscard]] virtual Result<std::optional<domain::Category>> find_by_slug(
      const std::string& slug) const = 0;

  [[nodiscard]] virtual Result<std::vector<domain::Category>> list(std::size_t limit,
                                                                   std::size_t offset) const = 0;

  [[nodiscard]] virtual Result<std::size_t> count() const = 0;
};

}  // namespace flowforge::persistence
