#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/engine/scheduler.hpp"
#include "flowforge/engine/worker_pool.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::server {

/// Registers the /api/v1/jobs endpoints:
///   POST   /api/v1/jobs             create a job
///   GET    /api/v1/jobs             list jobs (?limit=&offset=)
///   GET    /api/v1/jobs/{id}        fetch a single job
///   GET    /api/v1/jobs/{id}/attempts  list execution attempt history (Phase 2B-3, additive)
///   POST   /api/v1/jobs/{id}/cancel cancel a job
///
/// Handlers are a thin translation of HTTP <-> JobService/IScheduler/
/// IWorkerPool; all business logic lives there so it stays testable
/// without HTTP.
///
/// POST /api/v1/jobs's semantics from Phase 1/2A are unchanged: it always
/// creates and persists a job. Phase 2B-2 added one additive behavior: if
/// the request body includes a non-empty `job_type`, the route also
/// attempts `scheduler->schedule()` on the newly-created job and, only on
/// success, marks it Queued via JobService. A scheduling failure (unknown
/// job_type, scheduler at capacity) never fails the HTTP request -- the
/// job was genuinely created either way -- it is reported via a
/// `"scheduling"` object in the response body instead. See
/// docs/architecture/execution-model.md, "HTTP integration".
///
/// Phase 2B-3 makes cancel best-effort-propagate beyond the persisted
/// status: after `JobService::cancel_job()` succeeds, the route also
/// best-effort calls `scheduler->cancel()` (removes it from the dispatch
/// queue if still sitting there) and `worker_pool->request_cancellation()`
/// (signals cooperative cancellation if it is already executing). Neither
/// call's result affects the HTTP response -- the job's persisted
/// `Cancelled` status is the source of truth; these are just best-effort
/// attempts to also stop it from progressing further in the in-memory
/// dispatch/execution pipeline.
void register_job_routes(httplib::Server& server, const std::shared_ptr<services::JobService>& job_service,
                         const std::shared_ptr<engine::IScheduler>& scheduler,
                         const std::shared_ptr<engine::IWorkerPool>& worker_pool,
                         const std::shared_ptr<engine::IExecutionManager>& execution_manager,
                         const std::shared_ptr<infra::MetricsRegistry>& metrics);

}  // namespace flowforge::server
