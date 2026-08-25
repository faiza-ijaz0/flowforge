#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/persistence/worker_repository.hpp"
#include "flowforge/persistence/workflow_repository.hpp"

namespace flowforge::persistence {

/// Thread-safe, process-local repository implementations. These are real
/// (not test doubles) and currently back the running server -- FlowForge
/// has no PostgreSQL driver wired up yet (see
/// docs/architecture/overview.md). Data does not survive a process
/// restart, which is the primary reason a real database is still Phase 2
/// work; the repository *interface* these implement is what makes that
/// swap possible without touching callers.
class InMemoryJobRepository final : public IJobRepository {
 public:
  Result<void> insert(const domain::Job& job) override;
  [[nodiscard]] Result<domain::Job> find_by_id(const infra::JobId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Job>> list(std::size_t limit, std::size_t offset) const override;
  Result<void> update(const domain::Job& job) override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Job> jobs_by_id_;
  std::vector<std::string> insertion_order_;
};

class InMemoryWorkflowRepository final : public IWorkflowRepository {
 public:
  Result<void> insert(const domain::Workflow& workflow) override;
  [[nodiscard]] Result<domain::Workflow> find_by_id(const infra::WorkflowId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Workflow>> list(std::size_t limit,
                                                           std::size_t offset) const override;
  Result<void> update(const domain::Workflow& workflow) override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Workflow> workflows_by_id_;
  std::vector<std::string> insertion_order_;
};

class InMemoryWorkerRepository final : public IWorkerRepository {
 public:
  Result<void> insert(const domain::Worker& worker) override;
  [[nodiscard]] Result<domain::Worker> find_by_id(const infra::WorkerId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Worker>> list() const override;
  Result<void> update(const domain::Worker& worker) override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Worker> workers_by_id_;
  std::vector<std::string> insertion_order_;
};

}  // namespace flowforge::persistence
