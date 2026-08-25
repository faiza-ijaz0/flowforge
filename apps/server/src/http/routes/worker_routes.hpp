#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/persistence/worker_repository.hpp"

namespace flowforge::server {

/// Registers GET /api/v1/workers: lists registered workers from the
/// (currently in-memory, currently always-empty) worker repository.
/// Worker process registration/heartbeating is Phase 2 work.
void register_worker_routes(httplib::Server& server,
                            const std::shared_ptr<persistence::IWorkerRepository>& repository);

}  // namespace flowforge::server
