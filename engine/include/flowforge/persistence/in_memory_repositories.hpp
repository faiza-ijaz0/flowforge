#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "flowforge/engine/execution_manager.hpp"
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
  [[nodiscard]] Result<std::vector<domain::Job>> list_by_status(domain::JobStatus status,
                                                                std::size_t limit) const override;

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

/// Real, thread-safe, in-process implementation of `engine::IExecutionManager`
/// (the `job_attempts` persistence boundary -- see execution_manager.hpp).
/// Declared here alongside the other `InMemory*Repository` types even
/// though the interface it implements lives in `engine::` rather than
/// `persistence::` (a Phase 1 placement choice for that one interface --
/// not worth relitigating by moving it, since doing so touches no
/// behavior). Used as the development/test-mode default, same as the
/// other in-memory repositories.
class InMemoryExecutionRepository final : public engine::IExecutionManager {
 public:
  Result<void> record(const domain::Execution& execution) override;
  [[nodiscard]] Result<std::vector<domain::Execution>> history_for(const infra::JobId& job_id) const override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Execution> executions_by_id_;
  std::map<std::string, std::vector<std::string>> execution_ids_by_job_id_;
};

}  // namespace flowforge::persistence
