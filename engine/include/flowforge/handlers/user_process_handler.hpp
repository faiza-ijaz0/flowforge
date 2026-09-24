#pragma once

#include <memory>

#include "flowforge/engine/job_handler.hpp"
#include "flowforge/persistence/user_repository.hpp"

namespace flowforge::handlers {

/// The first concrete workload-processing handler (Phase 3A -- see
/// docs/architecture/workload-model.md, "User Import as the first concrete
/// workload"). Registered under job_type "user.process", the same string
/// `services::CreateWorkloadRequest::type` uses for a user-import workload
/// (see `WorkloadService`'s class comment, "Why type == job_type").
///
/// Validates and normalizes one user record's payload -- a flat JSON
/// object `{"name": "...", "email": "...", "phone": "..." (optional)}` --
/// deterministically: trims `name`, trims+lowercases `email`, and rejects
/// (as a hard, non-retryable `Result` error -- mirroring
/// `DelayHandler`'s convention for a structurally-invalid payload) a
/// payload missing either required field or with a malformed email. This
/// is real, bounded application logic, not an echo -- see IJobHandler's
/// "no arbitrary code execution" constraint, which this respects by only
/// ever touching its own payload string plus its own constructor-injected
/// repository -- no external I/O beyond that.
///
/// The actual validation/normalization rules live in
/// `domain::validate_and_normalize_user_record()` (Phase 3B), shared with
/// `services::WorkloadService`'s CSV user-import path
/// (`services::parse_user_import_csv`) so a CSV row that the import
/// preview reports as "valid" is guaranteed to be accepted by this
/// handler at execution time too -- see docs/architecture/user-import.md.
///
/// **Persistence (Phase 3H).** Every earlier phase left Users the one
/// domain of the three (Users/Products/Categories) with no dedicated
/// table -- a validated record was normalized and echoed back as the job's
/// output, but nothing survived beyond the job/job_attempts rows every job
/// already gets. That inconsistency is closed here: this handler now
/// upserts into a `users` table (migration 0016) through a constructor-
/// injected `IUserRepository`, exactly mirroring `ProductProcessHandler`'s
/// "Why ProductProcessHandler writes to PostgreSQL directly" rationale --
/// the repository is this handler instance's own dependency, resolved once
/// at application composition (`apps/server/src/http/app.cpp`), never
/// reached around `ExecutionContext`.
///
/// Hand-rolled, minimal JSON field extraction (not nlohmann::json): the
/// engine has zero JSON library dependency by design (see
/// docs/architecture/overview.md, "Dependency direction") -- the same
/// reason `postgres_job_repository.cpp` hand-parses `retry_policy` JSON
/// rather than linking nlohmann::json into `flowforge_engine`. Supports
/// only `\"` and `\\` escapes within a string value and does not parse
/// `metadata` at all -- explicitly documented, bounded-scope limitations
/// (see docs/architecture/workload-model.md, "Known limitations"), not
/// oversights.
///
/// Retryability: a validation failure (malformed/missing fields) is always
/// non-retryable -- retrying an unfixable payload can never succeed,
/// identical to before Phase 3H. A `users` table write failure
/// (`ErrorCode::Database`/`Infrastructure`) is retryable=true, mirroring
/// `ProductProcessHandler`: a transient database/connection-pool issue may
/// succeed on a later attempt.
class UserProcessHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "user.process";

  explicit UserProcessHandler(std::shared_ptr<persistence::IUserRepository> user_repository)
      : user_repository_(std::move(user_repository)) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;

 private:
  std::shared_ptr<persistence::IUserRepository> user_repository_;
};

}  // namespace flowforge::handlers
