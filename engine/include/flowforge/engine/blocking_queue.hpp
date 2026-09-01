#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace flowforge::engine {

/// Thread-safe, optionally-bounded FIFO queue. This is the low-level
/// concurrency primitive that the future `QueueManager` (job-aware,
/// priority-ordered, backed by persistence) will be built on top of --
/// it deliberately knows nothing about jobs.
///
/// `close()` puts the queue into a state where `pop()` unblocks and
/// returns `std::nullopt` once drained, which is how `ThreadPool` and
/// future consumers implement graceful shutdown without a poison-pill
/// value.
template <typename T>
class BlockingQueue {
 public:
  explicit BlockingQueue(std::size_t capacity = 0) : capacity_(capacity) {}

  /// Blocks if the queue is at capacity (capacity_ == 0 means unbounded).
  /// Returns false if the queue was closed instead of accepting the item.
  bool push(T item) {
    std::unique_lock lock(mutex_);
    not_full_.wait(lock, [&] { return closed_ || capacity_ == 0 || items_.size() < capacity_; });
    if (closed_) {
      return false;
    }
    items_.push_back(std::move(item));
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// Blocks until an item is available or the queue is closed and
  /// drained, in which case it returns std::nullopt.
  std::optional<T> pop() {
    std::unique_lock lock(mutex_);
    not_empty_.wait(lock, [&] { return closed_ || !items_.empty(); });
    if (items_.empty()) {
      return std::nullopt;  // closed_ and drained
    }
    T item = std::move(items_.front());
    items_.pop_front();
    lock.unlock();
    not_full_.notify_one();
    return item;
  }

  /// Non-blocking. Returns false (item not accepted) if the queue is
  /// closed or already at capacity -- callers needing backpressure
  /// (reject immediately rather than block, e.g. `LocalWorkerPool::
  /// dispatch()`) use this instead of `push()`. `capacity_ == 0` means
  /// unbounded, same as `push()`.
  bool try_push(T item) {
    std::unique_lock lock(mutex_);
    if (closed_ || (capacity_ != 0 && items_.size() >= capacity_)) {
      return false;
    }
    items_.push_back(std::move(item));
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// Non-blocking pop; returns std::nullopt if no item is immediately available.
  std::optional<T> try_pop() {
    std::lock_guard lock(mutex_);
    if (items_.empty()) {
      return std::nullopt;
    }
    T item = std::move(items_.front());
    items_.pop_front();
    not_full_.notify_one();
    return item;
  }

  /// Signals that no more items will be pushed. Already-queued items can
  /// still be popped; once drained, pop() returns std::nullopt.
  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] bool closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard lock(mutex_);
    return items_.size();
  }

  [[nodiscard]] bool empty() const {
    std::lock_guard lock(mutex_);
    return items_.empty();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<T> items_;
  std::size_t capacity_;
  bool closed_ = false;
};

}  // namespace flowforge::engine
