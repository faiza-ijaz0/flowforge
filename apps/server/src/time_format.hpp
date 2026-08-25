#pragma once

#include <string>

#include "flowforge/infra/clock.hpp"

namespace flowforge::server {

/// Formats a TimePoint as RFC 3339 / ISO 8601 UTC, e.g.
/// "2026-08-25T14:03:21.123Z". Kept in apps/server rather than
/// engine/infra because it is purely a wire-format concern for the JSON
/// API, not something the engine itself needs.
[[nodiscard]] std::string to_iso8601(infra::TimePoint time_point);

}  // namespace flowforge::server
