#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/services/workload_service.hpp"

namespace flowforge::server {

/// Registers the /api/v1/workloads endpoints (see
/// docs/architecture/workload-model.md and docs/architecture/user-import.md):
///   POST /api/v1/workloads             create a workload from a JSON `items` array
///   POST /api/v1/workloads/user-imports  create a "user.process" workload from an uploaded CSV
///   GET  /api/v1/workloads             list workloads (?limit=&offset=)
///   GET  /api/v1/workloads/{id}        fetch a single workload (live-computed progress)
///   GET  /api/v1/workloads/{id}/items  bounded, paginated per-item results
///
/// Handlers are a thin translation of HTTP <-> `services::WorkloadService`
/// (the generic, business-domain-agnostic engine service) and
/// `services::import_users_from_csv` (the CSV-upload endpoint's own,
/// user-specific adapter -- see user_import.hpp's class comment for why
/// that composition lives outside `WorkloadService`, not as one of its
/// methods). All business logic lives in one of those two places, not
/// here -- mirrors job_routes.cpp's division of labor. A future
/// products/categories CSV-upload endpoint would register its own routes
/// here (or in a sibling file) calling its own analogous
/// `import_products_from_csv`/`import_categories_from_csv` adapter,
/// without any change to `WorkloadService` or to the routes below.
///
/// `POST /api/v1/workloads`'s response body is the created workload (see
/// json/workload_json.hpp's `to_json`) plus an additive `"items"` array
/// reporting each item's dispatch outcome -- mirrors `POST /api/v1/jobs`'s
/// `"scheduling"` field (job_routes.cpp): a per-item scheduling/creation
/// failure never fails the whole HTTP request, since the workload and
/// every other item's job were genuinely created either way.
void register_workload_routes(httplib::Server& server,
                              const std::shared_ptr<services::WorkloadService>& workload_service,
                              const std::shared_ptr<infra::Logger>& logger,
                              const std::shared_ptr<infra::MetricsRegistry>& metrics);

}  // namespace flowforge::server
