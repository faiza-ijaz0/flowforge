#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/product.hpp"

namespace flowforge::server {

/// Serializes a persisted Product -- id/sku/name/price/currency/
/// category/description/stock_quantity/job_id/timestamps. Mirrors
/// `to_json(const domain::Workload&)`'s conventions (workload_json.hpp):
/// timestamps as ISO 8601 strings, optional fields (`category`,
/// `description`, `job_id`) as JSON `null` when unset rather than
/// omitted, so the dashboard never needs to branch on key presence.
[[nodiscard]] nlohmann::json to_json(const domain::Product& product);

}  // namespace flowforge::server
