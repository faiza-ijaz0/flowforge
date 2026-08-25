#pragma once

#include <httplib.h>

#include <chrono>
#include <memory>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/config.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/infra/metrics.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"
#include "flowforge/services/job_service.hpp"

namespace flowforge::server {

/// Wires together configuration, logging, metrics, persistence, services,
/// and HTTP routes into a runnable server. Kept separate from `main()` so
/// integration tests (apps/server/tests) can construct an `App`, listen on
/// an ephemeral port, and issue real HTTP requests against it without a
/// separate process.
class App {
 public:
  explicit App(infra::AppConfig config);

  /// Blocks serving requests until `stop()` is called from another thread
  /// (or the process receives a signal wired up by main.cpp).
  void run();
  void stop();

  [[nodiscard]] const infra::AppConfig& config() const noexcept { return config_; }
  [[nodiscard]] httplib::Server& http_server() noexcept { return http_; }

 private:
  void register_routes();

  infra::AppConfig config_;
  std::shared_ptr<infra::Logger> logger_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::MetricsRegistry> metrics_;
  std::shared_ptr<persistence::InMemoryJobRepository> job_repository_;
  std::shared_ptr<persistence::InMemoryWorkflowRepository> workflow_repository_;
  std::shared_ptr<persistence::InMemoryWorkerRepository> worker_repository_;
  std::shared_ptr<services::JobService> job_service_;
  httplib::Server http_;
  std::chrono::steady_clock::time_point process_start_time_;
};

}  // namespace flowforge::server
