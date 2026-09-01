#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

enum class ExecutionOutcome : std::uint8_t { Running, Succeeded, Failed, TimedOut, Cancelled };

[[nodiscard]] std::string_view to_string(ExecutionOutcome outcome) noexcept;

/// Inverse of to_string(ExecutionOutcome). Used by
/// `persistence::postgres::PostgresExecutionRepository` mapping a stored
/// `job_attempts.outcome` column back to the enum; returns std::nullopt
/// for any value that isn't one of the known outcome strings.
[[nodiscard]] std::optional<ExecutionOutcome> execution_outcome_from_string(std::string_view value) noexcept;

/// Record of a single attempt to execute a job -- the domain equivalent of
/// a `job_attempts` row (see database/migrations). Distinct from `Job`
/// itself: a job with `max_attempts = 3` may accumulate up to three
/// `Execution` records.
struct Execution {
  infra::ExecutionId id;
  infra::JobId job_id;
  /// The worker that ran this attempt. Optional to mirror
  /// `job_attempts.worker_id`'s `ON DELETE SET NULL` foreign key
  /// (nullable by design -- a worker record can be removed without
  /// losing attempt history). Populated for real by Phase 2B-3's
  /// `engine::JobExecutor`.
  std::optional<infra::WorkerId> worker_id;
  std::uint32_t attempt_number = 1;
  ExecutionOutcome outcome = ExecutionOutcome::Running;
  infra::TimePoint started_at;
  std::optional<infra::TimePoint> finished_at;
  std::optional<std::string> error_message;
};

}  // namespace flowforge::domain
