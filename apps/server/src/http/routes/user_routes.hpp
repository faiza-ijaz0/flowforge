#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/persistence/user_repository.hpp"

namespace flowforge::server {

/// Registers the read-only Users API (Phase 3H -- closes the persistence
/// gap documented in docs/architecture/phase-3g-audit.md §2.4):
///
///   GET /api/v1/users   ?limit=&offset=
///
/// A thin HTTP <-> `persistence::IUserRepository` translation, identical
/// in shape to `register_product_routes`/`register_category_routes`. Users
/// themselves are written only by `handlers::UserProcessHandler` at
/// job-execution time -- there is no `POST /api/v1/users`; creating one is
/// always "confirm an import" (`POST /api/v1/process/confirm` or
/// `POST /api/v1/workloads/user-imports`), never a direct write through
/// this router.
void register_user_routes(httplib::Server& server,
                          const std::shared_ptr<persistence::IUserRepository>& user_repository);

}  // namespace flowforge::server
