#pragma once

// Internal helper shared by the postgres/* repository implementations.
// Not under include/ -- this is a persistence-layer implementation detail,
// not part of FlowForge's public API surface.

#include "flowforge/infra/clock.hpp"

namespace flowforge::persistence::postgres {

/// Converts a domain TimePoint to fractional seconds since the Unix epoch
/// for binding as a query parameter passed through PostgreSQL's
/// `to_timestamp(double precision)`. Chosen over formatting/parsing an
/// ISO-8601 string: it sidesteps timezone-format edge cases entirely (no
/// text parsing on either side of the round trip) and needs no date/time
/// formatting library.
[[nodiscard]] inline double to_epoch_seconds(infra::TimePoint tp) noexcept {
  return std::chrono::duration<double>(tp.time_since_epoch()).count();
}

/// Inverse of to_epoch_seconds(); pairs with reading a column via
/// PostgreSQL's `extract(epoch from <timestamptz column>)`.
[[nodiscard]] inline infra::TimePoint from_epoch_seconds(double seconds) noexcept {
  return infra::TimePoint{
      std::chrono::duration_cast<infra::TimePoint::duration>(std::chrono::duration<double>(seconds))};
}

}  // namespace flowforge::persistence::postgres
