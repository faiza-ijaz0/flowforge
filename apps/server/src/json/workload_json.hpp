#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/job.hpp"
#include "flowforge/domain/workload.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/user_import.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::server {

/// Serializes a Workload to its wire representation (id/type/status/
/// total_items/queued_items/running_items/completed_items/failed_items/
/// timestamps). status()/the four item counts reflect whatever progress
/// the caller already computed via WorkloadService (see
/// workload_routes.cpp) -- this function does no computation of its own.
[[nodiscard]] nlohmann::json to_json(const domain::Workload& workload);

/// Serializes one item's create-time dispatch outcome --
/// `{"job_id": "...", "scheduled": bool, "reason"?: "..."}` -- the shape
/// both `POST /api/v1/workloads` and `POST /api/v1/workloads/user-imports`
/// return in their `"items"` array (see job_routes.cpp's `"scheduling"`
/// field, which this mirrors).
[[nodiscard]] nlohmann::json to_json(const services::WorkloadItemDispatchOutcome& outcome);

/// Serializes one rejected CSV row -- `{"row_number": N, "reason": "..."}`.
[[nodiscard]] nlohmann::json to_json(const services::RejectedImportRow& row);

/// Serializes a full `services::UserImportResult` -- the created
/// workload's fields plus the CSV-specific summary (total_rows/
/// valid_rows/invalid_rows/rejected_rows/rejected_rows_truncated) and the
/// per-item dispatch outcomes. See docs/architecture/user-import.md,
/// "Bulk submission semantics" for why all of these fields are present
/// together rather than only reporting the created workload.
[[nodiscard]] nlohmann::json to_json(const services::UserImportResult& result);

/// Serializes one row of a workload's paginated item list (`GET
/// /api/v1/workloads/{id}/items`): job_id/name/email (parsed from the
/// job's own payload -- see docs/architecture/user-import.md, "Row -> Job
/// payload" -- with graceful `null` fallback if the payload isn't the
/// expected shape, e.g. for a job created outside the CSV import path)/
/// status/attempt_count/last_error/updated_at. Never exposes anything
/// beyond what `domain::Job` itself already carries -- no stack traces, no
/// internal exception text (see `apps/server/src/http/error_response.cpp`
/// for the general policy this follows).
[[nodiscard]] nlohmann::json to_json_workload_item(const domain::Job& job);

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
