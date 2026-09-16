#pragma once

#include <httplib.h>

#include <memory>

#include "flowforge/persistence/category_repository.hpp"

namespace flowforge::server {

/// Registers the read-only Categories API (Phase 3F -- see
/// docs/architecture/category-processing.md):
///
///   GET /api/v1/categories   ?limit=&offset=
///
/// A thin HTTP <-> `persistence::ICategoryRepository` translation, exactly
/// like `register_product_routes`'s read endpoint -- no business logic
/// here. Categories are written only by `handlers::CategoryProcessHandler`
/// at job-execution time -- there is no `POST /api/v1/categories`;
/// creating one is always "confirm an import" (`POST
/// /api/v1/process/confirm`), never a direct write through this router.
void register_category_routes(httplib::Server& server,
                              const std::shared_ptr<persistence::ICategoryRepository>& category_repository);

}  // namespace flowforge::server
