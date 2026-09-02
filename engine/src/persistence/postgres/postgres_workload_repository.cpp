#include "flowforge/persistence/postgres/postgres_workload_repository.hpp"

#include <pqxx/pqxx>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_workload_repository";

constexpr std::string_view kSelectWorkloadColumns =
    "id, type, total_items, extract(epoch from created_at) AS created_at_epoch, extract(epoch from "
    "updated_at) AS updated_at_epoch";

domain::Workload row_to_workload(const pqxx::row& row) {
  return domain::Workload::restore(infra::WorkloadId{row["id"].as<std::string>()},
                                   row["type"].as<std::string>(), row["total_items"].as<std::size_t>(),
                                   from_epoch_seconds(row["created_at_epoch"].as<double>()),
                                   from_epoch_seconds(row["updated_at_epoch"].as<double>()));
}

}  // namespace

PostgresWorkloadRepository::PostgresWorkloadRepository(std::shared_ptr<PgConnectionPool> pool,
                                                       std::shared_ptr<infra::Logger> logger,
                                                       std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresWorkloadRepository::insert(const domain::Workload& workload) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(
        "INSERT INTO workloads (id, type, total_items, created_at, updated_at) "
        "VALUES ($1, $2, $3, to_timestamp($4), to_timestamp($5))",
        pqxx::params{workload.id().value(), workload.type(), static_cast<long long>(workload.total_items()),
                     to_epoch_seconds(workload.created_at()), to_epoch_seconds(workload.updated_at())});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_workload_inserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(
        kComponent, "insert failed",
        {{.key = "workload_id", .value = workload.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workload_repository.insert"));
  }
}

Result<domain::Workload> PostgresWorkloadRepository::find_by_id(const infra::WorkloadId& id) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectWorkloadColumns) + " FROM workloads WHERE id = $1",
                        pqxx::params{id.value()});
    txn.commit();
    if (result.empty()) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "workload with id '" + id.value() + "' was not found"));
    }
    return row_to_workload(result[0]);
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_id failed",
                   {{.key = "workload_id", .value = id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workload_repository.find_by_id"));
  }
}

Result<std::vector<domain::Workload>> PostgresWorkloadRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectWorkloadColumns) +
                            " FROM workloads ORDER BY created_at ASC, id ASC LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});
    txn.commit();

    std::vector<domain::Workload> workloads;
    workloads.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      workloads.push_back(row_to_workload(row));
    }
    return workloads;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workload_repository.list"));
  }
}

}  // namespace flowforge::persistence::postgres
