#pragma once

#include "flowforge/engine/job_handler.hpp"

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
/// ever touching its own payload string, with no external I/O.
///
/// The actual validation/normalization rules live in
/// `domain::validate_and_normalize_user_record()` (Phase 3B), shared with
/// `services::WorkloadService`'s CSV user-import path
/// (`services::parse_user_import_csv`) so a CSV row that the import
/// preview reports as "valid" is guaranteed to be accepted by this
/// handler at execution time too -- see docs/architecture/user-import.md.
/// This class only owns JSON payload extraction and the
/// execution-specific concerns (cancellation, output shape).
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
/// Retryability: this handler has no external I/O, so every failure path
/// (malformed/missing fields, cancellation) is deterministic and would
/// fail identically on a retry -- `retryable()` is therefore always
/// `false` wherever it applies (the cooperative-cancellation
/// `ExecutionResult::failure` path; see .cpp).
class UserProcessHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "user.process";

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;
};

}  // namespace flowforge::handlers
