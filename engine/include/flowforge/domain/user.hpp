#pragma once

#include <optional>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

/// The persisted representation of a user (database/migrations/
/// 0016_create_users.sql) -- the read-model counterpart of
/// `NormalizedUserRecord` (user_record.hpp), which is the input/
/// validation-side type. Mirrors `domain::Product`'s shape/conventions
/// exactly (see that class's comment for the full rationale): a plain
/// aggregate, not a class with invariants to encapsulate, and `job_id` is
/// how a user's provenance is recoverable without duplicating
/// `workload_id` onto this table.
struct User {
  infra::UserId id;
  std::string name;
  std::string email;
  std::optional<std::string> phone;
  std::optional<infra::JobId> job_id;
  infra::TimePoint created_at;
  infra::TimePoint updated_at;
};

}  // namespace flowforge::domain
