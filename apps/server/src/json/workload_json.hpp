#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/workload.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::server {

/// Serializes a Workload to its wire representation (id/type/status/
/// total_items/completed_items/failed_items/timestamps). status()/
/// completed_items()/failed_items() reflect whatever progress the caller
/// already computed via WorkloadService (see workload_routes.cpp) -- this
/// function does no computation of its own.
[[nodiscard]] nlohmann::json to_json(const domain::Workload& workload);

/// Parses and *shape*-validates a create-workload request body into a
/// `services::CreateWorkloadRequest`. Mirrors `parse_create_job_request`'s
/// division of labor (job_json.hpp): only rejects bodies that are not
/// well-formed enough to construct a request from -- business-rule
/// validation (item count/size bounds) is
/// `WorkloadService::create_workload`'s job. Each element of the `items`
/// array becomes one item's opaque payload string: a JSON object is
/// `.dump()`'d (the common case -- e.g. `{"name": "...", "email": "..."}`
/// for a "user.process" workload), a JSON string is taken as-is, mirroring
/// how `parse_create_job_request` handles a job's top-level `payload`
/// field.
[[nodiscard]] Result<services::CreateWorkloadRequest> parse_create_workload_request(
    const nlohmann::json& body);

}  // namespace flowforge::server
