#include "flowforge/engine/thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <tuple>
#include <vector>

namespace flowforge::engine {
namespace {

TEST(ThreadPoolTest, SubmitReturnsComputedValue) {
  ThreadPool pool(2);
  auto future = pool.submit([] { return 21 * 2; });
  EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, RunsAllSubmittedTasks) {
  ThreadPool pool(4);
  constexpr int kTaskCount = 200;
  std::atomic<int> counter{0};

  std::vector<std::future<void>> futures;
  futures.reserve(kTaskCount);
  for (int i = 0; i < kTaskCount; ++i) {
    futures.push_back(pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
  }
  for (auto& f : futures) {
    f.wait();
  }
  EXPECT_EQ(counter.load(), kTaskCount);
}

TEST(ThreadPoolTest, PropagatesExceptionsThroughFuture) {
  ThreadPool pool(1);
  auto future = pool.submit([]() -> int { throw std::runtime_error("task failed"); });
  EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, StopIsIdempotentAndDrainsQueuedTasks) {
  ThreadPool pool(2);
  std::atomic<int> counter{0};
  for (int i = 0; i < 10; ++i) {
    std::ignore = pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
  }
  pool.stop();
  pool.stop();  // must not hang or crash
  EXPECT_EQ(counter.load(), 10);
}

TEST(ThreadPoolTest, SubmitAfterStopThrows) {
  ThreadPool pool(1);
  pool.stop();
  EXPECT_THROW(std::ignore = pool.submit([] {}), std::runtime_error);
}

TEST(ThreadPoolTest, ThreadCountIsAtLeastOne) {
  ThreadPool pool(0);
  EXPECT_GE(pool.thread_count(), 1u);
}

}  // namespace
}  // namespace flowforge::engine
