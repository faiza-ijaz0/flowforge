// Phase 2B-1 acceptance test (see PHASE 2B-1 brief, "ACCEPTANCE TEST"):
// proves the handler abstraction and HandlerRegistry work end-to-end,
// without involving the Scheduler or WorkerPool -- neither exists yet,
// and this phase deliberately does not build them (see
// docs/architecture/execution-model.md).

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/engine/handler_registry.hpp"
#include "flowforge/handlers/builtin_handlers.hpp"
#include "flowforge/infra/logger.hpp"

namespace flowforge::engine {
namespace {

std::shared_ptr<infra::Logger> test_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

class ExecutionModelAcceptanceTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(handlers::register_builtin_handlers(registry).has_value()); }

  HandlerRegistry registry;
};

TEST_F(ExecutionModelAcceptanceTest, FullLifecycleFromJobToExecutionResult) {
  // 3. Create a valid Job for a registered type.
  domain::Job job(infra::JobId::generate(), "default", "hello world", domain::RetryPolicy{},
                  std::chrono::system_clock::now(), /*priority=*/0, /*job_type=*/"transform");
  ASSERT_TRUE(registry.contains(job.job_type()));

  // 4. Resolve the correct handler.
  auto handler = registry.resolve(job.job_type());
  ASSERT_TRUE(handler.has_value());
  EXPECT_EQ((*handler)->job_type(), "transform");

  // 5. Execute it through the handler abstraction.
  ExecutionContext context(infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                           test_logger());
  auto execution = (*handler)->execute(context, job.payload());

  // 6. Receive a real ExecutionResult.
  ASSERT_TRUE(execution.has_value());

  // 7. Verify the expected output/state.
  EXPECT_TRUE(execution->succeeded());
  EXPECT_EQ(execution->output(), "HELLO WORLD");
}

TEST_F(ExecutionModelAcceptanceTest, EchoAndDelayHandlersAreAlsoResolvableAndExecutable) {
  ExecutionContext context(infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                           test_logger());

  auto echo = registry.resolve("echo");
  ASSERT_TRUE(echo.has_value());
  auto echo_result = (*echo)->execute(context, "ping");
  ASSERT_TRUE(echo_result.has_value());
  EXPECT_EQ(echo_result->output(), "ping");

  auto delay = registry.resolve("delay");
  ASSERT_TRUE(delay.has_value());
  auto delay_result = (*delay)->execute(context, "0");
  ASSERT_TRUE(delay_result.has_value());
  EXPECT_TRUE(delay_result->succeeded());
}

// 8. Verify unknown job types fail cleanly.
TEST_F(ExecutionModelAcceptanceTest, UnknownJobTypeFailsCleanly) {
  domain::Job job(infra::JobId::generate(), "default", "{}", domain::RetryPolicy{},
                  std::chrono::system_clock::now(), /*priority=*/0, /*job_type=*/"does-not-exist");

  auto handler = registry.resolve(job.job_type());
  ASSERT_FALSE(handler.has_value());
  EXPECT_EQ(handler.error().code(), ErrorCode::NotFound);
}

// 9. Verify duplicate registration fails cleanly.
TEST_F(ExecutionModelAcceptanceTest, DuplicateBuiltinRegistrationFailsCleanly) {
  auto result = handlers::register_builtin_handlers(registry);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Conflict);
}

// 10. Verify concurrent lookups are safe.
TEST_F(ExecutionModelAcceptanceTest, ConcurrentResolveOfBuiltinHandlersIsSafe) {
  constexpr int kThreads = 8;
  constexpr int kLookupsPerThread = 1000;
  std::atomic<int> success_count{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kLookupsPerThread; ++i) {
        if (registry.resolve("echo").has_value()) {
          success_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(success_count.load(), kThreads * kLookupsPerThread);
}

}  // namespace
}  // namespace flowforge::engine
