#include "flowforge/persistence/postgres/postgres_category_repository.hpp"

#include <pqxx/pqxx>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_category_repository";

constexpr std::string_view kSelectCategoryColumns =
    "id, name, slug, description, parent_slug, job_id, "
    "extract(epoch from created_at) AS created_at_epoch, extract(epoch from updated_at) AS updated_at_epoch";

domain::Category row_to_category(const pqxx::row& row) {
  domain::Category category{
      .id = infra::CategoryId{row["id"].as<std::string>()},
      .name = row["name"].as<std::string>(),
      .slug = row["slug"].as<std::string>(),
      .description = row["description"].as<std::optional<std::string>>(),
      .parent_slug = row["parent_slug"].as<std::optional<std::string>>(),
      .job_id = std::nullopt,
      .created_at = from_epoch_seconds(row["created_at_epoch"].as<double>()),
      .updated_at = from_epoch_seconds(row["updated_at_epoch"].as<double>()),
  };
  if (auto job_id = row["job_id"].as<std::optional<std::string>>(); job_id.has_value()) {
    category.job_id = infra::JobId{*job_id};
  }
  return category;
}

}  // namespace

PostgresCategoryRepository::PostgresCategoryRepository(std::shared_ptr<PgConnectionPool> pool,
                                                       std::shared_ptr<infra::Logger> logger,
                                                       std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresCategoryRepository::upsert(const infra::JobId& job_id,
                                                const domain::NormalizedCategoryRecord& record) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(
        "INSERT INTO categories (name, slug, description, parent_slug, job_id) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (slug) DO UPDATE SET "
        "name = EXCLUDED.name, description = EXCLUDED.description, "
        "parent_slug = EXCLUDED.parent_slug, job_id = EXCLUDED.job_id, updated_at = now()",
        pqxx::params{record.name, record.slug, record.description, record.parent_slug, job_id.value()});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_category_upserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "upsert failed",
                   {{.key = "slug", .value = record.slug}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "category_repository.upsert"));
  }
}

Result<std::optional<domain::Category>> PostgresCategoryRepository::find_by_slug(
    const std::string& slug) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectCategoryColumns) + " FROM categories WHERE slug = $1",
                        pqxx::params{slug});
    txn.commit();
    if (result.empty()) {
      return std::optional<domain::Category>(std::nullopt);
    }
    return std::optional<domain::Category>(row_to_category(result[0]));
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_slug failed",
                   {{.key = "slug", .value = slug}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "category_repository.find_by_slug"));
  }
}

Result<std::vector<domain::Category>> PostgresCategoryRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectCategoryColumns) +
                            " FROM categories ORDER BY created_at ASC, id ASC LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});
    txn.commit();

    std::vector<domain::Category> categories;
    categories.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      categories.push_back(row_to_category(row));
    }
    return categories;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "category_repository.list"));
  }
}

Result<std::size_t> PostgresCategoryRepository::count() const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec("SELECT count(*) FROM categories");
    txn.commit();
    return result[0][0].as<std::size_t>();
  } catch (const std::exception& e) {
    logger_->error(kComponent, "count failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "category_repository.count"));
  }
}

}  // namespace flowforge::persistence::postgres
