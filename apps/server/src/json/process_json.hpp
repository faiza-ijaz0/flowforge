#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/structured_record.hpp"
#include "flowforge/domain/user_record.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/input_processing_service.hpp"

namespace flowforge::server {

/// Serializes one rejected record -- `{"index": N, "reason": "..."}`.
[[nodiscard]] nlohmann::json to_json(const domain::RejectedRecord& record);

/// Serializes a full `services::ProcessResult` -- the created workload's
/// fields (see `json/workload_json.hpp`'s `to_json(const domain::
/// Workload&)`, which this reuses) plus the source-agnostic extraction
/// summary (total_records/valid_records/invalid_records/rejected_records/
/// rejected_records_truncated) and the per-item dispatch outcomes.
/// Mirrors `to_json(const services::UserImportResult&)`
/// (workload_json.hpp) at the generic level `POST /api/v1/process`
/// operates at -- see docs/architecture/input-processing.md.
[[nodiscard]] nlohmann::json to_json(const services::ProcessResult& result);

/// Serializes one normalized, ready-to-confirm user record --
/// `{"name": "...", "email": "...", "phone"?: "..."}`.
[[nodiscard]] nlohmann::json to_json(const domain::NormalizedUserRecord& record);

/// Serializes a full `services::PreviewResult` (Phase 3D-1 -- see
/// docs/architecture/input-processing.md, "Preview"). No `workload`/
/// `items` fields exist here (unlike `to_json(const
/// services::ProcessResult&)`): nothing was created by a preview call.
[[nodiscard]] nlohmann::json to_json(const services::PreviewResult& result);

/// Parses and *shape*-validates a `POST /api/v1/process/confirm` request
/// body into a `services::ConfirmRequest`. Only rejects a body that is not
/// well-formed enough to construct a request from -- per-record business-
/// rule validation (is this actually a valid name/email/phone) is
/// `InputProcessingService::confirm`'s job, mirroring
/// `parse_create_workload_request`'s (workload_json.hpp) division of
/// labor.
[[nodiscard]] Result<services::ConfirmRequest> parse_confirm_request(const nlohmann::json& body);

}  // namespace flowforge::server
