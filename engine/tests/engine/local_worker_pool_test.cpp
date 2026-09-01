#include "flowforge/engine/local_worker_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/engine/job_executor.hpp"
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

domain::Job make_job(std::string job_type, std::string payload = "hello") {
  return domain::Job(infra::JobId::generate(), "q", std::move(payload), domain::RetryPolicy{},
                     std::chrono::system_clock::now(), 0, std::move(job_type));
}

/// Test-only IExecutor that records job ids it was asked to run (in
/// completion order) and blocks each call on a per-call latch so tests
/// can deterministically control when execution "finishes" -- used to
/// prove real concurrency (multiple in-flight executions at once) without
/// relying on timing.
class RecordingExecutor final : public IExecutor {
 public:
  Result<domain::Execution> execute(const domain::Job& job, const infra::WorkerId& worker_id,
                                    std::shared_ptr<std::atomic<bool>> /*cancellation_flag*/) override {
    {
      std::lock_guard lock(mutex_);
      in_flight_.insert(job.id().value());
      max_concurrent_ = std::max(max_concurrent_, in_flight_.size());
      cv_.notify_all();
    }

    if (hold_.load(std::memory_order_relaxed)) {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [&] { return !hold_.load(std::memory_order_relaxed); });
    }

    {
      std::lock_guard lock(mutex_);
      in_flight_.erase(job.id().value());
      completed_.push_back(job.id().value());
      cv_.notify_all();
    }

    domain::Execution execution;
    execution.id = infra::ExecutionId::generate();
    execution.job_id = job.id();
    execution.worker_id = worker_id;
    execution.outcome = domain::ExecutionOutcome::Succeeded;
    execution.started_at = std::chrono::system_clock::now();
    execution.finished_at = execution.started_at;
    return execution;
  }

  void hold_executions() { hold_.store(true); }
  void release_all() {
    std::lock_guard lock(mutex_);
    hold_.store(false);
    cv_.notify_all();
  }

  [[nodiscard]] std::size_t wait_for_in_flight(std::size_t count) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(5), [&] { return in_flight_.size() >= count; });
    return in_flight_.size();
  }

  [[nodiscard]] std::vector<std::string> wait_for_completed(std::size_t count) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(5), [&] { return completed_.size() >= count; });
    return completed_;
  }

  [[nodiscard]] std::size_t max_concurrent() const {
    std::lock_guard lock(mutex_);
    return max_concurrent_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::set<std::string> in_flight_;
  std::vector<std::string> completed_;
  std::size_t max_concurrent_ = 0;
  std::atomic<bool> hold_{false};
};

std::shared_ptr<persistence::IWorkerRepository> make_worker_repo() {
  return std::make_shared<persistence::InMemoryWorkerRepository>();
}

// --- Lifecycle ---------------------------------------------------------

TEST(LocalWorkerPoolTest, StartRegistersWorkersAndSucceeds) {
  auto worker_repo = make_worker_repo();
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), worker_repo,
                       WorkerPoolConfig{.worker_count = 3}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());
  EXPECT_EQ(pool.capacity(), 3u);

  auto workers = worker_repo->list();
  ASSERT_TRUE(workers.has_value());
  EXPECT_EQ(workers->size(), 3u);

  std::ignore = pool.stop();
}

TEST(LocalWorkerPoolTest, DoubleStartReturnsConflict) {
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), make_worker_repo(), WorkerPoolConfig{},
                       silent_logger());
  ASSERT_TRUE(pool.start().has_value());
  auto second = pool.start();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
  std::ignore = pool.stop();
}

TEST(LocalWorkerPoolTest, DoubleStopReturnsConflict) {
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), make_worker_repo(), WorkerPoolConfig{},
                       silent_logger());
  ASSERT_TRUE(pool.start().has_value());
  ASSERT_TRUE(pool.stop().has_value());
  auto second = pool.stop();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(LocalWorkerPoolTest, DispatchBeforeStartReturnsConflict) {
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), make_worker_repo(), WorkerPoolConfig{},
                       silent_logger());
  auto result = pool.dispatch(make_job("echo"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Conflict);
}

TEST(LocalWorkerPoolTest, StopMarksRegisteredWorkersOffline) {
  auto worker_repo = make_worker_repo();
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), worker_repo,
                       WorkerPoolConfig{.worker_count = 2}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());
  ASSERT_TRUE(pool.stop().has_value());

  auto workers = worker_repo->list();
  ASSERT_TRUE(workers.has_value());
  for (const auto& worker : *workers) {
    EXPECT_EQ(worker.status(), domain::WorkerStatus::Offline);
  }
}

// --- Dispatch / capacity -----------------------------------------------

TEST(LocalWorkerPoolTest, DispatchQueueCapacityIsRespected) {
  auto executor = std::make_shared<RecordingExecutor>();
  executor->hold_executions();
  LocalWorkerPool pool(executor, make_worker_repo(), WorkerPoolConfig{.worker_count = 1, .queue_capacity = 1},
                       silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  // First job occupies the single worker (held open); queue capacity 1
  // means exactly one more can sit queued behind it.
  ASSERT_TRUE(pool.dispatch(make_job("echo")).has_value());
  std::ignore = executor->wait_for_in_flight(1);
  ASSERT_TRUE(pool.dispatch(make_job("echo")).has_value());

  auto overflow = pool.dispatch(make_job("echo"));
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error().code(), ErrorCode::Conflict);

  executor->release_all();
  std::ignore = pool.stop();
}

// --- Concurrency ---------------------------------------------------------

TEST(LocalWorkerPoolTest, MultipleWorkersExecuteConcurrently) {
  auto executor = std::make_shared<RecordingExecutor>();
  executor->hold_executions();
  LocalWorkerPool pool(executor, make_worker_repo(),
                       WorkerPoolConfig{.worker_count = 4, .queue_capacity = 16}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(pool.dispatch(make_job("echo")).has_value());
  }
  // All 4 workers should be able to pick up work simultaneously -- prove
  // real concurrency, not one-at-a-time serialization.
  EXPECT_EQ(executor->wait_for_in_flight(4), 4u);
  EXPECT_EQ(pool.active_count(), 4u);

  executor->release_all();
  std::ignore = executor->wait_for_completed(4);
  std::ignore = pool.stop();
  EXPECT_GE(executor->max_concurrent(), 2u);
}

TEST(LocalWorkerPoolTest, OneSlowJobDoesNotBlockUnrelatedFastJobs) {
  auto executor = std::make_shared<RecordingExecutor>();
  LocalWorkerPool pool(executor, make_worker_repo(),
                       WorkerPoolConfig{.worker_count = 2, .queue_capacity = 16}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  executor->hold_executions();
  auto slow_job = make_job("delay", "5000");
  ASSERT_TRUE(pool.dispatch(slow_job).has_value());
  std::ignore = executor->wait_for_in_flight(1);  // slow job now occupies one of the two workers.

  auto fast_job = make_job("echo");
  executor->release_all();  // let the *fast* job (and, incidentally, the slow one) proceed.
  ASSERT_TRUE(pool.dispatch(fast_job).has_value());

  auto completed = executor->wait_for_completed(2);
  EXPECT_EQ(completed.size(), 2u);

  std::ignore = pool.stop();
}

TEST(LocalWorkerPoolTest, NoJobIsLostUnderConcurrentDispatch) {
  auto executor = std::make_shared<RecordingExecutor>();
  LocalWorkerPool pool(executor, make_worker_repo(),
                       WorkerPoolConfig{.worker_count = 4, .queue_capacity = 512}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  constexpr int kProducers = 8;
  constexpr int kJobsPerProducer = 20;
  constexpr std::size_t kTotal =
      static_cast<std::size_t>(kProducers) * static_cast<std::size_t>(kJobsPerProducer);

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  std::atomic<int> accepted{0};
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&] {
      for (int i = 0; i < kJobsPerProducer; ++i) {
        if (pool.dispatch(make_job("echo")).has_value()) {
          accepted.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  EXPECT_EQ(accepted.load(), static_cast<int>(kTotal));

  auto completed = executor->wait_for_completed(kTotal);
  EXPECT_EQ(completed.size(), kTotal);
  // No job executed twice: every completed id must be unique.
  std::set<std::string> unique_ids(completed.begin(), completed.end());
  EXPECT_EQ(unique_ids.size(), kTotal);

  std::ignore = pool.stop();
}

// --- Cancellation --------------------------------------------------------

TEST(LocalWorkerPoolTest, RequestCancellationOnUnknownJobReturnsNotFound) {
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), make_worker_repo(), WorkerPoolConfig{},
                       silent_logger());
  ASSERT_TRUE(pool.start().has_value());
  auto result = pool.request_cancellation(infra::JobId::generate());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
  std::ignore = pool.stop();
}

TEST(LocalWorkerPoolTest, RequestCancellationReachesRunningExecutorFlag) {
  // Uses the real JobExecutor + DelayHandler to prove cancellation
  // actually reaches a live in-flight execution's ExecutionContext, not
  // just a test double.
  auto job_repo = std::make_shared<persistence::InMemoryJobRepository>();
  auto execution_repo = std::make_shared<persistence::InMemoryExecutionRepository>();
  auto executor = std::make_shared<JobExecutor>(make_registry(), job_repo, execution_repo,
                                                infra::make_system_clock(), silent_logger());
  LocalWorkerPool pool(executor, make_worker_repo(), WorkerPoolConfig{.worker_count = 1}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  auto job = make_job("delay", "5000");
  ASSERT_TRUE(job_repo->insert(job).has_value());

  const auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(pool.dispatch(job).has_value());

  // Poll until the pool reports the job as active, then cancel it.
  for (int i = 0; i < 200 && pool.active_count() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_EQ(pool.active_count(), 1u);
  ASSERT_TRUE(pool.request_cancellation(job.id()).has_value());

  for (int i = 0; i < 200 && pool.active_count() != 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  EXPECT_EQ(pool.active_count(), 0u);
  EXPECT_LT(elapsed.count(), 2000);

  auto updated = job_repo->find_by_id(job.id());
  ASSERT_TRUE(updated.has_value());
  EXPECT_EQ(updated->status(), domain::JobStatus::Cancelled);

  std::ignore = pool.stop();
}

// --- Shutdown --------------------------------------------------------------

TEST(LocalWorkerPoolTest, StopDrainsQueuedWorkBeforeJoining) {
  auto executor = std::make_shared<RecordingExecutor>();
  LocalWorkerPool pool(executor, make_worker_repo(),
                       WorkerPoolConfig{.worker_count = 2, .queue_capacity = 16}, silent_logger());
  ASSERT_TRUE(pool.start().has_value());

  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(pool.dispatch(make_job("echo")).has_value());
  }
  ASSERT_TRUE(pool.stop().has_value());

  auto completed = executor->wait_for_completed(5);
  EXPECT_EQ(completed.size(), 5u);
}

// --- Phase 2B-5: readiness signal and metrics -------------------------

TEST(LocalWorkerPoolTest, IsRunningReflectsLifecycleState) {
  LocalWorkerPool pool(std::make_shared<RecordingExecutor>(), make_worker_repo(), WorkerPoolConfig{},
                       silent_logger());
  EXPECT_FALSE(pool.is_running());

  ASSERT_TRUE(pool.start().has_value());
  EXPECT_TRUE(pool.is_running());

  ASSERT_TRUE(pool.stop().has_value());
  EXPECT_FALSE(pool.is_running());
}

TEST(LocalWorkerPoolTest, CompletedJobsCounterIncrementsPerDispatchedJob) {
  auto executor = std::make_shared<RecordingExecutor>();
  auto metrics = infra::make_in_memory_metrics_registry();
  LocalWorkerPool pool(executor, make_worker_repo(), WorkerPoolConfig{.worker_count = 2}, silent_logger(),
                       metrics);
  ASSERT_TRUE(pool.start().has_value());

  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(pool.dispatch(make_job("echo")).has_value());
  }
  std::ignore = executor->wait_for_completed(3);
  std::ignore = pool.stop();

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.counters.find("flowforge_worker_pool_jobs_completed_total");
  ASSERT_NE(it, snapshot.counters.end());
  EXPECT_EQ(it->second, 3);
}

}  // namespace
}  // namespace flowforge::engine
