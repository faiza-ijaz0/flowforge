#pragma once

#include <nlohmann/json.hpp>

#include "flowforge/domain/structured_record.hpp"
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

}  // namespace flowforge::server
