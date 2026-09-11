#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/services/input_processing_service.hpp"

namespace flowforge::server {

/// Registers the Phase 3C/3D-1 processing-center endpoints (see
/// docs/architecture/input-processing.md):
///
///   POST /api/v1/process           multipart/form-data: "source", "target", "file"
///   POST /api/v1/process/preview   multipart/form-data: "source", "target", "file"
///   POST /api/v1/process/confirm   application/json: {"target", "records"}
///
/// A thin HTTP <-> `services::InputProcessingService` translation, exactly
/// like every other route file in this codebase -- no business logic
/// here. `source`/`target` are shape-validated (must be one of the known
/// enum strings) before `InputProcessingService::process()`/`preview()`
/// is ever called; an unsupported-but-well-formed combination (e.g.
/// `image`+`users` for `process()`, or `csv`+`users` for `preview()`) is
/// rejected by that call with a clear, non-fake `400`, never a silent
/// no-op or a fabricated success -- see `InputProcessingService`'s class
/// comment. `preview` never creates a workload or job (see
/// `InputProcessingService::preview`'s class comment); `confirm` is the
/// only one of these three that does, alongside `process`.
void register_process_routes(
    httplib::Server& server,
    const std::shared_ptr<services::InputProcessingService>& input_processing_service);

}  // namespace flowforge::server
