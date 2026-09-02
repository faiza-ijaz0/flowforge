#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/infra/metrics.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::server {

/// Registers the /api/v1/workloads endpoints (Phase 3A -- see
/// docs/architecture/workload-model.md):
///   POST /api/v1/workloads        create a workload + its item jobs
///   GET  /api/v1/workloads        list workloads (?limit=&offset=)
///   GET  /api/v1/workloads/{id}   fetch a single workload (with live-computed progress)
///
/// Handlers are a thin translation of HTTP <-> WorkloadService, mirroring
/// job_routes.cpp's division of labor: all business logic (validation,
/// job creation, scheduling) lives in WorkloadService, not here.
///
/// `POST /api/v1/workloads`'s response body is the created workload (see
/// json/workload_json.hpp's `to_json`) plus an additive `"items"` array
/// reporting each item's dispatch outcome -- mirrors `POST /api/v1/jobs`'s
/// `"scheduling"` field (job_routes.cpp): a per-item scheduling/creation
/// failure never fails the whole HTTP request, since the workload and
/// every other item's job were genuinely created either way.
///
/// This phase does NOT implement a CSV/multipart upload endpoint -- see
/// docs/architecture/workload-model.md, "Deferred to Phase 3B".
void register_workload_routes(httplib::Server& server,
                              const std::shared_ptr<services::WorkloadService>& workload_service,
                              const std::shared_ptr<infra::MetricsRegistry>& metrics);

}  // namespace flowforge::server
