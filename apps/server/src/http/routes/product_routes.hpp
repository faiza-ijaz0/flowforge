#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/persistence/product_repository.hpp"

namespace flowforge::server {

/// Registers the read-only Products API (Phase 3E -- see
/// docs/architecture/product-processing.md):
///
///   GET /api/v1/products   ?limit=&offset=
///
/// A thin HTTP <-> `persistence::IProductRepository` translation, exactly
/// like every other route file in this codebase -- no business logic
/// here (mirrors `register_workload_routes`'s read endpoints). Products
/// themselves are written only by `handlers::ProductProcessHandler` at
/// job-execution time (see its class comment) -- there is no `POST
/// /api/v1/products`; creating one is always "confirm an import" (`POST
/// /api/v1/process/confirm`), never a direct write through this router.
void register_product_routes(httplib::Server& server,
                             const std::shared_ptr<persistence::IProductRepository>& product_repository);

}  // namespace flowforge::server
