// Phase 2B-4 acceptance test (see PHASE 2B-4 brief, "END-TO-END ACCEPTANCE
// TEST"): proves the retry engine works through the REAL, unmodified
// production pipeline --
// PriorityScheduler -> LocalWorkerPool -> JobExecutor -> HandlerRegistry ->
// IJobHandler -> ExecutionResult -> RetryDispatcher -- wired together
// exactly like apps/server/src/http/app.cpp::App::create() does, minus
// HTTP itself. A scripted, in-test IJobHandler stands in for a real
// built-in handler (none of echo/delay/transform can be told to fail on
// demand) -- everything downstream of it is real, not mocked.
//
// RetryDispatcher::poll_once() (not start()/stop()) is called directly so
// the test can decide exactly when a retry scan happens, rather than
// racing a real background poll thread -- see retry_dispatcher.hpp's
// class comment on why poll_once() is public.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <tuple>

#include "flowforge/engine/job_executor.hpp"
#include "flowforge/engine/local_worker_pool.hpp"
#include "flowforge/engine/priority_scheduler.hpp"
#include "flowforge/engine/retry_dispatcher.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::engine {
namespace {

std::shared_ptr<infra::Logger> silent_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

/// Fails its first `fail_count` invocations (retryable), then succeeds.
class ScriptedHandler final : public IJobHandler {
 public:
  ScriptedHandler(std::string job_type, std::size_t fail_count)
      : job_type_(std::move(job_type)), fail_count_(fail_count) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return job_type_; }

  Result<domain::ExecutionResult> execute(const ExecutionContext& /*context*/,
                                          const std::string& /*payload*/) override {
    const std::size_t call = calls_.fetch_add(1, std::memory_order_relaxed);
    if (call < fail_count_) {
      return domain::ExecutionResult::failure(ErrorCode::JobExecution, "scripted transient failure",
                                              /*retryable=*/true, std::chrono::milliseconds{0});
    }
    return domain::ExecutionResult::success("ok", std::chrono::milliseconds{0});
  }

 private:
  std::string job_type_;
  std::size_t fail_count_;
  std::atomic<std::size_t> calls_{0};
};

class RetryEngineAcceptanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    job_repo = std::make_shared<persistence::InMemoryJobRepository>();
    execution_repo = std::make_shared<persistence::InMemoryExecutionRepository>();
    worker_repo = std::make_shared<persistence::InMemoryWorkerRepository>();
    clock = infra::make_system_clock();
    logger = silent_logger();
  }

  void TearDown() override {
    if (retry_dispatcher) {
      std::ignore = retry_dispatcher->stop();
    }
    if (scheduler) {
      std::ignore = scheduler->stop();
    }
    if (worker_pool) {
      std::ignore = worker_pool->stop();
    }
  }

  /// Wires the real pipeline with `handler` as the only registered
  /// handler. Mirrors App::create()'s construction order exactly
  /// (executor -> worker_pool -> scheduler -> retry_dispatcher).
  void build_pipeline(std::shared_ptr<IJobHandler> handler) {
    handler_registry = std::make_shared<HandlerRegistry>();
    ASSERT_TRUE(handler_registry->register_handler(std::move(handler)).has_value());

    executor = std::make_shared<JobExecutor>(handler_registry, job_repo, execution_repo, clock, logger);
    worker_pool =
        std::make_shared<LocalWorkerPool>(executor, worker_repo, WorkerPoolConfig{.worker_count = 1}, logger);
    ASSERT_TRUE(worker_pool->start().has_value());

    scheduler = std::make_shared<PriorityScheduler>(handler_registry, SchedulerConfig{}, logger, nullptr,
                                                    worker_pool);
    ASSERT_TRUE(scheduler->start().has_value());

    retry_dispatcher =
        std::make_shared<RetryDispatcher>(job_repo, scheduler, clock, RetryDispatcherConfig{}, logger);
    // Deliberately not started -- poll_once() is invoked explicitly so the
    // test controls exactly when a retry scan happens.
  }

  /// Real dispatch/execution happens on background threads (the actual
  /// LocalWorkerPool worker), so waiting for a status transition to land
  /// means polling with a bounded timeout -- the same pattern
  /// apps/server/tests/http_server_test.cpp's end-to-end tests already use
  /// for the identical reason.
  domain::Job wait_for_status(const infra::JobId& id, domain::JobStatus expected) {
    domain::Job last = *job_repo->find_by_id(id);
    for (int i = 0; i < 200; ++i) {
      auto current = job_repo->find_by_id(id);
      if (current.has_value()) {
        last = *current;
        if (last.status() == expected) {
          return last;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return last;
  }

  std::shared_ptr<persistence::InMemoryJobRepository> job_repo;
  std::shared_ptr<persistence::InMemoryExecutionRepository> execution_repo;
  std::shared_ptr<persistence::InMemoryWorkerRepository> worker_repo;
  std::shared_ptr<infra::Clock> clock;
  std::shared_ptr<infra::Logger> logger;
  std::shared_ptr<HandlerRegistry> handler_registry;
  std::shared_ptr<JobExecutor> executor;
  std::shared_ptr<LocalWorkerPool> worker_pool;
  std::shared_ptr<PriorityScheduler> scheduler;
  std::shared_ptr<RetryDispatcher> retry_dispatcher;
};

// Job created -> first execution fails -> attempt #1 persisted -> retry
// decision made -> retry scheduled -> attempt #2 executes -> final state
// reflects the result (Succeeded). Exactly the brief's required sequence.
TEST_F(RetryEngineAcceptanceTest, JobFailsOnceThenRetrySucceedsThroughRealPipeline) {
  build_pipeline(std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/1));

  domain::RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_backoff = std::chrono::milliseconds{0};  // Immediately eligible once Retrying.
  domain::Job job(infra::JobId::generate(), "acceptance", "hello", policy, clock->now(), 0, "flaky");
  ASSERT_TRUE(job_repo->insert(job).has_value());
  job.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(job).has_value());
  ASSERT_TRUE(scheduler->schedule(job).has_value());

  auto after_first = wait_for_status(job.id(), domain::JobStatus::Retrying);
  ASSERT_EQ(after_first.status(), domain::JobStatus::Retrying);
  EXPECT_EQ(after_first.attempt_count(), 1u);

  auto retried_count = retry_dispatcher->poll_once();
  ASSERT_TRUE(retried_count.has_value());
  EXPECT_EQ(*retried_count, 1u);

  auto final_job = wait_for_status(job.id(), domain::JobStatus::Succeeded);
  EXPECT_EQ(final_job.status(), domain::JobStatus::Succeeded);
  EXPECT_EQ(final_job.attempt_count(), 2u);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 2u);
  EXPECT_EQ((*history)[0].attempt_number, 1u);
  EXPECT_EQ((*history)[0].outcome, domain::ExecutionOutcome::Failed);
  EXPECT_EQ((*history)[1].attempt_number, 2u);
  EXPECT_EQ((*history)[1].outcome, domain::ExecutionOutcome::Succeeded);
}

// A job that never stops failing exhausts its retries and reaches
// DeadLetter -- never executed more than max_attempts times.
TEST_F(RetryEngineAcceptanceTest, PermanentlyFailingJobReachesDeadLetterAfterMaxAttempts) {
  build_pipeline(std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100));

  domain::RetryPolicy policy;
  policy.max_attempts = 2;
  policy.initial_backoff = std::chrono::milliseconds{0};
  domain::Job job(infra::JobId::generate(), "acceptance", "hello", policy, clock->now(), 0, "flaky");
  ASSERT_TRUE(job_repo->insert(job).has_value());
  job.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(job).has_value());
  ASSERT_TRUE(scheduler->schedule(job).has_value());

  auto after_first = wait_for_status(job.id(), domain::JobStatus::Retrying);
  ASSERT_EQ(after_first.status(), domain::JobStatus::Retrying);

  ASSERT_TRUE(retry_dispatcher->poll_once().has_value());

  auto final_job = wait_for_status(job.id(), domain::JobStatus::DeadLetter);
  EXPECT_EQ(final_job.status(), domain::JobStatus::DeadLetter);
  EXPECT_EQ(final_job.attempt_count(), 2u);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  EXPECT_EQ(history->size(), 2u);

  // Never executed a third time: a further retry scan finds nothing
  // (DeadLetter is terminal, excluded from list_by_status(Retrying, ...)).
  auto extra_poll = retry_dispatcher->poll_once();
  ASSERT_TRUE(extra_poll.has_value());
  EXPECT_EQ(*extra_poll, 0u);
}

// Cancellation must never turn into a retry: JobService::cancel_job()'s
// Cancelled transition (simulated directly here, the same way
// JobExecutor's own racing-cancel tests do) is a terminal state the retry
// dispatcher must leave alone, even though the job was Retrying moments
// before.
TEST_F(RetryEngineAcceptanceTest, CancellingARetryingJobPreventsFurtherExecution) {
  build_pipeline(std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100));

  domain::RetryPolicy policy;
  policy.max_attempts = 5;
  policy.initial_backoff = std::chrono::milliseconds{0};
  domain::Job job(infra::JobId::generate(), "acceptance", "hello", policy, clock->now(), 0, "flaky");
  ASSERT_TRUE(job_repo->insert(job).has_value());
  job.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(job).has_value());
  ASSERT_TRUE(scheduler->schedule(job).has_value());

  auto after_first = wait_for_status(job.id(), domain::JobStatus::Retrying);
  ASSERT_EQ(after_first.status(), domain::JobStatus::Retrying);

  // Simulate the HTTP cancel route's JobService::cancel_job() call.
  domain::Job cancelled = after_first;
  cancelled.transition_to(domain::JobStatus::Cancelled, clock->now());
  ASSERT_TRUE(job_repo->update(cancelled).has_value());

  auto retried_count = retry_dispatcher->poll_once();
  ASSERT_TRUE(retried_count.has_value());
  EXPECT_EQ(*retried_count, 0u);

  auto final_job = job_repo->find_by_id(job.id());
  ASSERT_TRUE(final_job.has_value());
  EXPECT_EQ(final_job->status(), domain::JobStatus::Cancelled);
  // Only the one real attempt (the one already in flight when cancelled)
  // was ever recorded -- no phantom retry attempt.
  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  EXPECT_EQ(history->size(), 1u);
}

}  // namespace
}  // namespace flowforge::engine
