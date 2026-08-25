#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/infra/metrics.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::server {

/// Registers the /api/v1/jobs endpoints:
///   POST   /api/v1/jobs             create a job
///   GET    /api/v1/jobs             list jobs (?limit=&offset=)
///   GET    /api/v1/jobs/{id}        fetch a single job
///   POST   /api/v1/jobs/{id}/cancel cancel a job
///
/// Handlers are a thin translation of HTTP <-> JobService; all business
/// logic lives in JobService so it stays testable without HTTP.
void register_job_routes(httplib::Server& server, const std::shared_ptr<services::JobService>& job_service,
                         const std::shared_ptr<infra::MetricsRegistry>& metrics);

}  // namespace flowforge::server
