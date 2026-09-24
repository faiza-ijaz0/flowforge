#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/user.hpp"

namespace flowforge::server {

/// Serializes a persisted User -- id/name/email/phone/job_id/timestamps.
/// Mirrors `to_json(const domain::Product&)`'s conventions (product_json.hpp).
[[nodiscard]] nlohmann::json to_json(const domain::User& user);

}  // namespace flowforge::server
