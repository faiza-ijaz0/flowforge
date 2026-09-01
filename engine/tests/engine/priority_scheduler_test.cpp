#include "flowforge/engine/priority_scheduler.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/infra/logger.hpp"

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

domain::Job make_job(std::string job_type, int priority = 0,
                     domain::JobStatus status = domain::JobStatus::Pending) {
  domain::Job job(infra::JobId::generate(), "default-queue", "{}", domain::RetryPolicy{},
                  std::chrono::system_clock::now(), priority, std::move(job_type));
  if (status != domain::JobStatus::Pending) {
    job.transition_to(status, std::chrono::system_clock::now());
  }
  return job;
}

/// Test logger that records every scheduler dispatch-loop log line's
/// `job_id` (any message containing "dispatch" -- "job dispatched",
/// "job cancelled before dispatch", "handler vanished between schedule
/// and dispatch" -- all three, and only those three, are logged from
/// inside PriorityScheduler::dispatch_loop()). Optionally blocks the
/// *first* matching call until release_gate() is called, which lets a
/// test deterministically pause the dispatch loop right after it has
/// popped its first item -- long enough to fill the queue to a known
/// state, or to schedule more (higher-priority) work -- without any
/// sleep-based timing assumption.
class RecordingGatedLogger final : public infra::Logger {
 public:
  void log(infra::LogLevel, std::string_view component, std::string_view message,
           const std::vector<infra::Field>& fields) const override {
    if (!(component == "scheduler" && message.find("dispatch") != std::string_view::npos)) {
      return;
    }
    std::unique_lock lock(mutex_);
    if (gate_armed_ && !gate_consumed_) {
      gate_consumed_ = true;
      waiting_ = true;
      cv_.notify_all();
      cv_.wait(lock, [&] { return released_; });
      waiting_ = false;
    }
    for (const auto& field : fields) {
      if (field.key == "job_id") {
        dispatched_order_.push_back(field.value);
      }
    }
    cv_.notify_all();
  }

  void arm_gate() {
    std::lock_guard lock(mutex_);
    gate_armed_ = true;
    gate_consumed_ = false;
    released_ = false;
  }

  void wait_for_gate() const {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return waiting_; });
  }

  void release_gate() {
    std::lock_guard lock(mutex_);
    released_ = true;
    cv_.notify_all();
  }

  [[nodiscard]] std::vector<std::string> wait_for_count(std::size_t count) const {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(5), [&] { return dispatched_order_.size() >= count; });
    return dispatched_order_;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  mutable bool gate_armed_ = false;
  mutable bool gate_consumed_ = false;
  mutable bool waiting_ = false;
  mutable bool released_ = true;
  mutable std::vector<std::string> dispatched_order_;
};

// --- Lifecycle -------------------------------------------------------------

TEST(PrioritySchedulerTest, StartSucceedsOnFreshScheduler) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  EXPECT_TRUE(scheduler.start().has_value());
  EXPECT_EQ(scheduler.state(), SchedulerState::Running);
  EXPECT_TRUE(scheduler.stop().has_value());
}

TEST(PrioritySchedulerTest, StopSucceedsOnRunningScheduler) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  ASSERT_TRUE(scheduler.start().has_value());
  EXPECT_TRUE(scheduler.stop().has_value());
  EXPECT_EQ(scheduler.state(), SchedulerState::Stopped);
}

TEST(PrioritySchedulerTest, DoubleStartReturnsConflict) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  ASSERT_TRUE(scheduler.start().has_value());
  auto second = scheduler.start();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
  std::ignore = scheduler.stop();
}

TEST(PrioritySchedulerTest, DoubleStopReturnsConflict) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(scheduler.stop().has_value());
  auto second = scheduler.stop();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(PrioritySchedulerTest, RestartAfterStopIsRejected) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(scheduler.stop().has_value());
  auto restarted = scheduler.start();
  ASSERT_FALSE(restarted.has_value());
  EXPECT_EQ(restarted.error().code(), ErrorCode::Conflict);
}

TEST(PrioritySchedulerTest, SchedulingBeforeStartReturnsConflict) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  auto result = scheduler.schedule(make_job("echo"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Conflict);
}

TEST(PrioritySchedulerTest, SchedulingAfterShutdownReturnsConflict) {
  PriorityScheduler scheduler(make_registry(), SchedulerConfig{}, silent_logger());
  ASSERT_TRUE(scheduler.start().has_value());
  ASSERT_TRUE(scheduler.stop().has_value());
  auto result = scheduler.schedule(make_job("echo"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Conflict);
}

// --- Scheduling --------------------------------------------------------

class RunningSchedulerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scheduler = std::make_unique<PriorityScheduler>(make_registry(), SchedulerConfig{}, silent_logger());
    ASSERT_TRUE(scheduler->start().has_value());
  }
  void TearDown() override { std::ignore = scheduler->stop(); }

  std::unique_ptr<PriorityScheduler> scheduler;
};

TEST_F(RunningSchedulerTest, ValidJobIsAccepted) {
  EXPECT_TRUE(scheduler->schedule(make_job("echo")).has_value());
}

TEST_F(RunningSchedulerTest, TerminalJobIsRejected) {
  auto result = scheduler->schedule(make_job("echo", 0, domain::JobStatus::Succeeded));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(RunningSchedulerTest, EmptyJobTypeIsRejected) {
  auto result = scheduler->schedule(make_job(""));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST_F(RunningSchedulerTest, UnknownJobTypeIsRejected) {
  auto result = scheduler->schedule(make_job("no-such-handler"));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

TEST_F(RunningSchedulerTest, MultipleJobsAreAllAccepted) {
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(scheduler->schedule(make_job("echo")).has_value());
  }
}

TEST_F(RunningSchedulerTest, CancelUnknownJobReturnsNotFound) {
  auto result = scheduler->cancel(infra::JobId::generate());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::NotFound);
}

// --- Priority ------------------------------------------------------------

TEST(PrioritySchedulerPriorityTest, HigherPriorityDispatchedBeforeLowerPriority) {
  auto logger = std::make_shared<RecordingGatedLogger>();
  PriorityScheduler scheduler(make_registry(),
                              SchedulerConfig{.queue_capacity = 8, .dispatch_worker_count = 1}, logger);
  ASSERT_TRUE(scheduler.start().has_value());

  logger->arm_gate();
  auto first = make_job("echo", /*priority=*/0);
  const std::string first_id = first.id().value();
  ASSERT_TRUE(scheduler.schedule(first).has_value());
  // Deterministically wait for the single dispatch worker to pop `first`
  // and pause inside its dispatch log call, before scheduling anything
  // else -- this guarantees `first` is dispatched before the two jobs
  // below regardless of their priority, and that the two below are
  // ordered strictly by priority against each other.
  logger->wait_for_gate();

  auto high = make_job("echo", /*priority=*/10);
  auto medium = make_job("echo", /*priority=*/5);
  const std::string high_id = high.id().value();
  const std::string medium_id = medium.id().value();
  ASSERT_TRUE(scheduler.schedule(medium).has_value());
  ASSERT_TRUE(scheduler.schedule(high).has_value());

  logger->release_gate();
  auto order = logger->wait_for_count(3);
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], first_id);
  EXPECT_EQ(order[1], high_id);
  EXPECT_EQ(order[2], medium_id);

  std::ignore = scheduler.stop();
}

TEST(PrioritySchedulerPriorityTest, EqualPriorityPreservesFifoOrder) {
  auto logger = std::make_shared<RecordingGatedLogger>();
  PriorityScheduler scheduler(make_registry(),
                              SchedulerConfig{.queue_capacity = 8, .dispatch_worker_count = 1}, logger);
  ASSERT_TRUE(scheduler.start().has_value());

  logger->arm_gate();
  auto first = make_job("echo", /*priority=*/0);
  const std::string first_id = first.id().value();
  ASSERT_TRUE(scheduler.schedule(first).has_value());
  logger->wait_for_gate();

  auto second = make_job("echo", /*priority=*/0);
  auto third = make_job("echo", /*priority=*/0);
  const std::string second_id = second.id().value();
  const std::string third_id = third.id().value();
  ASSERT_TRUE(scheduler.schedule(second).has_value());
  ASSERT_TRUE(scheduler.schedule(third).has_value());

  logger->release_gate();
  auto order = logger->wait_for_count(3);
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], first_id);
  EXPECT_EQ(order[1], second_id);
  EXPECT_EQ(order[2], third_id);

  std::ignore = scheduler.stop();
}

// --- Capacity --------------------------------------------------------------

TEST(PrioritySchedulerCapacityTest, QueueCapacityIsRespected) {
  auto logger = std::make_shared<RecordingGatedLogger>();
  PriorityScheduler scheduler(make_registry(),
                              SchedulerConfig{.queue_capacity = 2, .dispatch_worker_count = 1}, logger);
  ASSERT_TRUE(scheduler.start().has_value());

  logger->arm_gate();
  ASSERT_TRUE(scheduler.schedule(make_job("echo")).has_value());
  // Dispatch worker has now popped that job and is paused mid-dispatch,
  // so the queue behind it is guaranteed empty at this point.
  logger->wait_for_gate();

  ASSERT_TRUE(scheduler.schedule(make_job("echo")).has_value());
  ASSERT_TRUE(scheduler.schedule(make_job("echo")).has_value());

  auto overflow = scheduler.schedule(make_job("echo"));
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error().code(), ErrorCode::Conflict);

  logger->release_gate();
  std::ignore = scheduler.stop();
}

// Phase 2B-5: the backpressure-specific counter must increment exactly on
// the queue-full rejection path, not on other rejection reasons (unknown
// job_type, terminal job, empty job_type) -- those already increment the
// generic flowforge_scheduler_rejections_total counter but say nothing
// about capacity specifically.
TEST(PrioritySchedulerCapacityTest, BackpressureRejectionIncrementsDedicatedCounter) {
  auto logger = std::make_shared<RecordingGatedLogger>();
  auto metrics = infra::make_in_memory_metrics_registry();
  PriorityScheduler scheduler(
      make_registry(), SchedulerConfig{.queue_capacity = 1, .dispatch_worker_count = 1}, logger, metrics);
  ASSERT_TRUE(scheduler.start().has_value());

  logger->arm_gate();
  ASSERT_TRUE(scheduler.schedule(make_job("echo")).has_value());
  logger->wait_for_gate();
  ASSERT_TRUE(scheduler.schedule(make_job("echo")).has_value());  // Fills the capacity-1 queue.

  auto overflow = scheduler.schedule(make_job("echo"));
  ASSERT_FALSE(overflow.has_value());

  const auto snapshot = metrics->snapshot();
  auto it = snapshot.counters.find("flowforge_scheduler_backpressure_rejections_total");
  ASSERT_NE(it, snapshot.counters.end());
  EXPECT_EQ(it->second, 1);

  logger->release_gate();
  std::ignore = scheduler.stop();
}

// --- Concurrency -----------------------------------------------------------

TEST(PrioritySchedulerConcurrencyTest, ConcurrentProducersLoseNoJobs) {
  auto logger = std::make_shared<RecordingGatedLogger>();
  constexpr std::size_t kProducers = 8;
  constexpr std::size_t kJobsPerProducer = 50;
  constexpr std::size_t kTotal = kProducers * kJobsPerProducer;

  PriorityScheduler scheduler(make_registry(),
                              SchedulerConfig{.queue_capacity = kTotal, .dispatch_worker_count = 4}, logger);
  ASSERT_TRUE(scheduler.start().has_value());

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  std::atomic<int> accepted{0};
  for (std::size_t p = 0; p < kProducers; ++p) {
    producers.emplace_back([&] {
      for (std::size_t i = 0; i < kJobsPerProducer; ++i) {
        if (scheduler.schedule(make_job("echo")).has_value()) {
          accepted.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  EXPECT_EQ(accepted.load(), static_cast<int>(kTotal));

  auto order = logger->wait_for_count(kTotal);
  EXPECT_EQ(order.size(), kTotal);

  std::ignore = scheduler.stop();
}

TEST(PrioritySchedulerConcurrencyTest, ShutdownWhileProducersActiveDoesNotDeadlockOrCrash) {
  auto scheduler = std::make_unique<PriorityScheduler>(
      make_registry(), SchedulerConfig{.queue_capacity = 4096}, silent_logger());
  ASSERT_TRUE(scheduler->start().has_value());

  std::atomic<bool> stop_producing{false};
  std::vector<std::thread> producers;
  producers.reserve(4);
  for (int p = 0; p < 4; ++p) {
    producers.emplace_back([&] {
      while (!stop_producing.load(std::memory_order_relaxed)) {
        auto result = scheduler->schedule(make_job("echo"));
        // Once the scheduler stops, every subsequent call must fail
        // cleanly with Conflict -- never crash, hang, or return success.
        if (!result.has_value()) {
          EXPECT_EQ(result.error().code(), ErrorCode::Conflict);
        }
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_TRUE(scheduler->stop().has_value());
  stop_producing.store(true, std::memory_order_relaxed);
  for (auto& producer : producers) {
    producer.join();
  }

  EXPECT_EQ(scheduler->state(), SchedulerState::Stopped);
}

}  // namespace
}  // namespace flowforge::engine
