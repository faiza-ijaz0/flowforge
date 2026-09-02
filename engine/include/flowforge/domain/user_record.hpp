#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "flowforge/result.hpp"

namespace flowforge::domain {

/// A validated, normalized user record ready to become a `user.process`
/// job payload -- the single, shared representation both
/// `handlers::UserProcessHandler` (validating one job's payload at
/// execution time) and `services::WorkloadService`'s CSV import path
/// (validating one CSV row at import time, see
/// `services::parse_user_import_csv`) produce via the same function below,
/// so "what makes a valid user record" can never drift between the two
/// call sites. See docs/architecture/user-import.md.
struct NormalizedUserRecord {
  std::string name;
  std::string email;
  std::optional<std::string> phone;
};

/// Trims `name`, trims+lowercases `email`, trims optional `phone`, and
/// validates all three against the same bounds/shape rules `user.process`
/// has enforced since Phase 3A (see .cpp for exact limits and rationale).
/// Returns `ErrorCode::Validation` describing the first rule that failed
/// -- deterministic, not accumulating every violation.
[[nodiscard]] Result<NormalizedUserRecord> validate_and_normalize_user_record(
    std::string_view name, std::string_view email, std::optional<std::string_view> phone);

/// Serializes `record` as the flat JSON object `user.process` expects as
/// its job payload: `{"name": "...", "email": "...", "phone": "..."}`
/// (`phone` omitted when unset). Used by `services::WorkloadService` to
/// build each CSV-imported row's `Job::payload()` -- see
/// docs/architecture/user-import.md, "Row -> Job payload". Hand-rolled,
/// not `nlohmann::json`: the engine has zero JSON library dependency by
/// design (see docs/architecture/overview.md, "Dependency direction").
[[nodiscard]] std::string serialize_user_record_as_job_payload(const NormalizedUserRecord& record);

}  // namespace flowforge::domain
