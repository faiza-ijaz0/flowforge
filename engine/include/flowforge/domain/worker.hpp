#pragma once

#include <cstdint>
#include <string>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

enum class WorkerStatus : std::uint8_t { Idle, Busy, Offline };

[[nodiscard]] std::string_view to_string(WorkerStatus status) noexcept;

/// Metadata about a worker process/thread pool participating in job
/// execution. This is a passive record today (updated by whatever
/// component owns worker lifecycle in Phase 2); it does not itself run
/// anything.
class Worker {
 public:
  Worker(infra::WorkerId id, std::string hostname, infra::TimePoint registered_at)
      : id_(std::move(id)),
        hostname_(std::move(hostname)),
        registered_at_(registered_at),
        last_heartbeat_(registered_at) {}

  [[nodiscard]] const infra::WorkerId& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& hostname() const noexcept { return hostname_; }
  [[nodiscard]] WorkerStatus status() const noexcept { return status_; }
  [[nodiscard]] infra::TimePoint registered_at() const noexcept { return registered_at_; }
  [[nodiscard]] infra::TimePoint last_heartbeat() const noexcept { return last_heartbeat_; }

  void heartbeat(infra::TimePoint now) { last_heartbeat_ = now; }
  void set_status(WorkerStatus status) { status_ = status; }

 private:
  infra::WorkerId id_;
  std::string hostname_;
  WorkerStatus status_ = WorkerStatus::Idle;
  infra::TimePoint registered_at_;
  infra::TimePoint last_heartbeat_;
};

}  // namespace flowforge::domain
