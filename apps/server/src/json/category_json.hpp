#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/category.hpp"

namespace flowforge::server {

/// Serializes a persisted Category -- id/name/slug/description/
/// parent_slug/job_id/timestamps. Mirrors `to_json(const domain::Product&)`'s
/// conventions (product_json.hpp): timestamps as ISO 8601 strings,
/// optional fields as JSON `null` when unset rather than omitted, so the
/// dashboard never needs to branch on key presence.
[[nodiscard]] nlohmann::json to_json(const domain::Category& category);

}  // namespace flowforge::server
