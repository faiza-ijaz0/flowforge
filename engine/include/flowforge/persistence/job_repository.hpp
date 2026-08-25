#pragma once

#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// Persistence boundary for jobs. FlowForge codes against this interface
/// everywhere instead of a concrete database client so that (a) the
/// engine/API layers are unit-testable without a real PostgreSQL instance
/// and (b) swapping in the eventual libpqxx-backed implementation (Phase
/// 2 -- see docs/architecture/overview.md) touches only one new file, not
/// every call site. `InMemoryJobRepository` below is a real, thread-safe,
/// fully-functional implementation used both by tests and by the server
/// today; it is not a mock.
class IJobRepository {
 public:
  IJobRepository() = default;
  virtual ~IJobRepository() = default;
  IJobRepository(const IJobRepository&) = delete;
  IJobRepository& operator=(const IJobRepository&) = delete;
  IJobRepository(IJobRepository&&) = delete;
  IJobRepository& operator=(IJobRepository&&) = delete;

  virtual Result<void> insert(const domain::Job& job) = 0;
  [[nodiscard]] virtual Result<domain::Job> find_by_id(const infra::JobId& id) const = 0;
  [[nodiscard]] virtual Result<std::vector<domain::Job>> list(std::size_t limit,
                                                              std::size_t offset) const = 0;
  virtual Result<void> update(const domain::Job& job) = 0;
};

}  // namespace flowforge::persistence
