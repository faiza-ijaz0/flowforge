#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/services/input_processing_service.hpp"

namespace flowforge::server {

/// Registers the Phase 3C processing-center endpoint (see
/// docs/architecture/input-processing.md):
///
///   POST /api/v1/process   multipart/form-data: "source", "target", "file"
///
/// A thin HTTP <-> `services::InputProcessingService` translation, exactly
/// like every other route file in this codebase -- no business logic
/// here. `source`/`target` are shape-validated (must be one of the known
/// enum strings) before `InputProcessingService::process()` is ever
/// called; an unsupported-but-well-formed `(source, target)` combination
/// (e.g. `image`+`users`) is rejected by that call with a clear,
/// non-fake `400`, never a silent no-op or a fabricated success -- see
/// `InputProcessingService::process()`'s class comment.
void register_process_routes(
    httplib::Server& server,
    const std::shared_ptr<services::InputProcessingService>& input_processing_service);

}  // namespace flowforge::server
