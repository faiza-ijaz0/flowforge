#pragma once

#include <expected>

#include "flowforge/error.hpp"

namespace flowforge {

/// Canonical result type for all fallible operations in FlowForge.
///
/// FlowForge uses `std::expected<T, Error>` (C++23) rather than exceptions
/// for expected/recoverable failures: configuration problems, validation
/// failures, not-found lookups, database errors, etc. This makes fallible
/// call sites visible in function signatures and forces callers to handle
/// (or explicitly propagate) failure, which matters a great deal in a job
/// engine where a swallowed error can silently drop a job. See
/// docs/architecture/overview.md for the full rationale and the narrow set
/// of cases where exceptions remain appropriate.
template <typename T>
using Result = std::expected<T, Error>;

}  // namespace flowforge
