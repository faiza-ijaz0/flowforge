#include "flowforge/persistence/postgres/postgres_product_repository.hpp"

#include <pqxx/pqxx>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_product_repository";

constexpr std::string_view kSelectProductColumns =
    "id, sku, name, price, currency, category, description, stock_quantity, job_id, "
    "extract(epoch from created_at) AS created_at_epoch, extract(epoch from updated_at) AS updated_at_epoch";

domain::Product row_to_product(const pqxx::row& row) {
  domain::Product product{
      .id = infra::ProductId{row["id"].as<std::string>()},
      .sku = row["sku"].as<std::string>(),
      .name = row["name"].as<std::string>(),
      .price = row["price"].as<double>(),
      .currency = row["currency"].as<std::string>(),
      .category = row["category"].as<std::optional<std::string>>(),
      .description = row["description"].as<std::optional<std::string>>(),
      .stock_quantity = row["stock_quantity"].as<std::int64_t>(),
      .job_id = std::nullopt,
      .created_at = from_epoch_seconds(row["created_at_epoch"].as<double>()),
      .updated_at = from_epoch_seconds(row["updated_at_epoch"].as<double>()),
  };
  if (auto job_id = row["job_id"].as<std::optional<std::string>>(); job_id.has_value()) {
    product.job_id = infra::JobId{*job_id};
  }
  return product;
}

}  // namespace

PostgresProductRepository::PostgresProductRepository(std::shared_ptr<PgConnectionPool> pool,
                                                     std::shared_ptr<infra::Logger> logger,
                                                     std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresProductRepository::upsert(const infra::JobId& job_id,
                                               const domain::NormalizedProductRecord& record) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(
        "INSERT INTO products (sku, name, price, currency, category, description, stock_quantity, job_id) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
        "ON CONFLICT (sku) DO UPDATE SET "
        "name = EXCLUDED.name, price = EXCLUDED.price, currency = EXCLUDED.currency, "
        "category = EXCLUDED.category, description = EXCLUDED.description, "
        "stock_quantity = EXCLUDED.stock_quantity, job_id = EXCLUDED.job_id, updated_at = now()",
        pqxx::params{record.sku, record.name, record.price, record.currency, record.category,
                     record.description, record.stock_quantity, job_id.value()});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_product_upserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "upsert failed",
                   {{.key = "sku", .value = record.sku}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "product_repository.upsert"));
  }
}

Result<std::optional<domain::Product>> PostgresProductRepository::find_by_sku(const std::string& sku) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(
        "SELECT " + std::string(kSelectProductColumns) + " FROM products WHERE sku = $1", pqxx::params{sku});
    txn.commit();
    if (result.empty()) {
      return std::optional<domain::Product>(std::nullopt);
    }
    return std::optional<domain::Product>(row_to_product(result[0]));
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_sku failed",
                   {{.key = "sku", .value = sku}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "product_repository.find_by_sku"));
  }
}

Result<std::vector<domain::Product>> PostgresProductRepository::list(std::size_t limit,
                                                                     std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectProductColumns) +
                            " FROM products ORDER BY created_at ASC, id ASC LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});
    txn.commit();

    std::vector<domain::Product> products;
    products.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      products.push_back(row_to_product(row));
    }
    return products;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "product_repository.list"));
  }
}

Result<std::size_t> PostgresProductRepository::count() const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec("SELECT count(*) FROM products");
    txn.commit();
    return result[0][0].as<std::size_t>();
  } catch (const std::exception& e) {
    logger_->error(kComponent, "count failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "product_repository.count"));
  }
}

}  // namespace flowforge::persistence::postgres
