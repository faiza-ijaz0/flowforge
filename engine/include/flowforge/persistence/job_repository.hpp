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

  /// Returns up to `limit` jobs currently in `status`, in an unspecified
  /// but stable order (oldest-updated first in both implementations).
  /// Added for Phase 2B-4's `engine::RetryDispatcher` (`status ==
  /// JobStatus::Retrying`), which needs to find retry-eligible jobs
  /// without a full-table `list()` scan -- `jobs.status` already has an
  /// index (`idx_jobs_status`, migration 0005) from Phase 1, so no new
  /// migration is needed for this query to be efficient.
  [[nodiscard]] virtual Result<std::vector<domain::Job>> list_by_status(domain::JobStatus status,
                                                                        std::size_t limit) const = 0;

  /// Returns up to `limit` jobs (skipping the first `offset`) whose
  /// `workload_id()` equals `workload_id`, in an unspecified but stable
  /// order (creation order in both implementations). Added for Phase 3A's
  /// `services::WorkloadService`, which uses this (offset always 0, limit
  /// = the workload's total_items) to compute a workload's live progress
  /// from its child jobs' current statuses (see docs/architecture/
  /// workload-model.md, "Status derivation") rather than maintaining
  /// separately-updated counters. `offset` was added in Phase 3B for
  /// `WorkloadService::list_items`'s paginated per-item view (see
  /// docs/architecture/user-import.md, "Bounded item retrieval").
  /// `jobs.workload_id` has an index (`idx_jobs_workload_id`, migration
  /// 0013), so this is not a full-table scan.
  [[nodiscard]] virtual Result<std::vector<domain::Job>> list_by_workload_id(
      const infra::WorkloadId& workload_id, std::size_t limit, std::size_t offset) const = 0;
};

}  // namespace flowforge::persistence
