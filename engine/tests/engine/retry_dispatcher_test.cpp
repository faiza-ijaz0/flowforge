#include "flowforge/engine/retry_dispatcher.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/in_memory_repositories.hpp"

namespace flowforge::engine {
namespace {

std::shared_ptr<infra::Logger> silent_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

/// Test-only IScheduler that records every job id it was asked to
/// schedule and can be configured to reject every call -- deterministic
/// scaffolding for proving RetryDispatcher's own logic (eligibility
/// timing, re-check-before-acting, deferral on rejection) in isolation
/// from the real PriorityScheduler/WorkerPool/JobExecutor pipeline (that
/// full pipeline is covered separately by retry_engine_acceptance_test.cpp).
class FakeScheduler final : public IScheduler {
 public:
  Result<void> schedule(const domain::Job& job) override {
    std::lock_guard lock(mutex_);
    if (reject_) {
      return std::unexpected(make_error(ErrorCode::Conflict, "fake scheduler configured to reject"));
    }
    scheduled_ids_.push_back(job.id().value());
    return {};
  }

  Result<void> cancel(const infra::JobId& /*job_id*/) override { return {}; }

  void set_reject(bool reject) {
    std::lock_guard lock(mutex_);
    reject_ = reject;
  }

  [[nodiscard]] std::vector<std::string> scheduled_ids() const {
    std::lock_guard lock(mutex_);
    return scheduled_ids_;
  }

  [[nodiscard]] std::size_t schedule_count() const {
    std::lock_guard lock(mutex_);
    return scheduled_ids_.size();
  }

 private:
  mutable std::mutex mutex_;
  bool reject_ = false;
  std::vector<std::string> scheduled_ids_;
};

/// Records the persisted status of each job at the instant schedule() is
/// called (see the WorkloadService equivalent in workload_service_test.cpp).
class StatusProbeScheduler final : public IScheduler {
 public:
  explicit StatusProbeScheduler(std::shared_ptr<persistence::IJobRepository> jobs) : jobs_(std::move(jobs)) {}

  Result<void> schedule(const domain::Job& job) override {
    auto persisted = jobs_->find_by_id(job.id());
    std::lock_guard lock(mutex_);
    statuses_.push_back(persisted ? persisted->status() : domain::JobStatus::Pending);
    return {};
  }
  Result<void> cancel(const infra::JobId& /*job_id*/) override { return {}; }

  [[nodiscard]] std::vector<domain::JobStatus> statuses() const {
    std::lock_guard lock(mutex_);
    return statuses_;
  }

 private:
  std::shared_ptr<persistence::IJobRepository> jobs_;
  mutable std::mutex mutex_;
  std::vector<domain::JobStatus> statuses_;
};

class RetryDispatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    job_repo = std::make_shared<persistence::InMemoryJobRepository>();
    fake_scheduler = std::make_shared<FakeScheduler>();
    clock = std::make_shared<infra::ManualClock>();
  }

  domain::Job make_retrying_job(domain::RetryPolicy policy = {}) {
    domain::Job job(infra::JobId::generate(), "q", "hello", policy, clock->now(), 0, "flaky");
    job.record_attempt_failure("boom", clock->now());
    std::ignore = job_repo->insert(job);
    return job;
  }

  std::unique_ptr<RetryDispatcher> make_dispatcher(RetryDispatcherConfig config = {}) {
    return std::make_unique<RetryDispatcher>(job_repo, fake_scheduler, clock, config, silent_logger());
  }

  std::shared_ptr<persistence::InMemoryJobRepository> job_repo;
  std::shared_ptr<FakeScheduler> fake_scheduler;
  std::shared_ptr<infra::ManualClock> clock;
};

// Phase 3I regression: the Retrying -> Queued transition must be persisted
// before the retry is scheduled. Otherwise a worker can finish the retry
// first and the late Queued write overwrites its outcome.
TEST_F(RetryDispatcherTest, RetryIsPersistedAsQueuedBeforeItIsScheduled) {
  const auto job = make_retrying_job();
  clock->advance(std::chrono::hours(1));

  auto probe = std::make_shared<StatusProbeScheduler>(job_repo);
  RetryDispatcher dispatcher(job_repo, probe, clock, RetryDispatcherConfig{}, silent_logger());
  auto result = dispatcher.poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 1u);

  const auto seen = probe->statuses();
  ASSERT_EQ(seen.size(), 1u);
  EXPECT_EQ(seen[0], domain::JobStatus::Queued);
}

TEST_F(RetryDispatcherTest, RejectedRetryRestoresTheOriginalRetryingRow) {
  const auto job = make_retrying_job();
  const auto original = job_repo->find_by_id(job.id());
  ASSERT_TRUE(original.has_value());
  clock->advance(std::chrono::hours(1));
  fake_scheduler->set_reject(true);

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0u);

  auto after = job_repo->find_by_id(job.id());
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->status(), domain::JobStatus::Retrying);
  // updated_at unchanged, so the retry's backoff is not reset.
  EXPECT_EQ(after->updated_at(), original->updated_at());
}

TEST_F(RetryDispatcherTest, DoesNotScheduleJobsInOtherStatuses) {
  domain::Job queued(infra::JobId::generate(), "q", "hello", domain::RetryPolicy{}, clock->now(), 0, "flaky");
  queued.transition_to(domain::JobStatus::Queued, clock->now());
  std::ignore = job_repo->insert(queued);

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0u);
  EXPECT_EQ(fake_scheduler->schedule_count(), 0u);
}

TEST_F(RetryDispatcherTest, DoesNotScheduleBeforeBackoffElapses) {
  domain::RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{1000};
  auto job = make_retrying_job(policy);

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0u);
  EXPECT_EQ(fake_scheduler->schedule_count(), 0u);

  auto still_retrying = job_repo->find_by_id(job.id());
  ASSERT_TRUE(still_retrying.has_value());
  EXPECT_EQ(still_retrying->status(), domain::JobStatus::Retrying);
}

TEST_F(RetryDispatcherTest, SchedulesEligibleJobAfterBackoffElapsesAndPersistsQueued) {
  domain::RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{1000};
  auto job = make_retrying_job(policy);

  clock->advance(std::chrono::milliseconds{1000});

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 1u);

  auto scheduled = fake_scheduler->scheduled_ids();
  ASSERT_EQ(scheduled.size(), 1u);
  EXPECT_EQ(scheduled[0], job.id().value());

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Queued);
}

TEST_F(RetryDispatcherTest, DoesNotDuplicateScheduleOnASecondTickOnceQueued) {
  domain::RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{100};
  make_retrying_job(policy);
  clock->advance(std::chrono::milliseconds{100});

  auto dispatcher = make_dispatcher();
  ASSERT_TRUE(dispatcher->poll_once().has_value());
  EXPECT_EQ(fake_scheduler->schedule_count(), 1u);

  // A second, immediate tick must not re-schedule -- the job is now
  // Queued, not Retrying, so list_by_status() no longer returns it.
  auto second = dispatcher->poll_once();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 0u);
  EXPECT_EQ(fake_scheduler->schedule_count(), 1u);
}

TEST_F(RetryDispatcherTest, SkipsJobThatWasCancelledAfterBecomingEligible) {
  domain::RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{100};
  auto job = make_retrying_job(policy);
  clock->advance(std::chrono::milliseconds{100});

  // Simulates a racing JobService::cancel_job() landing between this
  // dispatcher's (not-yet-run) list_by_status() and its schedule() call.
  auto current = job_repo->find_by_id(job.id());
  ASSERT_TRUE(current.has_value());
  domain::Job cancelled = *current;
  cancelled.transition_to(domain::JobStatus::Cancelled, clock->now());
  ASSERT_TRUE(job_repo->update(cancelled).has_value());

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0u);
  EXPECT_EQ(fake_scheduler->schedule_count(), 0u);

  auto final_state = job_repo->find_by_id(job.id());
  ASSERT_TRUE(final_state.has_value());
  EXPECT_EQ(final_state->status(), domain::JobStatus::Cancelled);
}

TEST_F(RetryDispatcherTest, LeavesJobRetryingWhenSchedulerRejectsSoALaterTickCanRetry) {
  domain::RetryPolicy policy;
  policy.initial_backoff = std::chrono::milliseconds{100};
  auto job = make_retrying_job(policy);
  clock->advance(std::chrono::milliseconds{100});
  fake_scheduler->set_reject(true);

  auto dispatcher = make_dispatcher();
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0u);

  auto still_retrying = job_repo->find_by_id(job.id());
  ASSERT_TRUE(still_retrying.has_value());
  EXPECT_EQ(still_retrying->status(), domain::JobStatus::Retrying);

  // Once the scheduler recovers, a later tick succeeds -- the job was
  // never lost.
  fake_scheduler->set_reject(false);
  auto second = dispatcher->poll_once();
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 1u);
  auto now_queued = job_repo->find_by_id(job.id());
  ASSERT_TRUE(now_queued.has_value());
  EXPECT_EQ(now_queued->status(), domain::JobStatus::Queued);
}

TEST_F(RetryDispatcherTest, CandidatesCounterReflectsBatchSizeSeenNotJustResubmitted) {
  // Phase 2B-5: "candidates" (jobs seen this tick) must be distinguishable
  // from "resubmitted" (jobs actually eligible/dispatched) -- insert one
  // job whose backoff has not elapsed alongside one that has.
  domain::RetryPolicy not_yet_eligible;
  not_yet_eligible.initial_backoff = std::chrono::milliseconds{10'000};
  make_retrying_job(not_yet_eligible);

  domain::RetryPolicy eligible;
  eligible.initial_backoff = std::chrono::milliseconds{0};
  make_retrying_job(eligible);

  auto metrics = infra::make_in_memory_metrics_registry();
  RetryDispatcher dispatcher(job_repo, fake_scheduler, clock, RetryDispatcherConfig{}, silent_logger(),
                             metrics);
  auto retried = dispatcher.poll_once();
  ASSERT_TRUE(retried.has_value());
  EXPECT_EQ(*retried, 1u);  // Only the eligible one was resubmitted.

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.counters.find("flowforge_retry_dispatcher_candidates_total");
  ASSERT_NE(it, snapshot.counters.end());
  EXPECT_EQ(it->second, 2);  // Both were seen as candidates this tick.
}

TEST_F(RetryDispatcherTest, RespectsBatchSizeLimitPerTick) {
  domain::RetryPolicy policy;  // Zero backoff -- immediately eligible.
  policy.initial_backoff = std::chrono::milliseconds{0};
  for (int i = 0; i < 5; ++i) {
    make_retrying_job(policy);
  }

  auto dispatcher = make_dispatcher(RetryDispatcherConfig{.batch_size = 2});
  auto result = dispatcher->poll_once();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 2u);
}

TEST_F(RetryDispatcherTest, StartStopLifecycleRejectsDoubleCallsAndRestart) {
  auto dispatcher = make_dispatcher(RetryDispatcherConfig{.poll_interval = std::chrono::milliseconds{10}});
  ASSERT_TRUE(dispatcher->start().has_value());
  EXPECT_TRUE(dispatcher->is_running());

  auto second_start = dispatcher->start();
  ASSERT_FALSE(second_start.has_value());
  EXPECT_EQ(second_start.error().code(), ErrorCode::Conflict);

  ASSERT_TRUE(dispatcher->stop().has_value());
  EXPECT_FALSE(dispatcher->is_running());

  auto second_stop = dispatcher->stop();
  ASSERT_FALSE(second_stop.has_value());
  EXPECT_EQ(second_stop.error().code(), ErrorCode::Conflict);

  auto restart = dispatcher->start();
  ASSERT_FALSE(restart.has_value());
  EXPECT_EQ(restart.error().code(), ErrorCode::Conflict);
}

/// Mirrors PrioritySchedulerConcurrencyTest.
/// ShutdownWhileProducersActiveDoesNotDeadlockOrCrash: starts the real
/// background poll thread with retrying jobs continuously being inserted
/// from another thread, then stops the dispatcher concurrently -- proves
/// stop() joins promptly (bounded wait) and neither deadlocks nor crashes.
TEST_F(RetryDispatcherTest, StopWhileProducerActiveDoesNotDeadlockOrCrash) {
  auto dispatcher = make_dispatcher(RetryDispatcherConfig{.poll_interval = std::chrono::milliseconds{5}});
  ASSERT_TRUE(dispatcher->start().has_value());

  std::atomic<bool> keep_producing{true};
  std::thread producer([&] {
    domain::RetryPolicy policy;
    policy.initial_backoff = std::chrono::milliseconds{0};
    while (keep_producing.load(std::memory_order_relaxed)) {
      domain::Job job(infra::JobId::generate(), "q", "hello", policy, std::chrono::system_clock::now(), 0,
                      "flaky");
      job.record_attempt_failure("boom", std::chrono::system_clock::now());
      std::ignore = job_repo->insert(job);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto stopped = dispatcher->stop();
  keep_producing.store(false);
  producer.join();

  EXPECT_TRUE(stopped.has_value());
  EXPECT_FALSE(dispatcher->is_running());
}

}  // namespace
}  // namespace flowforge::engine
