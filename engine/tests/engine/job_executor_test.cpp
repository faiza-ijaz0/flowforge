#include "flowforge/engine/job_executor.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <tuple>

#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::engine {
namespace {

std::shared_ptr<infra::Logger> silent_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

std::shared_ptr<HandlerRegistry> make_registry() {
  auto registry = std::make_shared<HandlerRegistry>();
  std::ignore = handlers::register_builtin_handlers(*registry);
  return registry;
}

/// Test-only handler that fails its first `fail_count` invocations (with a
/// caller-chosen `retryable()` classification) before succeeding --
/// deterministic scaffolding for Phase 2B-4's retry-classification and
/// retry-then-succeed/retry-then-exhaust tests, which none of the
/// built-in handlers (echo/delay/transform) can produce on demand.
class ScriptedHandler final : public IJobHandler {
 public:
  ScriptedHandler(std::string job_type, std::size_t fail_count, bool retryable)
      : job_type_(std::move(job_type)), fail_count_(fail_count), retryable_(retryable) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return job_type_; }

  Result<domain::ExecutionResult> execute(const ExecutionContext& /*context*/,
                                          const std::string& /*payload*/) override {
    const std::size_t call = calls_.fetch_add(1, std::memory_order_relaxed);
    if (call < fail_count_) {
      return domain::ExecutionResult::failure(ErrorCode::JobExecution, "scripted failure", retryable_,
                                              std::chrono::milliseconds{0});
    }
    return domain::ExecutionResult::success("ok", std::chrono::milliseconds{0});
  }

  [[nodiscard]] std::size_t call_count() const { return calls_.load(std::memory_order_relaxed); }

 private:
  std::string job_type_;
  std::size_t fail_count_;
  bool retryable_;
  std::atomic<std::size_t> calls_{0};
};

class JobExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    job_repo = std::make_shared<persistence::InMemoryJobRepository>();
    execution_repo = std::make_shared<persistence::InMemoryExecutionRepository>();
    clock = infra::make_system_clock();
    metrics = infra::make_in_memory_metrics_registry();
    executor = std::make_unique<JobExecutor>(make_registry(), job_repo, execution_repo, clock,
                                             silent_logger(), metrics);
  }

  std::unique_ptr<JobExecutor> make_executor_with_timeout(std::chrono::milliseconds timeout) {
    return std::make_unique<JobExecutor>(make_registry(), job_repo, execution_repo, clock, silent_logger(),
                                         metrics, timeout);
  }

  std::unique_ptr<JobExecutor> make_executor_with_handler(std::shared_ptr<IJobHandler> handler) {
    auto registry = std::make_shared<HandlerRegistry>();
    std::ignore = registry->register_handler(std::move(handler));
    return std::make_unique<JobExecutor>(registry, job_repo, execution_repo, clock, silent_logger(), metrics);
  }

  domain::Job make_and_insert_job(std::string job_type, std::string payload = "hello",
                                  domain::JobStatus status = domain::JobStatus::Queued) {
    domain::Job job(infra::JobId::generate(), "q", std::move(payload), domain::RetryPolicy{}, clock->now(), 0,
                    std::move(job_type));
    if (status != domain::JobStatus::Pending) {
      job.transition_to(status, clock->now());
    }
    std::ignore = job_repo->insert(job);
    return job;
  }

  std::shared_ptr<persistence::InMemoryJobRepository> job_repo;
  std::shared_ptr<persistence::InMemoryExecutionRepository> execution_repo;
  std::shared_ptr<infra::Clock> clock;
  std::shared_ptr<infra::MetricsRegistry> metrics;
  std::unique_ptr<JobExecutor> executor;
};

TEST_F(JobExecutorTest, ExecutesEchoHandlerSuccessfully) {
  auto job = make_and_insert_job("echo", "hello world");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Succeeded);
  EXPECT_EQ(result->attempt_number, 1u);
  ASSERT_TRUE(result->worker_id.has_value());

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Succeeded);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 1u);
  EXPECT_EQ((*history)[0].outcome, domain::ExecutionOutcome::Succeeded);
}

TEST_F(JobExecutorTest, ExecutesTransformHandlerSuccessfully) {
  auto job = make_and_insert_job("transform", "hello world");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Succeeded);

  auto updated = job_repo->find_by_id(job.id());
  EXPECT_EQ(updated->status(), domain::JobStatus::Succeeded);
}

TEST_F(JobExecutorTest, ExecutesDelayHandlerSuccessfully) {
  auto job = make_and_insert_job("delay", "10");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Succeeded);
}

TEST_F(JobExecutorTest, UnknownHandlerTypeFailsCleanlyAndRecordsAttempt) {
  auto job = make_and_insert_job("no-such-handler");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Failed);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Failed);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 1u);
}

TEST_F(JobExecutorTest, SkipsExecutionIfJobAlreadyTerminal) {
  auto job = make_and_insert_job("echo", "hello", domain::JobStatus::Cancelled);
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Conflict);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Cancelled);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  EXPECT_TRUE(history->empty());
}

TEST_F(JobExecutorTest, ExternalCancellationDuringDelayIsRespectedAndJobBecomesCancelled) {
  auto job = make_and_insert_job("delay", "5000");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  std::thread canceller([flag] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    flag->store(true, std::memory_order_relaxed);
  });

  const auto start = std::chrono::steady_clock::now();
  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  canceller.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Cancelled);
  EXPECT_LT(elapsed.count(), 2000);  // stopped well short of the requested 5s delay.

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Cancelled);
}

TEST_F(JobExecutorTest, RacingCancelDuringExecutionIsNotOverwrittenBySuccess) {
  // Simulates JobService::cancel_job() persisting Cancelled *while the
  // handler is still running* (not before execute() even starts -- that
  // is SkipsExecutionIfJobAlreadyTerminal's scenario). Uses DelayHandler
  // to create a window during which the job repository is updated to
  // Cancelled directly -- deliberately NOT via the cancellation flag, to
  // prove the post-execution persisted-state re-check (not just the
  // flag) is what protects against this race. The handler itself is
  // never told to stop, so it completes successfully regardless; the
  // job's persisted Cancelled status must still win.
  auto job = make_and_insert_job("delay", "200");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  std::thread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto current = job_repo->find_by_id(job.id());
    ASSERT_TRUE(current.has_value());
    domain::Job cancelled_copy = *current;
    cancelled_copy.transition_to(domain::JobStatus::Cancelled, clock->now());
    ASSERT_TRUE(job_repo->update(cancelled_copy).has_value());
  });

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  canceller.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Cancelled);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Cancelled);
}

TEST_F(JobExecutorTest, TimeoutFailsJobAndDistinguishesFromExternalCancellation) {
  auto timed_executor = make_executor_with_timeout(std::chrono::milliseconds(50));
  auto job = make_and_insert_job("delay", "5000");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  const auto start = std::chrono::steady_clock::now();
  auto result = timed_executor->execute(job, infra::WorkerId::generate(), flag);
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::TimedOut);
  EXPECT_LT(elapsed.count(), 2000);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  // A timeout is system-initiated, not a user cancellation -- Failed, not
  // Cancelled (see JobExecutor::execute()'s class comment).
  EXPECT_EQ(updated->status(), domain::JobStatus::Failed);
}

TEST_F(JobExecutorTest, HandlerThatIgnoresCancellationStillCompletesAndWatcherJoinsCleanly) {
  // EchoHandler never checks is_cancelled() -- a timeout configured
  // shorter than nothing (it finishes instantly) must not cause any
  // hang; the watcher thread should join promptly regardless.
  auto timed_executor = make_executor_with_timeout(std::chrono::milliseconds(1));
  auto job = make_and_insert_job("echo", "hello");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = timed_executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Succeeded);
}

// --- Phase 2B-4: retry classification and lifecycle ------------------------

TEST_F(JobExecutorTest, RetryableFailureLandsOnRetryingWhenAttemptsRemain) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);

  domain::RetryPolicy policy;
  policy.max_attempts = 3;
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag = std::make_shared<std::atomic<bool>>(false);
  auto result = exec->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Failed);
  EXPECT_EQ(result->attempt_number, 1u);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Retrying);
  EXPECT_EQ(updated->attempt_count(), 1u);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 1u);
  EXPECT_EQ((*history)[0].outcome, domain::ExecutionOutcome::Failed);
}

TEST_F(JobExecutorTest, RetryableFailureLandsOnDeadLetterWhenAttemptsExhausted) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);

  domain::RetryPolicy policy;
  policy.max_attempts = 1;
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag = std::make_shared<std::atomic<bool>>(false);
  auto result = exec->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::DeadLetter);
  EXPECT_EQ(updated->attempt_count(), 1u);
}

TEST_F(JobExecutorTest, NonRetryableFailureGoesStraightToFailedRegardlessOfAttemptsRemaining) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/false);
  auto exec = make_executor_with_handler(handler);

  domain::RetryPolicy policy;
  policy.max_attempts = 5;  // Plenty of attempts remaining -- must not matter.
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag = std::make_shared<std::atomic<bool>>(false);
  auto result = exec->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Failed);
  EXPECT_EQ(updated->attempt_count(), 1u);
}

/// Proves the full retry-then-succeed lifecycle at the Executor level:
/// attempt #1 fails retryably (Retrying), a re-submission (what
/// RetryDispatcher does for real) flips it back to Queued, and attempt #2
/// succeeds -- attempt numbers are sequential, both attempts are
/// persisted, and the final job state agrees with the attempt history.
TEST_F(JobExecutorTest, SecondExecutionAttemptAfterRetryableFailureCanSucceed) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/1, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);
  auto job = make_and_insert_job("flaky");

  auto flag1 = std::make_shared<std::atomic<bool>>(false);
  auto first = exec->execute(job, infra::WorkerId::generate(), flag1);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->outcome, domain::ExecutionOutcome::Failed);
  EXPECT_EQ(first->attempt_number, 1u);

  auto after_first = job_repo->find_by_id(job.id());
  ASSERT_TRUE(after_first.has_value());
  ASSERT_EQ(after_first->status(), domain::JobStatus::Retrying);

  // Simulate RetryDispatcher's own Retrying -> Queued re-submission step.
  domain::Job retried = *after_first;
  retried.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(retried).has_value());

  auto flag2 = std::make_shared<std::atomic<bool>>(false);
  auto second = exec->execute(retried, infra::WorkerId::generate(), flag2);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->outcome, domain::ExecutionOutcome::Succeeded);
  EXPECT_EQ(second->attempt_number, 2u);

  auto final_job = job_repo->find_by_id(job.id());
  ASSERT_TRUE(final_job.has_value());
  EXPECT_EQ(final_job->status(), domain::JobStatus::Succeeded);
  EXPECT_EQ(final_job->attempt_count(), 2u);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  ASSERT_EQ(history->size(), 2u);
  EXPECT_EQ((*history)[0].attempt_number, 1u);
  EXPECT_EQ((*history)[0].outcome, domain::ExecutionOutcome::Failed);
  EXPECT_EQ((*history)[1].attempt_number, 2u);
  EXPECT_EQ((*history)[1].outcome, domain::ExecutionOutcome::Succeeded);
}

/// Mirrors the test above but the handler never succeeds -- proves a
/// retried job can also end up permanently failed (DeadLetter) once its
/// policy's attempts are exhausted, not just eventually succeed.
TEST_F(JobExecutorTest, RetryCanUltimatelyFailPermanentlyAfterExhaustingAttempts) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);

  domain::RetryPolicy policy;
  policy.max_attempts = 2;
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag1 = std::make_shared<std::atomic<bool>>(false);
  std::ignore = exec->execute(job, infra::WorkerId::generate(), flag1);
  auto after_first = job_repo->find_by_id(job.id());
  ASSERT_TRUE(after_first.has_value());
  ASSERT_EQ(after_first->status(), domain::JobStatus::Retrying);

  domain::Job retried = *after_first;
  retried.transition_to(domain::JobStatus::Queued, clock->now());
  ASSERT_TRUE(job_repo->update(retried).has_value());

  auto flag2 = std::make_shared<std::atomic<bool>>(false);
  std::ignore = exec->execute(retried, infra::WorkerId::generate(), flag2);

  auto final_job = job_repo->find_by_id(job.id());
  ASSERT_TRUE(final_job.has_value());
  EXPECT_EQ(final_job->status(), domain::JobStatus::DeadLetter);
  EXPECT_EQ(final_job->attempt_count(), 2u);

  auto history = execution_repo->history_for(job.id());
  ASSERT_TRUE(history.has_value());
  EXPECT_EQ(history->size(), 2u);
}

/// A successful execution must never itself trigger a retry -- there is no
/// retry mechanism to accidentally invoke here (JobExecutor never calls
/// IScheduler), but this pins down that a Succeeded job's attempt_count
/// and history stay exactly as Phase 2B-3's audit already proved (§ the
/// prior attempts-counter audit), unaffected by this phase's changes.
TEST_F(JobExecutorTest, SuccessfulExecutionNeverProducesARetryableState) {
  auto job = make_and_insert_job("echo", "hello");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  auto result = executor->execute(job, infra::WorkerId::generate(), flag);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, domain::ExecutionOutcome::Succeeded);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Succeeded);
  EXPECT_EQ(updated->attempt_count(), 1u);
  EXPECT_NE(updated->status(), domain::JobStatus::Retrying);
}

// --- Phase 2B-5: metrics -------------------------------------------------

TEST_F(JobExecutorTest, SuccessfulExecutionRecordsExecutionDuration) {
  auto job = make_and_insert_job("echo", "hello");
  auto flag = std::make_shared<std::atomic<bool>>(false);
  ASSERT_TRUE(executor->execute(job, infra::WorkerId::generate(), flag).has_value());

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.histograms.find("flowforge_executor_execution_duration_ms");
  ASSERT_NE(it, snapshot.histograms.end());
  EXPECT_EQ(it->second.count, 1u);
}

TEST_F(JobExecutorTest, NonRetryableFailureRecordsDurationAndOnlyIncrementsFailedCounter) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/1, /*retryable=*/false);
  auto exec = make_executor_with_handler(handler);
  auto job = make_and_insert_job("flaky");

  auto flag = std::make_shared<std::atomic<bool>>(false);
  ASSERT_TRUE(exec->execute(job, infra::WorkerId::generate(), flag).has_value());

  const auto snapshot = metrics->snapshot();
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_jobs_failed_total"), 1);
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_retryable_failures_total"), snapshot.counters.end());
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_jobs_timed_out_total"), snapshot.counters.end());
  ASSERT_NE(snapshot.histograms.find("flowforge_executor_execution_duration_ms"), snapshot.histograms.end());
}

TEST_F(JobExecutorTest, RetryableFailureThatStaysRetryingIncrementsRetryingNotFailed) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);
  domain::RetryPolicy policy;
  policy.max_attempts = 3;
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag = std::make_shared<std::atomic<bool>>(false);
  ASSERT_TRUE(exec->execute(job, infra::WorkerId::generate(), flag).has_value());

  const auto snapshot = metrics->snapshot();
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_retryable_failures_total"), 1);
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_jobs_retrying_total"), 1);
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_jobs_dead_letter_total"), snapshot.counters.end());
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_jobs_failed_total"), snapshot.counters.end());
}

TEST_F(JobExecutorTest, RetryableFailureThatExhaustsAttemptsIncrementsDeadLetterNotRetrying) {
  auto handler = std::make_shared<ScriptedHandler>("flaky", /*fail_count=*/100, /*retryable=*/true);
  auto exec = make_executor_with_handler(handler);
  domain::RetryPolicy policy;
  policy.max_attempts = 1;
  auto job = make_and_insert_job("flaky");
  job = domain::Job::restore(job.id(), job.queue_name(), job.payload(), policy, job.priority(), job.status(),
                             0, std::nullopt, job.created_at(), job.updated_at(), job.job_type());
  ASSERT_TRUE(job_repo->update(job).has_value());

  auto flag = std::make_shared<std::atomic<bool>>(false);
  ASSERT_TRUE(exec->execute(job, infra::WorkerId::generate(), flag).has_value());

  const auto snapshot = metrics->snapshot();
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_retryable_failures_total"), 1);
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_jobs_dead_letter_total"), 1);
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_jobs_retrying_total"), snapshot.counters.end());
}

TEST_F(JobExecutorTest, TimeoutRecordsDurationAndIncrementsTimedOutNotFailed) {
  auto timed_executor = make_executor_with_timeout(std::chrono::milliseconds(50));
  auto job = make_and_insert_job("delay", "5000");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  ASSERT_TRUE(timed_executor->execute(job, infra::WorkerId::generate(), flag).has_value());

  const auto snapshot = metrics->snapshot();
  EXPECT_EQ(snapshot.counters.at("flowforge_executor_jobs_timed_out_total"), 1);
  EXPECT_EQ(snapshot.counters.find("flowforge_executor_jobs_failed_total"), snapshot.counters.end());
  ASSERT_NE(snapshot.histograms.find("flowforge_executor_execution_duration_ms"), snapshot.histograms.end());
  EXPECT_EQ(snapshot.histograms.at("flowforge_executor_execution_duration_ms").count, 1u);
}

TEST_F(JobExecutorTest, CancellationDuringExecutionRecordsDuration) {
  auto job = make_and_insert_job("delay", "5000");
  auto flag = std::make_shared<std::atomic<bool>>(false);

  std::thread canceller([flag] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    flag->store(true, std::memory_order_relaxed);
  });
  ASSERT_TRUE(executor->execute(job, infra::WorkerId::generate(), flag).has_value());
  canceller.join();

  const auto snapshot = metrics->snapshot();
  ASSERT_NE(snapshot.histograms.find("flowforge_executor_execution_duration_ms"), snapshot.histograms.end());
  EXPECT_EQ(snapshot.histograms.at("flowforge_executor_execution_duration_ms").count, 1u);
}

}  // namespace
}  // namespace flowforge::engine
