#pragma once

#include <string_view>

#include "flowforge/error.hpp"

namespace flowforge::persistence::postgres {

/// Maps a caught exception (a pqxx exception in practice, but any
/// std::exception is handled) to FlowForge's `Error` type. `context` is a
/// short machine-readable label (e.g. "job_repository.insert") folded into
/// the message so a database-error log line is attributable without
/// needing a stack trace.
///
/// Deliberately keeps `e.what()` out of the *returned* Error's message for
/// anything other than validation-shaped failures: raw PostgreSQL error
/// text can include table/column/constraint names and fragments of query
/// text, which is useful for an operator reading server logs but is
/// internal detail that shouldn't reach an API client through a 500
/// response body. Callers are expected to log `e.what()` themselves
/// (typically via `infra::Logger`) before calling this.
[[nodiscard]] Error map_exception(const std::exception& e, std::string_view context);

}  // namespace flowforge::persistence::postgres
