#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace flowforge::domain {

/// The kind of raw input a caller can submit for processing (Phase 3C --
/// see docs/architecture/input-processing.md). Deliberately larger than
/// what is actually implemented this phase: the architecture must not
/// assume every source is implemented (see
/// `services::InputProcessingService`, which is the one place that
/// decides which `(InputSourceType, ProcessingTarget)` combinations are
/// actually supported today).
enum class InputSourceType : std::uint8_t { Csv, Image, Screenshot, Text, Url };

[[nodiscard]] std::string_view to_string(InputSourceType type) noexcept;

/// Inverse of to_string(InputSourceType), mirroring every other
/// `*_from_string` helper in this codebase (e.g.
/// `job_status_from_string`): returns std::nullopt for any value that
/// isn't one of the known source-type strings, rather than throwing.
[[nodiscard]] std::optional<InputSourceType> input_source_type_from_string(std::string_view value) noexcept;

/// Opaque raw input for a processing request -- exactly like
/// `domain::Job::payload()`, deliberately just bytes/text with no parsing
/// done yet: the domain layer must not depend on a CSV/image/JSON
/// library (see docs/architecture/overview.md, "Dependency direction").
/// `content` is text for `Csv`/`Text`/`Url`; a future `Image`/
/// `Screenshot` implementation would store raw image bytes here the same
/// way -- this type does not change shape when a new source type gains a
/// real extractor.
struct InputPayload {
  InputSourceType source_type;
  std::string content;
};

}  // namespace flowforge::domain
