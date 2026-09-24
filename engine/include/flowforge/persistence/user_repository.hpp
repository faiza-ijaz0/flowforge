#pragma once

#include <optional>
#include <vector>

#include "flowforge/domain/user.hpp"
#include "flowforge/domain/user_record.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// Persistence boundary for users (database/migrations/0016_create_users.sql).
/// Mirrors `IProductRepository`'s shape/conventions exactly, keyed by
/// (trimmed+lowercased) `email` instead of `sku`: `handlers::
/// UserProcessHandler` is the only writer, and a re-imported email is
/// expected behavior (the same person's row being re-submitted with
/// updated details), not an error -- see `IProductRepository`'s "Why
/// upsert, not insert-or-conflict" rationale, which applies identically
/// here.
class IUserRepository {
 public:
  IUserRepository() = default;
  virtual ~IUserRepository() = default;
  IUserRepository(const IUserRepository&) = delete;
  IUserRepository& operator=(const IUserRepository&) = delete;
  IUserRepository(IUserRepository&&) = delete;
  IUserRepository& operator=(IUserRepository&&) = delete;

  /// Inserts a new user row, or updates the existing one with the same
  /// (case-normalized) `email` in place -- `job_id` (and `updated_at`)
  /// always reflect the most recent write, `created_at` and `id` are
  /// preserved across an update. Never returns `Conflict` for a duplicate
  /// email -- that is the expected, handled case this method exists for.
  virtual Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedUserRecord& record) = 0;

  [[nodiscard]] virtual Result<std::optional<domain::User>> find_by_email(const std::string& email) const = 0;

  [[nodiscard]] virtual Result<std::vector<domain::User>> list(std::size_t limit,
                                                               std::size_t offset) const = 0;

  [[nodiscard]] virtual Result<std::size_t> count() const = 0;
};

}  // namespace flowforge::persistence
