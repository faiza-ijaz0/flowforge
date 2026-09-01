// Phase 2B-4 acceptance test, PostgreSQL-backed variant: the same real
// pipeline as engine/tests/engine/retry_engine_acceptance_test.cpp
// (PriorityScheduler -> LocalWorkerPool -> JobExecutor -> HandlerRegistry
// -> RetryDispatcher), but backed by the real Postgres*Repository
// implementations instead of the in-memory ones -- proving retry state
// (attempt_count, status, job_attempts rows) genuinely round-trips through
// PostgreSQL, not just through process memory. See the brief's "END-TO-END
// ACCEPTANCE TEST" section ("Run a real local PostgreSQL-backed test where
// possible").

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
#include "flowforge/persistence/postgres/postgres_execution_repository.hpp"
#include "flowforge/persistence/postgres/postgres_job_repository.hpp"
#include "flowforge/persistence/postgres/postgres_worker_repository.hpp"
#include "postgres_test_support.hpp"

namespace flowforge::persistence::postgres {
namespace {

using test::PostgresIntegrationTest;

/// Fails its first `fail_count` invocations (retryable), then succeeds --
/// identical scripted handler to the in-memory acceptance test, duplicated
/// here rather than shared across translation units (matches this
/// codebase's existing convention of small, per-test-file scaffolding --
/// see e.g. local_worker_pool_test.cpp's RecordingExecutor).
class ScriptedHandler final : public engine::IJobHandler {
 public:
  explicit ScriptedHandler(std::size_t fail_count) : fail_count_(fail_count) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return "flaky"; }

  Result<domain::ExecutionResult> execute(const engine::ExecutionContext& /*context*/,
                                          const std::string& /*payload*/) override {
    const std::size_t call = calls_.fetch_add(1, std::memory_order_relaxed);
    if (call < fail_count_) {
      return domain::ExecutionResult::failure(ErrorCode::JobExecution, "scripted transient failure",
                                              /*retryable=*/true, std::chrono::milliseconds{0});
    }
    return domain::ExecutionResult::success("ok", std::chrono::milliseconds{0});
  }

 private:
  std::size_t fail_count_;
  std::atomic<std::size_t> calls_{0};
};

class PostgresRetryEngineAcceptanceTest : public PostgresIntegrationTest {
 protected:
  void build_pipeline(std::shared_ptr<engine::IJobHandler> handler) {
    job_repo = std::make_shared<PostgresJobRepository>(pool_, logger_);
    execution_repo = std::make_shared<PostgresExecutionRepository>(pool_, logger_);
    worker_repo = std::make_shared<PostgresWorkerRepository>(pool_, logger_);
    clock = infra::make_system_clock();

    handler_registry = std::make_shared<engine::HandlerRegistry>();
    ASSERT_TRUE(handler_registry->register_handler(std::move(handler)).has_value());

    executor =
        std::make_shared<engine::JobExecutor>(handler_registry, job_repo, execution_repo, clock, logger_);
    worker_pool = std::make_shared<engine::LocalWorkerPool>(
        executor, worker_repo, engine::WorkerPoolConfig{.worker_count = 1}, logger_);
    ASSERT_TRUE(worker_pool->start().has_value());

    scheduler = std::make_shared<engine::PriorityScheduler>(handler_registry, engine::SchedulerConfig{},
                                                            logger_, nullptr, worker_pool);
    ASSERT_TRUE(scheduler->start().has_value());

    retry_dispatcher = std::make_shared<engine::RetryDispatcher>(job_repo, scheduler, clock,
                                                                 engine::RetryDispatcherConfig{}, logger_);
  }

  void TearDown() override {
    if (scheduler) {
      std::ignore = scheduler->stop();
    }
    if (worker_pool) {
      std::ignore = worker_pool->stop();
    }
    PostgresIntegrationTest::TearDown();
  }

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

  std::shared_ptr<PostgresJobRepository> job_repo;
  std::shared_ptr<PostgresExecutionRepository> execution_repo;
  std::shared_ptr<PostgresWorkerRepository> worker_repo;
  std::shared_ptr<infra::Clock> clock;
  std::shared_ptr<engine::HandlerRegistry> handler_registry;
  std::shared_ptr<engine::JobExecutor> executor;
  std::shared_ptr<engine::LocalWorkerPool> worker_pool;
  std::shared_ptr<engine::PriorityScheduler> scheduler;
  std::shared_ptr<engine::RetryDispatcher> retry_dispatcher;
};

TEST_F(PostgresRetryEngineAcceptanceTest, JobFailsOnceThenRetrySucceedsAgainstRealPostgres) {
  build_pipeline(std::make_shared<ScriptedHandler>(/*fail_count=*/1));

  domain::RetryPolicy policy;
  policy.max_attempts = 3;
  policy.initial_backoff = std::chrono::milliseconds{0};
  domain::Job job(infra::JobId::generate(), "pg-acceptance", "hello", policy, clock->now(), 0, "flaky");
  ASSERT_TRUE(job_repo->insert(job).has_value());
  job.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(job).has_value());
  ASSERT_TRUE(scheduler->schedule(job).has_value());

  auto after_first = wait_for_status(job.id(), domain::JobStatus::Retrying);
  ASSERT_EQ(after_first.status(), domain::JobStatus::Retrying) << "job never reached Retrying in time";
  EXPECT_EQ(after_first.attempt_count(), 1u);

  auto retried_count = retry_dispatcher->poll_once();
  ASSERT_TRUE(retried_count.has_value()) << retried_count.error().message();
  EXPECT_EQ(*retried_count, 1u);

  auto final_job = wait_for_status(job.id(), domain::JobStatus::Succeeded);
  EXPECT_EQ(final_job.status(), domain::JobStatus::Succeeded);
  EXPECT_EQ(final_job.attempt_count(), 2u);

  // Verify directly against the real job_attempts table, not just through
  // the repository abstraction, that both attempts genuinely persisted.
  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 2u);
  EXPECT_EQ((*history)[0].attempt_number, 1u);
  EXPECT_EQ((*history)[0].outcome, domain::ExecutionOutcome::Failed);
  EXPECT_TRUE((*history)[0].worker_id.has_value());
  EXPECT_EQ((*history)[1].attempt_number, 2u);
  EXPECT_EQ((*history)[1].outcome, domain::ExecutionOutcome::Succeeded);
  EXPECT_TRUE((*history)[1].worker_id.has_value());

  auto reloaded = job_repo->find_by_id(job.id());
  ASSERT_TRUE(reloaded.has_value());
  EXPECT_EQ(reloaded->status(), domain::JobStatus::Succeeded);
  EXPECT_EQ(reloaded->attempt_count(), 2u);
}

TEST_F(PostgresRetryEngineAcceptanceTest, PermanentlyFailingJobReachesDeadLetterAgainstRealPostgres) {
  build_pipeline(std::make_shared<ScriptedHandler>(/*fail_count=*/100));

  domain::RetryPolicy policy;
  policy.max_attempts = 2;
  policy.initial_backoff = std::chrono::milliseconds{0};
  domain::Job job(infra::JobId::generate(), "pg-acceptance", "hello", policy, clock->now(), 0, "flaky");
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

  auto extra_poll = retry_dispatcher->poll_once();
  ASSERT_TRUE(extra_poll.has_value());
  EXPECT_EQ(*extra_poll, 0u);
}

}  // namespace
}  // namespace flowforge::persistence::postgres
