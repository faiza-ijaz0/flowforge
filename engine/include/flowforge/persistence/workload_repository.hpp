#pragma once

#include <vector>

#include "flowforge/domain/workload.hpp"
#include "flowforge/infra/ids.hpp"
#include "flowforge/result.hpp"

namespace flowforge::persistence {

/// Persistence boundary for workloads (database/migrations/0012). Mirrors
/// `IJobRepository`'s shape and conventions.
///
/// Deliberately narrower than `IJobRepository`: there is no `update()`.
/// A workload's persisted row (id/type/total_items/timestamps) is never
/// mutated after creation in this phase -- `completed_items()`/
/// `failed_items()`/`status()` are computed on demand from the workload's
/// child `Job` rows (see `domain::Workload`'s class comment and
/// `services::WorkloadService::get_workload`), not written back here. A
/// future phase that adds a workload-level mutation (e.g. cancellation)
/// would add `update()` then, not before -- see
/// docs/architecture/workload-model.md, "Why there is no update()".
class IWorkloadRepository {
 public:
  IWorkloadRepository() = default;
  virtual ~IWorkloadRepository() = default;
  IWorkloadRepository(const IWorkloadRepository&) = delete;
  IWorkloadRepository& operator=(const IWorkloadRepository&) = delete;
  IWorkloadRepository(IWorkloadRepository&&) = delete;
  IWorkloadRepository& operator=(IWorkloadRepository&&) = delete;

  /// Persists `workload`'s base row (id/type/total_items/timestamps only).
  virtual Result<void> insert(const domain::Workload& workload) = 0;

  /// Returns the workload with completed_items()/failed_items()/status()
  /// at their defaults (0, 0, Pending) -- computing real progress from
  /// child Job rows is the caller's job (see
  /// `services::WorkloadService::get_workload`).
  [[nodiscard]] virtual Result<domain::Workload> find_by_id(const infra::WorkloadId& id) const = 0;

  [[nodiscard]] virtual Result<std::vector<domain::Workload>> list(std::size_t limit,
                                                                   std::size_t offset) const = 0;
};

}  // namespace flowforge::persistence
