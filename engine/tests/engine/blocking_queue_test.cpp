#include "flowforge/engine/blocking_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace flowforge::engine {
namespace {

TEST(BlockingQueueTest, PushThenPopReturnsSameValue) {
  BlockingQueue<int> queue;
  ASSERT_TRUE(queue.push(42));
  auto value = queue.pop();
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);
}

TEST(BlockingQueueTest, PreservesFifoOrder) {
  BlockingQueue<int> queue;
  for (int i = 0; i < 5; ++i) {
    queue.push(i);
  }
  for (int i = 0; i < 5; ++i) {
    auto value = queue.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, i);
  }
}

TEST(BlockingQueueTest, TryPopReturnsNulloptWhenEmpty) {
  BlockingQueue<int> queue;
  EXPECT_FALSE(queue.try_pop().has_value());
}

TEST(BlockingQueueTest, TryPushSucceedsUnderCapacity) {
  BlockingQueue<int> queue(2);
  EXPECT_TRUE(queue.try_push(1));
  EXPECT_TRUE(queue.try_push(2));
  EXPECT_EQ(queue.size(), 2u);
}

TEST(BlockingQueueTest, TryPushFailsAtCapacity) {
  BlockingQueue<int> queue(1);
  ASSERT_TRUE(queue.try_push(1));
  EXPECT_FALSE(queue.try_push(2));
  EXPECT_EQ(queue.size(), 1u);
}

TEST(BlockingQueueTest, TryPushFailsAfterClose) {
  BlockingQueue<int> queue;
  queue.close();
  EXPECT_FALSE(queue.try_push(1));
}

TEST(BlockingQueueTest, TryPushOnUnboundedQueueNeverRejectsForCapacity) {
  BlockingQueue<int> queue;  // capacity_ == 0 means unbounded.
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(queue.try_push(i));
  }
}

TEST(BlockingQueueTest, CloseDrainsRemainingItemsThenReturnsNullopt) {
  BlockingQueue<int> queue;
  queue.push(1);
  queue.push(2);
  queue.close();

  EXPECT_EQ(queue.pop().value(), 1);
  EXPECT_EQ(queue.pop().value(), 2);
  EXPECT_FALSE(queue.pop().has_value());
}

TEST(BlockingQueueTest, PushAfterCloseIsRejected) {
  BlockingQueue<int> queue;
  queue.close();
  EXPECT_FALSE(queue.push(1));
}

TEST(BlockingQueueTest, PopBlocksUntilItemIsPushed) {
  BlockingQueue<int> queue;
  std::atomic<bool> popped{false};

  std::thread consumer([&] {
    auto value = queue.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 99);
    popped = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(popped.load());
  queue.push(99);
  consumer.join();
  EXPECT_TRUE(popped.load());
}

TEST(BlockingQueueTest, BoundedQueueBlocksPushAtCapacity) {
  BlockingQueue<int> queue(1);
  ASSERT_TRUE(queue.push(1));

  std::atomic<bool> second_push_completed{false};
  std::thread producer([&] {
    queue.push(2);
    second_push_completed = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(second_push_completed.load());

  EXPECT_EQ(queue.pop().value(), 1);
  producer.join();
  EXPECT_TRUE(second_push_completed.load());
  EXPECT_EQ(queue.pop().value(), 2);
}

}  // namespace
}  // namespace flowforge::engine
