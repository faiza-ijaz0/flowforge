#include "flowforge/engine/priority_blocking_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace flowforge::engine {
namespace {

struct Item {
  int priority;
  int sequence;
  std::string label;
};

struct ItemOrder {
  [[nodiscard]] bool operator()(const Item& a, const Item& b) const noexcept {
    if (a.priority != b.priority) {
      return a.priority < b.priority;
    }
    return a.sequence > b.sequence;
  }
};

using ItemQueue = PriorityBlockingQueue<Item, ItemOrder>;

TEST(PriorityBlockingQueueTest, TryPushThenPopReturnsSameValue) {
  ItemQueue queue(4);
  ASSERT_TRUE(queue.try_push({.priority = 0, .sequence = 0, .label = "a"}));
  auto value = queue.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->label, "a");
}

TEST(PriorityBlockingQueueTest, HigherPriorityDispatchedFirst) {
  ItemQueue queue(8);
  ASSERT_TRUE(queue.try_push({.priority = 0, .sequence = 0, .label = "low"}));
  ASSERT_TRUE(queue.try_push({.priority = 10, .sequence = 1, .label = "high"}));
  ASSERT_TRUE(queue.try_push({.priority = 5, .sequence = 2, .label = "medium"}));

  EXPECT_EQ(queue.pop()->label, "high");
  EXPECT_EQ(queue.pop()->label, "medium");
  EXPECT_EQ(queue.pop()->label, "low");
}

TEST(PriorityBlockingQueueTest, EqualPriorityPreservesFifoOrderBySequence) {
  ItemQueue queue(8);
  // Pushed out of sequence order on purpose -- ordering must come from the
  // `sequence` field, not insertion order into this test's push() calls.
  ASSERT_TRUE(queue.try_push({.priority = 1, .sequence = 2, .label = "third"}));
  ASSERT_TRUE(queue.try_push({.priority = 1, .sequence = 0, .label = "first"}));
  ASSERT_TRUE(queue.try_push({.priority = 1, .sequence = 1, .label = "second"}));

  EXPECT_EQ(queue.pop()->label, "first");
  EXPECT_EQ(queue.pop()->label, "second");
  EXPECT_EQ(queue.pop()->label, "third");
}

TEST(PriorityBlockingQueueTest, TryPushFailsAtCapacity) {
  ItemQueue queue(1);
  ASSERT_TRUE(queue.try_push({.priority = 0, .sequence = 0, .label = "a"}));
  EXPECT_FALSE(queue.try_push({.priority = 0, .sequence = 1, .label = "b"}));
  EXPECT_EQ(queue.size(), 1u);
}

TEST(PriorityBlockingQueueTest, PoppingFreesCapacityForFurtherPushes) {
  ItemQueue queue(1);
  ASSERT_TRUE(queue.try_push({.priority = 0, .sequence = 0, .label = "a"}));
  ASSERT_TRUE(queue.pop().has_value());
  EXPECT_TRUE(queue.try_push({.priority = 0, .sequence = 1, .label = "b"}));
}

TEST(PriorityBlockingQueueTest, CloseDrainsRemainingItemsThenReturnsNullopt) {
  ItemQueue queue(4);
  ASSERT_TRUE(queue.try_push({.priority = 5, .sequence = 0, .label = "keep"}));
  queue.close();

  auto value = queue.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->label, "keep");
  EXPECT_FALSE(queue.pop().has_value());
}

TEST(PriorityBlockingQueueTest, TryPushAfterCloseIsRejected) {
  ItemQueue queue(4);
  queue.close();
  EXPECT_FALSE(queue.try_push({.priority = 0, .sequence = 0, .label = "a"}));
}

TEST(PriorityBlockingQueueTest, PopBlocksUntilItemIsPushed) {
  ItemQueue queue(4);
  std::atomic<bool> popped{false};

  std::thread consumer([&] {
    auto value = queue.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->label, "late");
    popped = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(popped.load());
  queue.try_push({.priority = 0, .sequence = 0, .label = "late"});
  consumer.join();
  EXPECT_TRUE(popped.load());
}

TEST(PriorityBlockingQueueTest, ConcurrentPushAndPopLosesNoItems) {
  ItemQueue queue(64);
  constexpr int kProducers = 8;
  constexpr int kItemsPerProducer = 200;
  constexpr int kTotal = kProducers * kItemsPerProducer;

  std::atomic<int> popped_count{0};
  std::thread consumer([&] {
    while (popped_count.load(std::memory_order_relaxed) < kTotal) {
      if (queue.pop().has_value()) {
        popped_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < kItemsPerProducer; ++i) {
        while (!queue.try_push({.priority = p, .sequence = i, .label = "x"})) {
          std::this_thread::yield();
        }
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  consumer.join();

  EXPECT_EQ(popped_count.load(), kTotal);
}

}  // namespace
}  // namespace flowforge::engine
