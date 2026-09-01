#include "flowforge/persistence/postgres/postgres_workflow_repository.hpp"

#include <pqxx/pqxx>

#include <map>
#include <optional>
#include <sstream>

#include "flowforge/persistence/postgres/error_mapping.hpp"
#include "pg_time.hpp"

namespace flowforge::persistence::postgres {

namespace {

constexpr std::string_view kComponent = "postgres_workflow_repository";

std::optional<domain::WorkflowStatus> workflow_status_from_string(std::string_view value) {
  if (value == "pending")
    return domain::WorkflowStatus::Pending;
  if (value == "running")
    return domain::WorkflowStatus::Running;
  if (value == "succeeded")
    return domain::WorkflowStatus::Succeeded;
  if (value == "failed")
    return domain::WorkflowStatus::Failed;
  if (value == "cancelled")
    return domain::WorkflowStatus::Cancelled;
  return std::nullopt;
}

/// Formats ids as a PostgreSQL array literal ("{a,b,c}") for binding as a
/// single `$n::uuid[]` parameter. This is still a fully parameterized
/// query -- the array literal is bound as one opaque data value, never
/// spliced into the SQL text -- it just lets a single query answer "give
/// me the steps/dependencies for this whole page of workflows" instead of
/// one query per workflow (see the N+1 note on load_steps_by_workflow).
std::string to_uuid_array_literal(const std::vector<std::string>& ids) {
  std::ostringstream out;
  out << '{';
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << ids[i];
  }
  out << '}';
  return out.str();
}

/// Loads the steps (in position order) and dependency edges for every
/// workflow id in `workflow_ids`, grouped by workflow_id. Used by both
/// find_by_id() (one id) and list() (a page of ids) so neither path issues
/// a per-workflow query for its steps -- exactly two queries regardless of
/// how many workflow ids are passed in.
Result<std::map<std::string, std::vector<domain::WorkflowStep>>> load_steps_by_workflow(
    pqxx::work& txn, const std::vector<std::string>& workflow_ids) {
  if (workflow_ids.empty()) {
    return std::map<std::string, std::vector<domain::WorkflowStep>>{};
  }
  const std::string ids_array = to_uuid_array_literal(workflow_ids);

  auto steps_result = txn.exec_params(
      "SELECT id, workflow_id, job_id, name FROM workflow_steps WHERE workflow_id = ANY($1::uuid[]) "
      "ORDER BY workflow_id, position ASC",
      pqxx::params{ids_array});

  std::map<std::string, std::vector<domain::WorkflowStep>> steps_by_workflow;
  for (const auto& row : steps_result) {
    domain::WorkflowStep step;
    step.id = infra::WorkflowStepId{row["id"].as<std::string>()};
    step.job_id = infra::JobId{row["job_id"].as<std::string>()};
    step.name = row["name"].as<std::string>();
    steps_by_workflow[row["workflow_id"].as<std::string>()].push_back(std::move(step));
  }

  auto deps_result = txn.exec_params(
      "SELECT step_id, depends_on_step_id FROM workflow_step_dependencies WHERE step_id IN "
      "(SELECT id FROM workflow_steps WHERE workflow_id = ANY($1::uuid[]))",
      pqxx::params{ids_array});

  std::map<std::string, std::vector<infra::WorkflowStepId>> depends_on_by_step;
  for (const auto& row : deps_result) {
    depends_on_by_step[row["step_id"].as<std::string>()].push_back(
        infra::WorkflowStepId{row["depends_on_step_id"].as<std::string>()});
  }

  for (auto& [workflow_id, steps] : steps_by_workflow) {
    for (auto& step : steps) {
      if (auto it = depends_on_by_step.find(step.id.value()); it != depends_on_by_step.end()) {
        step.depends_on = it->second;
      }
    }
  }
  return steps_by_workflow;
}

std::vector<domain::WorkflowStep> steps_for(
    const std::map<std::string, std::vector<domain::WorkflowStep>>& steps_by_workflow,
    const std::string& id) {
  auto it = steps_by_workflow.find(id);
  return it != steps_by_workflow.end() ? it->second : std::vector<domain::WorkflowStep>{};
}

Result<domain::Workflow> build_workflow(const pqxx::row& row, std::vector<domain::WorkflowStep> steps) {
  auto status = workflow_status_from_string(row["status"].as<std::string>());
  if (!status) {
    return std::unexpected(make_error(ErrorCode::Database, "workflow row has an unrecognized status value"));
  }
  domain::Workflow workflow(infra::WorkflowId{row["id"].as<std::string>()}, row["name"].as<std::string>(),
                            std::move(steps), from_epoch_seconds(row["created_at_epoch"].as<double>()));
  workflow.transition_to(*status, from_epoch_seconds(row["updated_at_epoch"].as<double>()));
  return workflow;
}

constexpr std::string_view kSelectWorkflowColumns =
    "id, name, status, extract(epoch from created_at) AS created_at_epoch, extract(epoch from updated_at) AS "
    "updated_at_epoch";

}  // namespace

PostgresWorkflowRepository::PostgresWorkflowRepository(std::shared_ptr<PgConnectionPool> pool,
                                                       std::shared_ptr<infra::Logger> logger,
                                                       std::shared_ptr<infra::MetricsRegistry> metrics)
    : pool_(std::move(pool)), logger_(std::move(logger)), metrics_(std::move(metrics)) {}

Result<void> PostgresWorkflowRepository::insert(const domain::Workflow& workflow) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);

    txn.exec_params(
        "INSERT INTO workflows (id, name, status, created_at, updated_at) "
        "VALUES ($1, $2, $3, to_timestamp($4), to_timestamp($5))",
        pqxx::params{workflow.id().value(), workflow.name(),
                     std::string(domain::to_string(workflow.status())),
                     to_epoch_seconds(workflow.created_at()), to_epoch_seconds(workflow.updated_at())});

    int position = 0;
    for (const auto& step : workflow.steps()) {
      txn.exec_params(
          "INSERT INTO workflow_steps (id, workflow_id, job_id, name, position) VALUES ($1, $2, $3, $4, $5)",
          pqxx::params{step.id.value(), workflow.id().value(), step.job_id.value(), step.name, position});
      ++position;
    }
    // Dependency edges are inserted only after every step row exists (all
    // steps in the workflow are visible to each other within this same
    // transaction), since workflow_step_dependencies' foreign keys point
    // at workflow_steps.id.
    for (const auto& step : workflow.steps()) {
      for (const auto& depends_on : step.depends_on) {
        txn.exec_params(
            "INSERT INTO workflow_step_dependencies (step_id, depends_on_step_id) VALUES ($1, $2)",
            pqxx::params{step.id.value(), depends_on.value()});
      }
    }

    txn.commit();
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_workflow_inserts_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(
        kComponent, "insert failed",
        {{.key = "workflow_id", .value = workflow.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workflow_repository.insert"));
  }
}

Result<domain::Workflow> PostgresWorkflowRepository::find_by_id(const infra::WorkflowId& id) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto result =
        txn.exec_params("SELECT " + std::string(kSelectWorkflowColumns) + " FROM workflows WHERE id = $1",
                        pqxx::params{id.value()});
    if (result.empty()) {
      txn.commit();
      return std::unexpected(
          make_error(ErrorCode::NotFound, "workflow with id '" + id.value() + "' was not found"));
    }

    auto steps_by_workflow = load_steps_by_workflow(txn, std::vector<std::string>{id.value()});
    txn.commit();
    if (!steps_by_workflow) {
      return std::unexpected(steps_by_workflow.error());
    }
    return build_workflow(result[0], steps_for(*steps_by_workflow, id.value()));
  } catch (const std::exception& e) {
    logger_->error(kComponent, "find_by_id failed",
                   {{.key = "workflow_id", .value = id.value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workflow_repository.find_by_id"));
  }
}

Result<std::vector<domain::Workflow>> PostgresWorkflowRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    auto workflows_result =
        txn.exec_params("SELECT " + std::string(kSelectWorkflowColumns) +
                            " FROM workflows ORDER BY created_at ASC, id ASC "
                            "LIMIT $1 OFFSET $2",
                        pqxx::params{static_cast<long long>(limit), static_cast<long long>(offset)});

    std::vector<std::string> ids;
    ids.reserve(static_cast<std::size_t>(workflows_result.size()));
    for (const auto& row : workflows_result) {
      ids.push_back(row["id"].as<std::string>());
    }

    auto steps_by_workflow = load_steps_by_workflow(txn, ids);
    txn.commit();
    if (!steps_by_workflow) {
      return std::unexpected(steps_by_workflow.error());
    }

    std::vector<domain::Workflow> workflows;
    workflows.reserve(static_cast<std::size_t>(workflows_result.size()));
    for (const auto& row : workflows_result) {
      auto workflow = build_workflow(row, steps_for(*steps_by_workflow, row["id"].as<std::string>()));
      if (!workflow) {
        return std::unexpected(workflow.error());
      }
      workflows.push_back(std::move(*workflow));
    }
    return workflows;
  } catch (const std::exception& e) {
    logger_->error(kComponent, "list failed", {{.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workflow_repository.list"));
  }
}

Result<void> PostgresWorkflowRepository::update(const domain::Workflow& workflow) {
  try {
    auto conn = pool_->acquire();
    if (!conn) {
      return std::unexpected(conn.error());
    }
    pqxx::work txn(**conn);
    // Steps/dependencies are immutable once a workflow is created (the
    // domain type exposes no mutator for them -- see domain::Workflow) so
    // update() only ever needs to touch the workflows row itself.
    auto result = txn.exec_params(
        "UPDATE workflows SET name = $2, status = $3, updated_at = to_timestamp($4) "
        "WHERE id = $1",
        pqxx::params{workflow.id().value(), workflow.name(),
                     std::string(domain::to_string(workflow.status())),
                     to_epoch_seconds(workflow.updated_at())});
    txn.commit();
    if (result.affected_rows() == 0) {
      return std::unexpected(
          make_error(ErrorCode::NotFound, "workflow with id '" + workflow.id().value() + "' was not found"));
    }
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_workflow_updates_total");
    }
    return {};
  } catch (const std::exception& e) {
    logger_->error(
        kComponent, "update failed",
        {{.key = "workflow_id", .value = workflow.id().value()}, {.key = "error", .value = e.what()}});
    if (metrics_) {
      metrics_->increment_counter("flowforge_db_errors_total");
    }
    return std::unexpected(map_exception(e, "workflow_repository.update"));
  }
}

}  // namespace flowforge::persistence::postgres
