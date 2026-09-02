#include "flowforge/persistence/repository_factory.hpp"

#include "flowforge/persistence/in_memory_repositories.hpp"

#ifdef FLOWFORGE_WITH_POSTGRES
#include "flowforge/persistence/postgres/connection_pool.hpp"
#include "flowforge/persistence/postgres/postgres_execution_repository.hpp"
#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "flowforge/persistence/postgres/postgres_worker_repository.hpp"
#include "flowforge/persistence/postgres/postgres_workflow_repository.hpp"
#include "flowforge/persistence/postgres/postgres_workload_repository.hpp"
#endif

namespace flowforge::persistence {

Result<RepositoryBundle> create_repositories(const infra::AppConfig& config,
                                             std::shared_ptr<infra::Logger> logger,
                                             std::shared_ptr<infra::MetricsRegistry> metrics) {
  if (config.database_url.empty()) {
    if (logger) {
      logger->info("repository_factory", "using in-memory repositories (FLOWFORGE_DATABASE_URL is not set)",
                   {});
    }
    RepositoryBundle bundle;
    bundle.jobs = std::make_shared<InMemoryJobRepository>();
    bundle.workflows = std::make_shared<InMemoryWorkflowRepository>();
    bundle.workers = std::make_shared<InMemoryWorkerRepository>();
    bundle.executions = std::make_shared<InMemoryExecutionRepository>();
    bundle.workloads = std::make_shared<InMemoryWorkloadRepository>();
    bundle.check_database_health = [] { return true; };
    return bundle;
  }

#ifndef FLOWFORGE_WITH_POSTGRES
  (void)metrics;
  return std::unexpected(make_error(
      ErrorCode::Configuration,
      "FLOWFORGE_DATABASE_URL is set but this build was compiled without PostgreSQL support "
      "(FLOWFORGE_WITH_POSTGRES was OFF at configure time). Either unset FLOWFORGE_DATABASE_URL to use "
      "in-memory persistence, or rebuild with PostgreSQL (libpq) available."));
#else
  if (logger) {
    logger->info("repository_factory", "using PostgreSQL-backed repositories",
                 {{.key = "pool_size", .value = std::to_string(config.database_pool_size)}});
  }

  postgres::PgPoolConfig pool_config{
      .connection_string = config.database_url,
      .pool_size = config.database_pool_size,
  };
  auto pool = postgres::PgConnectionPool::create(pool_config, logger, metrics);
  if (!pool) {
    return std::unexpected(pool.error());
  }

  RepositoryBundle bundle;
  bundle.jobs = std::make_shared<postgres::PostgresJobRepository>(*pool, logger, metrics);
  bundle.workflows = std::make_shared<postgres::PostgresWorkflowRepository>(*pool, logger, metrics);
  bundle.workers = std::make_shared<postgres::PostgresWorkerRepository>(*pool, logger, metrics);
  bundle.executions = std::make_shared<postgres::PostgresExecutionRepository>(*pool, logger, metrics);
  bundle.workloads = std::make_shared<postgres::PostgresWorkloadRepository>(*pool, logger, metrics);
  bundle.check_database_health = [pool = *pool] { return pool->is_available(); };
  return bundle;
#endif
}

}  // namespace flowforge::persistence
