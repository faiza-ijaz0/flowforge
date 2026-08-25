#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/persistence/workflow_repository.hpp"

namespace flowforge::server {

/// Registers GET /api/v1/workflows: lists workflows from the (currently
/// in-memory, currently always-empty) workflow repository. Workflow
/// creation/execution is Phase 2 work -- see docs/architecture/overview.md
/// -- so this endpoint exists to establish the route/response shape early
/// without pretending workflows can be created yet.
void register_workflow_routes(httplib::Server& server,
                              const std::shared_ptr<persistence::IWorkflowRepository>& repository);

}  // namespace flowforge::server
