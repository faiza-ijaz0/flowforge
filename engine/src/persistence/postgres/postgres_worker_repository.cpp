#include "flowforge/persistence/postgres/postgres_worker_repository.hpp"

#include <pqxx/pqxx>

#include <optional>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_worker_repository";

constexpr std::string_view kSelectWorkerColumns =
    "id, hostname, status, extract(epoch from registered_at) AS registered_at_epoch, "
    "extract(epoch from last_heartbeat) AS last_heartbeat_epoch";

std::optional<domain::WorkerStatus> worker_status_from_string(std::string_view value) {
  if (value == "idle")
    return domain::WorkerStatus::Idle;
  if (value == "busy")
    return domain::WorkerStatus::Busy;
  if (value == "offline")
    return domain::WorkerStatus::Offline;
  return std::nullopt;
}

Result<domain::Worker> row_to_worker(const pqxx::row& row) {
  auto status = worker_status_from_string(row["status"].as<std::string>());
  if (!status) {
    return std::unexpected(make_error(ErrorCode::Database, "worker row has an unrecognized status value"));
  }
  domain::Worker worker(infra::WorkerId{row["id"].as<std::string>()}, row["hostname"].as<std::string>(),
                        from_epoch_seconds(row["registered_at_epoch"].as<double>()));
  worker.heartbeat(from_epoch_seconds(row["last_heartbeat_epoch"].as<double>()));
  worker.set_status(*status);
  return worker;
}

}  // namespace

PostgresWorkerRepository::PostgresWorkerRepository(std::shared_ptr<PgConnectionPool> pool,
                                                   std::shared_ptr<infra::Logger> logger,
                                                   std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresWorkerRepository::insert(const domain::Worker& worker) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    txn.exec_params(
        "INSERT INTO workers (id, hostname, status, registered_at, last_heartbeat) "
        "VALUES ($1, $2, $3, to_timestamp($4), to_timestamp($5))",
        pqxx::params{worker.id().value(), worker.hostname(), std::string(domain::to_string(worker.status())),
                     to_epoch_seconds(worker.registered_at()), to_epoch_seconds(worker.last_heartbeat())});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_worker_inserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "insert failed",
                   {{.key = "worker_id", .value = worker.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "worker_repository.insert"));
  }
}

Result<domain::Worker> PostgresWorkerRepository::find_by_id(const infra::WorkerId& id) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectWorkerColumns) + " FROM workers WHERE id = $1",
                        pqxx::params{id.value()});
    txn.commit();
    if (result.empty()) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "worker with id '" + id.value() + "' was not found"));
    }
    return row_to_worker(result[0]);
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_id failed",
                   {{.key = "worker_id", .value = id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "worker_repository.find_by_id"));
  }
}

Result<std::vector<domain::Worker>> PostgresWorkerRepository::list() const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params("SELECT " + std::string(kSelectWorkerColumns) +
                                  " FROM workers ORDER BY registered_at ASC, id ASC");
    txn.commit();

    std::vector<domain::Worker> workers;
    workers.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      auto worker = row_to_worker(row);
      if (!worker) {
        return std::unexpected(worker.error());
      }
      workers.push_back(std::move(*worker));
    }
    return workers;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "worker_repository.list"));
  }
}

Result<void> PostgresWorkerRepository::update(const domain::Worker& worker) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params(
        "UPDATE workers SET hostname = $2, status = $3, last_heartbeat = to_timestamp($4) "
        "WHERE id = $1",
        pqxx::params{worker.id().value(), worker.hostname(), std::string(domain::to_string(worker.status())),
                     to_epoch_seconds(worker.last_heartbeat())});
    txn.commit();
    if (result.affected_rows() == 0) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "worker with id '" + worker.id().value() + "' was not found"));
    }
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_worker_updates_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(kComponent, "update failed",
                   {{.key = "worker_id", .value = worker.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "worker_repository.update"));
  }
}

}  // namespace flowforge::persistence::postgres
