#include "flowforge/engine/handler_registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include "flowforge/infra/logger.hpp"

namespace flowforge::engine {
namespace {

std::shared_ptr<infra::Logger> test_logger() {
  return infra::make_logger(infra::LogLevel::Off, false);
}

/// Minimal stateless test handler: returns a fixed job type and echoes
/// the payload back, incrementing a shared counter so tests can observe
/// how many times it actually ran.
class CountingHandler final : public IJobHandler {
 public:
  explicit CountingHandler(std::string type, std::shared_ptr<std::atomic<int>> counter)
      : type_(std::move(type)), counter_(std::move(counter)) {}

  [[nodiscard]] std::string_view job_type() const noexcept override { return type_; }

  [[nodiscard]] Result<domain::ExecutionResult> execute(const ExecutionContext&,
                                                        const std::string& payload) override {
    counter_->fetch_add(1, std::memory_order_relaxed);
    return domain::ExecutionResult::success(payload, std::chrono::milliseconds{0});
  }

 private:
  std::string type_;
  std::shared_ptr<std::atomic<int>> counter_;
};

class EmptyTypeHandler final : public IJobHandler {
 public:
  [[nodiscard]] std::string_view job_type() const noexcept override { return ""; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const ExecutionContext&,
                                                        const std::string&) override {
    return domain::ExecutionResult::success("", std::chrono::milliseconds{0});
  }
};

std::shared_ptr<CountingHandler> make_handler(std::string type) {
  return std::make_shared<CountingHandler>(std::move(type), std::make_shared<std::atomic<int>>(0));
}

// --- Registration --------------------------------------------------------

TEST(HandlerRegistryTest, RegisterThenResolveReturnsSameHandler) {
  HandlerRegistry registry;
  auto handler = make_handler("echo");
  ASSERT_TRUE(registry.register_handler(handler).has_value());

  auto resolved = registry.resolve("echo");
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->get(), handler.get());
}

TEST(HandlerRegistryTest, ContainsReflectsRegistrationState) {
  HandlerRegistry registry;
  EXPECT_FALSE(registry.contains("echo"));
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());
  EXPECT_TRUE(registry.contains("echo"));
}

TEST(HandlerRegistryTest, ResolveUnknownTypeReturnsNotFound) {
  HandlerRegistry registry;
  auto resolved = registry.resolve("does-not-exist");
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(resolved.error().code(), ErrorCode::NotFound);
}

TEST(HandlerRegistryTest, DuplicateRegistrationReturnsConflict) {
  HandlerRegistry registry;
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());

  auto second = registry.register_handler(make_handler("echo"));
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error().code(), ErrorCode::Conflict);
}

TEST(HandlerRegistryTest, RegisteringNullHandlerReturnsValidationError) {
  HandlerRegistry registry;
  auto result = registry.register_handler(nullptr);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(HandlerRegistryTest, RegisteringHandlerWithEmptyJobTypeReturnsValidationError) {
  HandlerRegistry registry;
  auto result = registry.register_handler(std::make_shared<EmptyTypeHandler>());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code(), ErrorCode::Validation);
}

TEST(HandlerRegistryTest, RegisteredTypesListsEveryRegisteredType) {
  HandlerRegistry registry;
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());
  ASSERT_TRUE(registry.register_handler(make_handler("delay")).has_value());

  auto types = registry.registered_types();
  EXPECT_EQ(types.size(), 2u);
  EXPECT_NE(std::ranges::find(types, "echo"), types.end());
  EXPECT_NE(std::ranges::find(types, "delay"), types.end());
}

// --- Execution -------------------------------------------------------------

TEST(HandlerRegistryTest, ResolvedHandlerExecutesAndReturnsExecutionResult) {
  HandlerRegistry registry;
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());
  auto handler = registry.resolve("echo");
  ASSERT_TRUE(handler.has_value());

  ExecutionContext context(infra::JobId::generate(), infra::ExecutionId::generate(), "worker-1",
                           test_logger());
  auto result = (*handler)->execute(context, "payload");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->succeeded());
  EXPECT_EQ(result->output(), "payload");
}

// --- Concurrency -----------------------------------------------------------

TEST(HandlerRegistryTest, ConcurrentLookupsAreSafe) {
  HandlerRegistry registry;
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());
  ASSERT_TRUE(registry.register_handler(make_handler("delay")).has_value());

  constexpr int kThreads = 16;
  constexpr int kLookupsPerThread = 2000;
  std::atomic<int> found_count{0};
  std::atomic<int> not_found_count{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kLookupsPerThread; ++i) {
        const std::string& type = (i % 2 == 0) ? "echo" : "unknown";
        auto resolved = registry.resolve(type);
        if (resolved.has_value()) {
          found_count.fetch_add(1, std::memory_order_relaxed);
        } else {
          not_found_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(found_count.load(), kThreads * kLookupsPerThread / 2);
  EXPECT_EQ(not_found_count.load(), kThreads * kLookupsPerThread / 2);
}

TEST(HandlerRegistryTest, ConcurrentExecutionOfSameStatelessHandlerIsSafe) {
  HandlerRegistry registry;
  auto counter = std::make_shared<std::atomic<int>>(0);
  ASSERT_TRUE(registry.register_handler(std::make_shared<CountingHandler>("echo", counter)).has_value());
  auto handler = registry.resolve("echo");
  ASSERT_TRUE(handler.has_value());

  constexpr int kThreads = 16;
  constexpr int kExecutionsPerThread = 500;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      ExecutionContext context(infra::JobId::generate(), infra::ExecutionId::generate(), "worker",
                               test_logger());
      for (int i = 0; i < kExecutionsPerThread; ++i) {
        auto result = (*handler)->execute(context, "x");
        ASSERT_TRUE(result.has_value());
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(counter->load(), kThreads * kExecutionsPerThread);
}

TEST(HandlerRegistryTest, RegistrationDuringConcurrentLookupsDoesNotRace) {
  HandlerRegistry registry;
  ASSERT_TRUE(registry.register_handler(make_handler("echo")).has_value());

  std::atomic<bool> stop{false};
  std::thread reader([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      std::ignore = registry.resolve("echo");
      std::ignore = registry.contains("delay");
    }
  });

  auto result = registry.register_handler(make_handler("delay"));
  stop.store(true, std::memory_order_relaxed);
  reader.join();

  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(registry.contains("delay"));
}

}  // namespace
}  // namespace flowforge::engine
