#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/job.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::server {

/// Serializes a Job to its wire representation. `payload` is re-parsed as
/// JSON when possible (jobs are created from a JSON body, so this
/// round-trips cleanly) and falls back to a raw string if the stored
/// payload is not valid JSON, which can only happen if a non-JSON payload
/// was written directly through the service layer rather than the API.
[[nodiscard]] nlohmann::json to_json(const domain::Job& job);

/// Parses and *shape*-validates (types/required fields) a create-job
/// request body into a `CreateJobRequest`. Business-rule validation
/// (non-empty queue name, payload size limits, etc.) is deliberately left
/// to `JobService::create_job` -- this function only rejects bodies that
/// are not well-formed enough to construct a request from.
[[nodiscard]] Result<services::CreateJobRequest> parse_create_job_request(const nlohmann::json& body);

}  // namespace flowforge::server
