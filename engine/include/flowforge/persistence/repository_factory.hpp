#pragma once

#include <functional>
#include <memory>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/infra/config.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/persistence/product_repository.hpp"
#include "flowforge/persistence/worker_repository.hpp"
#include "flowforge/persistence/workflow_repository.hpp"
#include "flowforge/persistence/workload_repository.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// The repositories the application needs, bundled together so callers
/// get an all-or-nothing construction result instead of several separate
/// fallible steps to sequence by hand.
struct RepositoryBundle {
  std::shared_ptr<IJobRepository> jobs;
  std::shared_ptr<IWorkflowRepository> workflows;
  std::shared_ptr<IWorkerRepository> workers;
  std::shared_ptr<engine::IExecutionManager> executions;
  std::shared_ptr<IWorkloadRepository> workloads;
  std::shared_ptr<IProductRepository> products;

  /// Cheap, non-blocking readiness signal for whichever persistence
  /// backend is actually active (Phase 2B-5) -- always returns `true` for
  /// in-memory repositories, and delegates to
  /// `postgres::PgConnectionPool::is_available()` for the PostgreSQL
  /// backend. Deliberately a `std::function`, not a new interface type:
  /// this is a one-off, composition-root-local wiring concern (`GET
  /// /ready` needs *some* answer to "is the active backend usable", not a
  /// Postgres-specific concept threaded through `IJobRepository` or a new
  /// abstraction every repository implementation would need to satisfy).
  std::function<bool()> check_database_health;
};

/// The composition root for persistence: the one place that decides
/// in-memory vs. PostgreSQL and constructs the concrete repositories
/// accordingly. Nothing above this (JobService, HTTP routes, App) ever
/// names a concrete repository type -- see docs/architecture/overview.md,
/// "PostgreSQL persistence architecture".
///
/// The rule is deliberately simple and config-driven, never silent:
///   - `config.database_url` empty  -> in-memory repositories (this is
///     the existing Phase 1 default for local development/tests).
///   - `config.database_url` set    -> PostgreSQL-backed repositories.
///     `AppConfig::load` already requires this to be set outside
///     development/test, so staging/production always take this path.
///     If PostgreSQL is unreachable, this returns an Error rather than
///     falling back to in-memory -- callers (App::create) are expected to
///     fail startup on that Error, not paper over it.
/// There is no environment-name branch here on purpose: "development" with
/// FLOWFORGE_DATABASE_URL set (e.g. via docker-compose, or a developer
/// opting into a local PostgreSQL) goes through the real PostgreSQL path
/// too, so that path gets exercised outside of staging/production.
[[nodiscard]] Result<RepositoryBundle> create_repositories(const infra::AppConfig& config,
                                                           std::shared_ptr<infra::Logger> logger,
                                                           std::shared_ptr<infra::MetricsRegistry> metrics);

}  // namespace flowforge::persistence
