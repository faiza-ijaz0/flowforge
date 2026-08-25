#include "flowforge/domain/worker.hpp"

namespace flowforge::domain {

std::string_view to_string(WorkerStatus status) noexcept {
  switch (status) {
    case WorkerStatus::Idle:
      return "idle";
    case WorkerStatus::Busy:
      return "busy";
    case WorkerStatus::Offline:
      return "offline";
  }
  return "unknown";
}

}  // namespace flowforge::domain
