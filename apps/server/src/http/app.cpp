#include "http/app.hpp"

#include <chrono>
#include <tuple>

#include "flowforge/engine/job_executor.hpp"
#include "flowforge/extractors/image_extractor.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/handlers/category_process_handler.hpp"
#include "flowforge/handlers/product_process_handler.hpp"
#include "flowforge/handlers/user_process_handler.hpp"
#include "flowforge/providers/tesseract_ocr_provider.hpp"
#include "http/cors.hpp"
#include "http/routes/category_routes.hpp"
#include "http/routes/health_routes.hpp"
#include "http/routes/job_routes.hpp"
#include "http/routes/process_routes.hpp"
#include "http/routes/product_routes.hpp"
#include "http/routes/user_routes.hpp"
#include "http/routes/worker_routes.hpp"
#include "http/routes/workflow_routes.hpp"
#include "http/routes/workload_routes.hpp"

namespace flowforge::server {

Result<std::unique_ptr<App>> App::create(infra::AppConfig config) {
  auto logger = infra::make_logger(config.log_level, config.structured_logging);
  auto metrics = infra::make_in_memory_metrics_registry();

  auto repositories = persistence::create_repositories(config, logger, metrics);
  if (!repositories) {
    logger->critical("server", "failed to initialize persistence",
                     {{.key = "error", .value = repositories.error().message()}});
    return std::unexpected(repositories.error());
  }

  auto handler_registry = std::make_shared<engine::HandlerRegistry>();
  if (auto registered = handlers::register_builtin_handlers(*handler_registry); !registered) {
    logger->critical("server", "failed to register built-in handlers",
                     {{.key = "error", .value = registered.error().message()}});
    return std::unexpected(registered.error());
  }
  // Phase 3E: registered separately from register_builtin_handlers, not
  // inside it -- ProductProcessHandler takes a constructor-injected
  // repository dependency (see its own class comment for why that never
  // widens engine::ExecutionContext), unlike every handler
  // register_builtin_handlers already registers, all of which are
  // dependency-free and default-constructible.
  if (auto registered = handler_registry->register_handler(
          std::make_shared<handlers::ProductProcessHandler>(repositories->products));
      !registered) {
    logger->critical("server", "failed to register product handler",
                     {{.key = "error", .value = registered.error().message()}});
    return std::unexpected(registered.error());
  }
  // Phase 3F: same registration convention as ProductProcessHandler above
  // -- constructor-injected repository, registered separately from
  // register_builtin_handlers.
  if (auto registered = handler_registry->register_handler(
          std::make_shared<handlers::CategoryProcessHandler>(repositories->categories));
      !registered) {
    logger->critical("server", "failed to register category handler",
                     {{.key = "error", .value = registered.error().message()}});
    return std::unexpected(registered.error());
  }
  // Phase 3H: UserProcessHandler now also takes a constructor-injected
  // repository (it upserts into the `users` table -- see its class
  // comment for why Users gained the same dedicated persistence Products/
  // Categories already had), so it moves out of register_builtin_handlers
  // and is registered here, identically to Product/Category above.
  if (auto registered = handler_registry->register_handler(
          std::make_shared<handlers::UserProcessHandler>(repositories->users));
      !registered) {
    logger->critical("server", "failed to register user handler",
                     {{.key = "error", .value = registered.error().message()}});
    return std::unexpected(registered.error());
  }

  auto executor = std::make_shared<engine::JobExecutor>(
      handler_registry, repositories->jobs, repositories->executions, infra::make_system_clock(), logger,
      metrics, std::chrono::milliseconds(config.execution_timeout_ms));

  auto worker_pool = std::make_shared<engine::LocalWorkerPool>(
      executor, repositories->workers,
      engine::WorkerPoolConfig{.worker_count = config.worker_pool_size,
                               .queue_capacity = config.worker_pool_queue_capacity},
      logger, metrics);
  if (auto started = worker_pool->start(); !started) {
    logger->critical("server", "failed to start worker pool",
                     {{.key = "error", .value = started.error().message()}});
    return std::unexpected(started.error());
  }

  auto scheduler = std::make_shared<engine::PriorityScheduler>(
      handler_registry,
      engine::SchedulerConfig{.queue_capacity = config.scheduler_queue_capacity,
                              .dispatch_worker_count = config.scheduler_dispatch_workers},
      logger, metrics, worker_pool);
  if (auto started = scheduler->start(); !started) {
    logger->critical("server", "failed to start scheduler",
                     {{.key = "error", .value = started.error().message()}});
    return std::unexpected(started.error());
  }

  // Phase 2B-4: the retry engine. Constructed last, after the Scheduler it
  // re-submits jobs to already exists -- see
  // docs/architecture/execution-model.md, "Retry engine" for why this
  // ordering (rather than injecting IScheduler into JobExecutor) avoids a
  // circular construction dependency (JobExecutor is built before
  // LocalWorkerPool, which the Scheduler itself depends on).
  auto retry_dispatcher = std::make_shared<engine::RetryDispatcher>(
      repositories->jobs, scheduler, infra::make_system_clock(),
      engine::RetryDispatcherConfig{.poll_interval = config.retry_poll_interval_ms,
                                    .batch_size = config.retry_batch_size},
      logger, metrics);
  if (auto started = retry_dispatcher->start(); !started) {
    logger->critical("server", "failed to start retry dispatcher",
                     {{.key = "error", .value = started.error().message()}});
    return std::unexpected(started.error());
  }

  // Phase 3D-1: probe for a usable Tesseract OCR binary once, at startup,
  // rather than on every /process/preview request -- see
  // `providers::TesseractCliOcrProvider::discover_executable`'s class
  // comment. A deployment without Tesseract installed gets a clean,
  // always-"not supported" image/screenshot preview (see
  // `InputProcessingService::preview`) instead of a crash or a fake
  // extraction the first time one is attempted -- never a build-time
  // failure either way.
  std::shared_ptr<engine::IInputExtractor> image_extractor;
  if (auto tesseract_path = providers::TesseractCliOcrProvider::discover_executable()) {
    logger->info("server", "image OCR extraction available",
                 {{.key = "tesseract_path", .value = *tesseract_path}});
    image_extractor = std::make_shared<extractors::ImageExtractor>(
        std::make_shared<providers::TesseractCliOcrProvider>(*tesseract_path));
  } else {
    logger->warn("server", "image OCR extraction not available -- no Tesseract binary found", {});
  }

  // Every dependency this process needs (persistence, worker pool,
  // scheduler, retry dispatcher) has now started successfully -- this is
  // the one moment "application readiness" (as opposed to "process
  // alive") is genuinely achieved, so it's the right place to log it
  // (Phase 2B-5). GET /ready computes its answer live on every request
  // rather than trusting a cached "became ready" flag (see
  // ReadinessChecks) -- this log line is a startup-diagnostics signal for
  // an operator watching logs, not something /ready itself depends on.
  logger->info("server", "application ready to accept work", {});

  // std::unique_ptr<App>(new App(...)) rather than std::make_unique: App's
  // constructor is private (construction must go through create()), which
  // make_unique cannot reach.
  return std::unique_ptr<App>(new App(std::move(config), std::move(logger), std::move(metrics),
                                      std::move(*repositories), std::move(handler_registry),
                                      std::move(worker_pool), std::move(scheduler),
                                      std::move(retry_dispatcher), std::move(image_extractor)));
}

App::App(infra::AppConfig config, std::shared_ptr<infra::Logger> logger,
         std::shared_ptr<infra::MetricsRegistry> metrics, persistence::RepositoryBundle repositories,
         std::shared_ptr<engine::HandlerRegistry> handler_registry,
         std::shared_ptr<engine::LocalWorkerPool> worker_pool,
         std::shared_ptr<engine::PriorityScheduler> scheduler,
         std::shared_ptr<engine::RetryDispatcher> retry_dispatcher,
         std::shared_ptr<engine::IInputExtractor> image_extractor)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      clock_(infra::make_system_clock()),
      metrics_(std::move(metrics)),
      job_repository_(std::move(repositories.jobs)),
      workflow_repository_(std::move(repositories.workflows)),
      worker_repository_(std::move(repositories.workers)),
      execution_manager_(std::move(repositories.executions)),
      workload_repository_(std::move(repositories.workloads)),
      product_repository_(std::move(repositories.products)),
      category_repository_(std::move(repositories.categories)),
      user_repository_(std::move(repositories.users)),
      job_service_(std::make_shared<services::JobService>(job_repository_, clock_, logger_, metrics_)),
      // Phase 3A: WorkloadService reuses JobService/scheduler exactly like
      // POST /api/v1/jobs does for a single job -- it is constructed here,
      // after job_service_, using the `scheduler` constructor parameter
      // directly (the scheduler_ *member* below is declared later, but the
      // parameter is available immediately regardless of member
      // declaration order).
      workload_service_(std::make_shared<services::WorkloadService>(
          workload_repository_, job_repository_, job_service_, scheduler, clock_, logger_, metrics_)),
      // Phase 3C: composes workload_service_ -- see
      // docs/architecture/input-processing.md. Never given its own
      // repositories/scheduler; it only ever reaches them through
      // workload_service_/the existing import functions.
      input_processing_service_(std::make_shared<services::InputProcessingService>(
          workload_service_, logger_, metrics_, std::move(image_extractor))),
      handler_registry_(std::move(handler_registry)),
      worker_pool_(std::move(worker_pool)),
      scheduler_(std::move(scheduler)),
      retry_dispatcher_(std::move(retry_dispatcher)),
      database_health_check_(std::move(repositories.check_database_health)),
      process_start_time_(std::chrono::steady_clock::now()) {
  register_routes();
}

void App::register_routes() {
  register_cors(http_, config_.cors_allowed_origin);

  // Phase 3B: a coarse, server-wide safety net bounding how much of any
  // single request body httplib will buffer into memory before a route
  // handler even runs -- defense in depth alongside
  // `services::WorkloadService`'s own, tighter, CSV-specific size check
  // (see docs/architecture/user-import.md, "Security"). 8 MiB comfortably
  // covers the CSV import's 2 MiB file-content bound plus multipart
  // framing/header overhead, while still bounding worst-case memory for
  // every other endpoint on this server.
  http_.set_payload_max_length(std::size_t{8} * 1024 * 1024);

  // Phase 2B-5: GET /ready reflects the actual state of every component
  // required to accept and process work, not just process liveness -- see
  // health_routes.hpp's class comment and docs/architecture/
  // execution-model.md, "Health vs readiness". Each check below is a
  // cheap, already-existing, non-blocking accessor -- nothing here adds a
  // new blocking call or a live database query on the request path.
  ReadinessChecks readiness{
      .database_healthy = database_health_check_,
      .scheduler_running = [scheduler =
                                scheduler_] { return scheduler->state() == engine::SchedulerState::Running; },
      .worker_pool_running = [worker_pool = worker_pool_] { return worker_pool->is_running(); },
      .retry_dispatcher_running = [retry_dispatcher =
                                       retry_dispatcher_] { return retry_dispatcher->is_running(); },
  };
  register_health_routes(http_, metrics_, config_, process_start_time_, std::move(readiness));

  register_job_routes(http_, job_service_, scheduler_, worker_pool_, execution_manager_, metrics_);
  register_workflow_routes(http_, workflow_repository_);
  register_worker_routes(http_, worker_repository_);
  register_workload_routes(http_, workload_service_, logger_, metrics_);
  register_process_routes(http_, input_processing_service_);
  register_product_routes(http_, product_repository_);
  register_category_routes(http_, category_repository_);
  register_user_routes(http_, user_repository_);

  http_.set_logger(
      [logger = logger_, metrics = metrics_](const httplib::Request& req, const httplib::Response& res) {
        logger->info("http", "request handled",
                     {{.key = "method", .value = req.method},
                      {.key = "path", .value = req.path},
                      {.key = "status", .value = std::to_string(res.status)}});
        // Phase 2B-5: coarse HTTP-level counters. Deliberately not a
        // per-request latency histogram here -- that would need a start
        // timestamp captured before routing, and httplib only supports one
        // pre-routing-handler registration for the whole server, already
        // owned by CORS's OPTIONS handling (cors.cpp); duplicating or
        // restructuring that seam just for HTTP latency was judged not worth
        // the risk this phase (see docs/architecture/execution-model.md,
        // "Observability", for the documented tradeoff). Execution-duration
        // timing -- the metric that actually matters for a job engine -- is
        // covered precisely, via a monotonic clock, at JobExecutor's boundary
        // instead (§20.3 of that doc).
        metrics->increment_counter("flowforge_http_requests_total");
        if (res.status >= 400) {
          metrics->increment_counter("flowforge_http_request_errors_total");
        }
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
  // Bracketing log lines (Phase 2B-5) so "did shutdown actually complete
  // cleanly" is answerable from logs alone -- each component below also
  // logs its own start/stop, but there was previously no single signal
  // marking the beginning and end of the overall shutdown sequence.
  logger_->info("server", "graceful shutdown starting", {});
  http_.stop();
  // Order matters, upstream-first: stop the RetryDispatcher first so it
  // stops handing retried jobs to the Scheduler, then the Scheduler so it
  // stops handing new jobs to the WorkerPool, then the WorkerPool (which
  // drains and executes whatever it already has queued before joining).
  // All calls are best-effort -- stop() fails with ErrorCode::Conflict if
  // already stopped (e.g. a second App::stop() call), which is harmless
  // here since each component's own destructor would otherwise handle it
  // defensively.
  std::ignore = retry_dispatcher_->stop();
  std::ignore = scheduler_->stop();
  std::ignore = worker_pool_->stop();
  logger_->info("server", "graceful shutdown complete", {});
}

}  // namespace flowforge::server
