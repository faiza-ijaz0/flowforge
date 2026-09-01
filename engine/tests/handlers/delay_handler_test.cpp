#include "flowforge/handlers/delay_handler.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "flowforge/infra/logger.hpp"

namespace flowforge::handlers {
namespace {

std::shared_ptr<infra::Logger> test_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

TEST(DelayHandlerTest, JobTypeIsDelay) {
  DelayHandler handler;
  EXPECT_EQ(handler.job_type(), "delay");
}

TEST(DelayHandlerTest, SleepsApproximatelyRequestedDuration) {
  DelayHandler handler;
  engine::ExecutionContext context{infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                                   test_logger()};

  const auto start = std::chrono::steady_clock::now();
  auto result = handler.execute(context, "50");
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->succeeded());
  EXPECT_GE(elapsed.count(), 50);
  EXPECT_GE(result->duration().count(), 50);
}

TEST(DelayHandlerTest, ZeroDelaySucceedsImmediately) {
  DelayHandler handler;
  engine::ExecutionContext context{infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                                   test_logger()};
  auto result = handler.execute(context, "0");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->succeeded());
}

TEST(DelayHandlerTest, NonNumericPayloadIsRejected) {
  DelayHandler handler;
  engine::ExecutionContext context{infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                                   test_logger()};
  auto result = handler.execute(context, "not-a-number");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(DelayHandlerTest, NegativeDelayIsRejected) {
  DelayHandler handler;
  engine::ExecutionContext context{infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                                   test_logger()};
  auto result = handler.execute(context, "-5");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(DelayHandlerTest, DelayBeyondMaxIsRejected) {
  DelayHandler handler;
  engine::ExecutionContext context{infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                                   test_logger()};
  const auto too_long = std::to_string(DelayHandler::kMaxDelay.count() + 1);
  auto result = handler.execute(context, too_long);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(DelayHandlerTest, CancellationStopsDelayEarlyAndReportsFailure) {
  DelayHandler handler;
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  engine::ExecutionContext context{infra::JobId::generate(),
                                   infra::ExecutionId::generate(),
                                   "worker-1",
                                   test_logger(),
                                   /*metrics=*/nullptr,
                                   cancelled};

  std::thread canceller([cancelled] {
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    cancelled->store(true, std::memory_order_relaxed);
  });

  const auto start = std::chrono::steady_clock::now();
  auto result = handler.execute(context, "5000");
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  canceller.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->succeeded());
  EXPECT_FALSE(result->retryable());
  ASSERT_TRUE(result->error_code().has_value());
  EXPECT_EQ(*result->error_code(), ErrorCode::JobExecution);
  // Cancelled well before the requested 5s delay would have elapsed.
  EXPECT_LT(elapsed.count(), 1000);
}

}  // namespace
}  // namespace flowforge::handlers
