#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace flowforge::engine {

/// Thread-safe, bounded, priority-ordered queue. Sibling of `BlockingQueue`
/// (blocking_queue.hpp) -- same mutex/condition-variable/`close()` shutdown
/// pattern -- but `pop()` always returns the highest-priority item
/// currently queued (per `Compare`) rather than FIFO order. `Compare`
/// follows `std::priority_queue`'s convention: `Compare(a, b) == true`
/// means "a comes out *after* b" (a has lower priority); the
/// highest-priority element is the one for which `Compare` returns false
/// against every other element, exactly like `std::priority_queue::top()`
/// (this class is backed by one).
///
/// Unlike `BlockingQueue`, there is no unbounded mode and no blocking
/// `push()`: `try_push()` is the only way in, and it never blocks --
/// callers needing hard backpressure (reject immediately when full,
/// rather than block the calling thread) use this directly. `capacity`
/// must be > 0.
template <typename T, typename Compare = std::less<T>>
class PriorityBlockingQueue {
 public:
  explicit PriorityBlockingQueue(std::size_t capacity) : capacity_(capacity) {}

  /// Non-blocking. Returns false (item not accepted) if the queue is
  /// closed or already at capacity.
  bool try_push(T item) {
    std::unique_lock lock(mutex_);
    if (closed_ || items_.size() >= capacity_) {
      return false;
    }
    items_.push(std::move(item));
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
    // std::priority_queue::top() returns a const&; moving out of it is
    // safe here specifically because pop() (which restores the heap
    // invariant) happens immediately afterward, with no intervening
    // top()/push() call that could observe the moved-from element.
    T item = std::move(const_cast<T&>(items_.top()));
    items_.pop();
    return item;
  }

  /// Signals that no more items will be pushed. Already-queued items can
  /// still be popped; once drained, pop() returns std::nullopt.
  /// Irreversible -- there is no way to reopen a closed queue.
  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
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
  std::priority_queue<T, std::vector<T>, Compare> items_;
  std::size_t capacity_;
  bool closed_ = false;
};

}  // namespace flowforge::engine
