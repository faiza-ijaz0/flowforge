#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/error.hpp"

namespace flowforge::server {

/// Maps a domain/engine Error to an HTTP status code. Centralized here so
/// every route handler produces consistent status codes for the same
/// error category instead of each handler guessing.
[[nodiscard]] int http_status_for(ErrorCode code) noexcept;

/// Renders the standard FlowForge API error body:
/// {"error": {"code": "not_found", "message": "..."}}
///
/// For an error whose `http_status_for(code)` is 5xx, `message` is
/// replaced with a fixed, generic string (Phase 2B-5) -- these codes
/// (Configuration/Infrastructure/Database/JobExecution/Internal/Network)
/// are exactly the ones whose underlying `Error::message()` can
/// originate from a raw caught exception (see `postgres::map_exception`'s
/// fallback branch, which embeds `e.what()` verbatim for an unclassified
/// database exception) rather than from application-generated validation
/// text. A 4xx error's message is always safe, caller-facing, and
/// unchanged: it was written by FlowForge's own validation code, never
/// derived from an exception. The full, original message is still logged
/// internally by whichever layer produced the error (every repository
/// already does this in its catch block) -- only the HTTP response is
/// sanitized. See docs/architecture/execution-model.md, "HTTP error
/// observability".
[[nodiscard]] nlohmann::json to_error_body(const Error& error);

}  // namespace flowforge::server
