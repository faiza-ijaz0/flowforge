#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "flowforge/engine/job_handler.hpp"
#include "flowforge/result.hpp"

namespace flowforge::engine {

/// Explicit, constructed-and-injected mapping from job type ->
/// `IJobHandler`. Not a singleton/global: the composition root that owns
/// job execution (the future Executor -- see docs/architecture/
/// execution-model.md) constructs one instance and passes it by
/// reference/shared_ptr to whatever needs to resolve handlers, the same
/// way `persistence::create_repositories`'s output is threaded through
/// explicitly rather than reached for via a global (see
/// docs/architecture/overview.md §7.7).
///
/// Thread-safety: registration is expected to happen once, at startup,
/// before any worker begins dispatching jobs -- but `resolve()` and
/// `contains()` are safe to call from many worker threads concurrently
/// at any time, including concurrently with a registration, via a
/// `shared_mutex`: lookups take a shared (reader) lock, registration
/// takes a unique (writer) lock. This keeps the hot lookup path
/// (`resolve`) allocation-free and lock-cheap (multiple readers proceed
/// in parallel) without requiring registration to be complete before any
/// reader can safely call in.
class HandlerRegistry {
 public:
  HandlerRegistry() = default;
  ~HandlerRegistry() = default;
  HandlerRegistry(const HandlerRegistry&) = delete;
  HandlerRegistry& operator=(const HandlerRegistry&) = delete;
  HandlerRegistry(HandlerRegistry&&) = delete;
  HandlerRegistry& operator=(HandlerRegistry&&) = delete;

  /// Registers `handler` under `handler->job_type()`. Fails with
  /// `ErrorCode::Validation` if `handler` is null or `job_type()` is
  /// empty, or `ErrorCode::Conflict` if a handler is already registered
  /// for that job type -- overwriting an existing registration is never
  /// silent.
  Result<void> register_handler(std::shared_ptr<IJobHandler> handler);

  /// Returns the handler registered for `job_type`, or
  /// `ErrorCode::NotFound` if none is registered. An unrecognized job
  /// type is an expected, operational outcome (e.g. a job record
  /// referencing a handler that was never registered), not a thrown
  /// exception.
  [[nodiscard]] Result<std::shared_ptr<IJobHandler>> resolve(const std::string& job_type) const;

  [[nodiscard]] bool contains(const std::string& job_type) const;

  /// Snapshot of currently-registered job types, in unspecified order.
  /// Intended for diagnostics/tests, not a hot path.
  [[nodiscard]] std::vector<std::string> registered_types() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<IJobHandler>> handlers_;
};

}  // namespace flowforge::engine
