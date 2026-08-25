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
[[nodiscard]] nlohmann::json to_error_body(const Error& error);

}  // namespace flowforge::server
