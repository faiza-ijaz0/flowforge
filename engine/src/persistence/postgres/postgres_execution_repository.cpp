#include "flowforge/persistence/postgres/postgres_execution_repository.hpp"

#include <pqxx/pqxx>

#include <optional>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_execution_repository";

constexpr pqxx::zview kInsertExecution =
    "INSERT INTO job_attempts (id, job_id, worker_id, attempt_number, outcome, started_at, finished_at, "
    "error_message) "
    "VALUES ($1, $2, $3, $4, $5, to_timestamp($6), to_timestamp($7), $8)";

constexpr std::string_view kSelectExecutionColumns =
    "id, job_id, worker_id, attempt_number, outcome, extract(epoch from started_at) AS started_at_epoch, "
    "extract(epoch from finished_at) AS finished_at_epoch, error_message";

Result<domain::Execution> row_to_execution(const pqxx::row& row) {
  auto outcome = domain::execution_outcome_from_string(row["outcome"].as<std::string>());
  if (!outcome) {
    return std::unexpected(
        make_error(ErrorCode::Database, "job_attempts row has an unrecognized outcome value"));
  }

  domain::Execution execution;
  execution.id = infra::ExecutionId{row["id"].as<std::string>()};
  execution.job_id = infra::JobId{row["job_id"].as<std::string>()};
  if (auto worker_id = row["worker_id"].as<std::optional<std::string>>(); worker_id.has_value()) {
    execution.worker_id = infra::WorkerId{*worker_id};
  }
  execution.attempt_number = row["attempt_number"].as<std::uint32_t>();
  execution.outcome = *outcome;
  execution.started_at = from_epoch_seconds(row["started_at_epoch"].as<double>());
  if (auto finished_at_epoch = row["finished_at_epoch"].as<std::optional<double>>();
      finished_at_epoch.has_value()) {
    execution.finished_at = from_epoch_seconds(*finished_at_epoch);
  }
  execution.error_message = row["error_message"].as<std::optional<std::string>>();
  return execution;
}

}  // namespace

PostgresExecutionRepository::PostgresExecutionRepository(std::shared_ptr<PgConnectionPool> pool,
                                                         std::shared_ptr<infra::Logger> logger,
                                                         std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresExecutionRepository::record(const domain::Execution& execution) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);

    const std::optional<std::string> worker_id =
        execution.worker_id ? std::optional<std::string>(execution.worker_id->value()) : std::nullopt;
    const std::optional<double> finished_at_epoch =
        execution.finished_at ? std::optional<double>(to_epoch_seconds(*execution.finished_at))
                              : std::nullopt;

    txn.exec_params(
        kInsertExecution,
        pqxx::params{execution.id.value(), execution.job_id.value(), worker_id, execution.attempt_number,
                     std::string(domain::to_string(execution.outcome)),
                     to_epoch_seconds(execution.started_at), finished_at_epoch, execution.error_message});
    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_execution_inserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(
        kComponent, "record failed",
        {{.key = "execution_id", .value = execution.id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "execution_repository.record"));
  }
}

Result<std::vector<domain::Execution>> PostgresExecutionRepository::history_for(
    const infra::JobId& job_id) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result = txn.exec_params("SELECT " + std::string(kSelectExecutionColumns) +
                                      " FROM job_attempts WHERE job_id = $1 ORDER BY attempt_number ASC",
                                  pqxx::params{job_id.value()});
    txn.commit();

    std::vector<domain::Execution> executions;
    executions.reserve(static_cast<std::size_t>(result.size()));
    for (const auto& row : result) {
      auto execution = row_to_execution(row);
      if (!execution) {
        return std::unexpected(execution.error());
      }
      executions.push_back(std::move(*execution));
    }
    return executions;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "history_for failed",
                   {{.key = "job_id", .value = job_id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "execution_repository.history_for"));
  }
}

}  // namespace flowforge::persistence::postgres
