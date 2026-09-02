#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace flowforge::domain {

/// What kind of business record a processing request produces jobs for
/// (Phase 3C -- see docs/architecture/input-processing.md). A processing
/// target is deliberately a domain-level concept, not a frontend route
/// name: `/users`/`/products`/`/categories` are dashboard routes that
/// happen to correspond to these, not the other way around -- the engine
/// must not depend on frontend naming.
enum class ProcessingTarget : std::uint8_t { Users, Products, Categories };

[[nodiscard]] std::string_view to_string(ProcessingTarget target) noexcept;

/// Inverse of to_string(ProcessingTarget); returns std::nullopt for any
/// value that isn't one of the known target strings.
[[nodiscard]] std::optional<ProcessingTarget> processing_target_from_string(std::string_view value) noexcept;

/// The `Job::job_type()`/`HandlerRegistry` key a processing target's jobs
/// are dispatched under -- e.g. `Users` -> `"user.process"`. This mapping
/// exists for all three targets (future mappings should be possible --
/// see the phase brief) even though only `"user.process"` has a
/// registered `IJobHandler` this phase; naming a job type here does not
/// by itself make that target usable end to end. Whether a given
/// `(InputSourceType, ProcessingTarget)` combination is actually
/// supported today is decided by `services::InputProcessingService`, not
/// by this function.
[[nodiscard]] std::string_view job_type_for_processing_target(ProcessingTarget target) noexcept;

}  // namespace flowforge::domain
