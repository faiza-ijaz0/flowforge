#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "flowforge/engine/execution_manager.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/persistence/category_repository.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/persistence/product_repository.hpp"
#include "flowforge/persistence/user_repository.hpp"
#include "flowforge/persistence/worker_repository.hpp"
#include "flowforge/persistence/workflow_repository.hpp"
#include "flowforge/persistence/workload_repository.hpp"

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
  [[nodiscard]] Result<std::vector<domain::Job>> list_by_workload_id(const infra::WorkloadId& workload_id,
                                                                     std::size_t limit,
                                                                     std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Job> jobs_by_id_;
  std::vector<std::string> insertion_order_;
};

class InMemoryWorkloadRepository final : public IWorkloadRepository {
 public:
  Result<void> insert(const domain::Workload& workload) override;
  [[nodiscard]] Result<domain::Workload> find_by_id(const infra::WorkloadId& id) const override;
  [[nodiscard]] Result<std::vector<domain::Workload>> list(std::size_t limit,
                                                           std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  mutable std::mutex mutex_;
  std::map<std::string, domain::Workload> workloads_by_id_;
  std::vector<std::string> insertion_order_;
};

/// Thread-safe, in-process implementation of `IProductRepository` --
/// mirrors `InMemoryWorkloadRepository`'s conventions, plus the id/
/// timestamp generation a real `upsert()` needs (see that method's own
/// doc comment): unlike the other in-memory repositories, callers never
/// construct a fully-formed `domain::Product` themselves, so this class
/// owns turning a `NormalizedProductRecord` into one -- exactly the
/// division of labor `PostgresProductRepository` has with its `INSERT
/// ... ON CONFLICT` statement's server-side defaults.
class InMemoryProductRepository final : public IProductRepository {
 public:
  explicit InMemoryProductRepository(std::shared_ptr<infra::Clock> clock = infra::make_system_clock())
      : clock_(std::move(clock)) {}

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedProductRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::Product>> find_by_sku(const std::string& sku) const override;
  [[nodiscard]] Result<std::vector<domain::Product>> list(std::size_t limit,
                                                          std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<infra::Clock> clock_;
  mutable std::mutex mutex_;
  std::map<std::string, domain::Product> products_by_id_;
  std::map<std::string, std::string> id_by_sku_;
  std::vector<std::string> insertion_order_;
};

/// Thread-safe, in-process implementation of `ICategoryRepository` --
/// mirrors `InMemoryProductRepository`'s conventions exactly (id/
/// timestamp generation owned here, keyed by `slug` the same way that
/// class is keyed by `sku`).
class InMemoryCategoryRepository final : public ICategoryRepository {
 public:
  explicit InMemoryCategoryRepository(std::shared_ptr<infra::Clock> clock = infra::make_system_clock())
      : clock_(std::move(clock)) {}

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedCategoryRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::Category>> find_by_slug(const std::string& slug) const override;
  [[nodiscard]] Result<std::vector<domain::Category>> list(std::size_t limit,
                                                           std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<infra::Clock> clock_;
  mutable std::mutex mutex_;
  std::map<std::string, domain::Category> categories_by_id_;
  std::map<std::string, std::string> id_by_slug_;
  std::vector<std::string> insertion_order_;
};

/// Thread-safe, in-process implementation of `IUserRepository` (Phase 3H)
/// -- mirrors `InMemoryProductRepository`'s conventions exactly, keyed by
/// `email` the same way that class is keyed by `sku`.
class InMemoryUserRepository final : public IUserRepository {
 public:
  explicit InMemoryUserRepository(std::shared_ptr<infra::Clock> clock = infra::make_system_clock())
      : clock_(std::move(clock)) {}

  Result<void> upsert(const infra::JobId& job_id, const domain::NormalizedUserRecord& record) override;
  [[nodiscard]] Result<std::optional<domain::User>> find_by_email(const std::string& email) const override;
  [[nodiscard]] Result<std::vector<domain::User>> list(std::size_t limit, std::size_t offset) const override;
  [[nodiscard]] Result<std::size_t> count() const override;

 private:
  std::shared_ptr<infra::Clock> clock_;
  mutable std::mutex mutex_;
  std::map<std::string, domain::User> users_by_id_;
  std::map<std::string, std::string> id_by_email_;
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
