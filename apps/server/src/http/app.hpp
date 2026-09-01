#pragma once

#include <httplib.h>

#include <chrono>
#include <functional>
#include <memory>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/engine/handler_registry.hpp"
#include "flowforge/engine/local_worker_pool.hpp"
#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/engine/retry_dispatcher.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/config.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/repository_factory.hpp"
#include "flowforge/result.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::server {

/// Wires together configuration, logging, metrics, persistence, services,
/// and HTTP routes into a runnable server. Kept separate from `main()` so
/// integration tests (apps/server/tests) can construct an `App`, listen on
/// an ephemeral port, and issue real HTTP requests against it without a
/// separate process.
///
/// Construction goes through `create()`, not a public constructor: picking
/// and connecting to a repository backend (persistence::create_repositories,
/// possibly PostgreSQL) is fallible -- an unreachable database must fail
/// startup with a clear error rather than the process either crashing
/// uncontrolled or silently falling back to in-memory persistence. See
/// docs/architecture/overview.md, "Server startup".
class App {
 public:
  [[nodiscard]] static Result<std::unique_ptr<App>> create(infra::AppConfig config);

  /// Blocks serving requests until `stop()` is called from another thread
  /// (or the process receives a signal wired up by main.cpp).
  void run();
  void stop();

  [[nodiscard]] const infra::AppConfig& config() const noexcept { return config_; }
  [[nodiscard]] httplib::Server& http_server() noexcept { return http_; }

 private:
  App(infra::AppConfig config, std::shared_ptr<infra::Logger> logger,
      std::shared_ptr<infra::MetricsRegistry> metrics, persistence::RepositoryBundle repositories,
      std::shared_ptr<engine::HandlerRegistry> handler_registry,
      std::shared_ptr<engine::LocalWorkerPool> worker_pool,
      std::shared_ptr<engine::PriorityScheduler> scheduler,
      std::shared_ptr<engine::RetryDispatcher> retry_dispatcher);

  void register_routes();

  infra::AppConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<persistence::IJobRepository> job_repository_;
  std::shared_ptr<persistence::IWorkflowRepository> workflow_repository_;
  std::shared_ptr<persistence::IWorkerRepository> worker_repository_;
  std::shared_ptr<engine::IExecutionManager> execution_manager_;
  std::shared_ptr<services::JobService> job_service_;
  std::shared_ptr<engine::HandlerRegistry> handler_registry_;
  std::shared_ptr<engine::LocalWorkerPool> worker_pool_;
  std::shared_ptr<engine::PriorityScheduler> scheduler_;
  std::shared_ptr<engine::RetryDispatcher> retry_dispatcher_;
  /// Cheap, non-blocking readiness signal for whichever persistence
  /// backend is active -- see `persistence::RepositoryBundle::
  /// check_database_health` and `GET /ready` (health_routes.hpp).
  std::function<bool()> database_health_check_;
  httplib::Server http_;
  std::chrono::steady_clock::time_point process_start_time_;
};

}  // namespace flowforge::server
