#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

enum class ExecutionOutcome : std::uint8_t { Running, Succeeded, Failed, TimedOut, Cancelled };

[[nodiscard]] std::string_view to_string(ExecutionOutcome outcome) noexcept;

/// Record of a single attempt to execute a job -- the domain equivalent of
/// a `job_attempts` row (see database/migrations). Distinct from `Job`
/// itself: a job with `max_attempts = 3` may accumulate up to three
/// `Execution` records.
struct Execution {
  infra::ExecutionId id;
  infra::JobId job_id;
  std::uint32_t attempt_number = 1;
  ExecutionOutcome outcome = ExecutionOutcome::Running;
  infra::TimePoint started_at;
  std::optional<infra::TimePoint> finished_at;
  std::optional<std::string> error_message;
};

}  // namespace flowforge::domain
