#include "http/app.hpp"

#include <chrono>

#include "http/routes/health_routes.hpp"
#include "http/routes/job_routes.hpp"
#include "http/routes/worker_routes.hpp"
#include "http/routes/workflow_routes.hpp"

namespace flowforge::server {

App::App(infra::AppConfig config)
    : config_(std::move(config)),
      logger_(infra::make_logger(config_.log_level, config_.structured_logging)),
      clock_(infra::make_system_clock()),
      metrics_(infra::make_in_memory_metrics_registry()),
      job_repository_(std::make_shared<persistence::InMemoryJobRepository>()),
      workflow_repository_(std::make_shared<persistence::InMemoryWorkflowRepository>()),
      worker_repository_(std::make_shared<persistence::InMemoryWorkerRepository>()),
      job_service_(std::make_shared<services::JobService>(job_repository_, clock_, logger_)),
      process_start_time_(std::chrono::steady_clock::now()) {
  register_routes();
}

void App::register_routes() {
  register_health_routes(http_, metrics_, config_, process_start_time_);
  register_job_routes(http_, job_service_, metrics_);
  register_workflow_routes(http_, workflow_repository_);
  register_worker_routes(http_, worker_repository_);

  http_.set_logger([logger = logger_](const httplib::Request& req, const httplib::Response& res) {
    logger->info("http", "request handled",
                 {{.key = "method", .value = req.method},
                  {.key = "path", .value = req.path},
                  {.key = "status", .value = std::to_string(res.status)}});
  });
}

void App::run() {
  logger_->info("server", "starting",
                {{.key = "host", .value = config_.server_host},
                 {.key = "port", .value = std::to_string(config_.server_port)}});
  if (!http_.listen(config_.server_host, config_.server_port)) {
    logger_->critical("server", "failed to bind",
                      {{.key = "host", .value = config_.server_host},
                       {.key = "port", .value = std::to_string(config_.server_port)}});
  }
}

void App::stop() {
  http_.stop();
}

}  // namespace flowforge::server
