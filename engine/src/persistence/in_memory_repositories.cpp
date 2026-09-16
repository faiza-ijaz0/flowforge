#include "flowforge/persistence/in_memory_repositories.hpp"

#include <algorithm>

namespace flowforge::persistence {

// --- InMemoryJobRepository ---------------------------------------------

Result<void> InMemoryJobRepository::insert(const domain::Job& job) {
  std::lock_guard lock(mutex_);
  const std::string& id = job.id().value();
  if (jobs_by_id_.contains(id)) {
    return std::unexpected(make_error(ErrorCode::Conflict, "job with id '" + id + "' already exists"));
  }
  jobs_by_id_.emplace(id, job);
  insertion_order_.push_back(id);
  return {};
}

Result<domain::Job> InMemoryJobRepository::find_by_id(const infra::JobId& id) const {
  std::lock_guard lock(mutex_);
  auto it = jobs_by_id_.find(id.value());
  if (it == jobs_by_id_.end()) {
    return std::unexpected(make_error(ErrorCode::NotFound, "job with id '" + id.value() + "' was not found"));
  }
  return it->second;
}

Result<std::vector<domain::Job>> InMemoryJobRepository::list(std::size_t limit, std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Job> result;
  if (offset >= insertion_order_.size()) {
    return result;
  }
  const std::size_t end = std::min(insertion_order_.size(), offset + limit);
  result.reserve(end - offset);
  for (std::size_t i = offset; i < end; ++i) {
    result.push_back(jobs_by_id_.at(insertion_order_[i]));
  }
  return result;
}

Result<void> InMemoryJobRepository::update(const domain::Job& job) {
  std::lock_guard lock(mutex_);
  const std::string& id = job.id().value();
  auto it = jobs_by_id_.find(id);
  if (it == jobs_by_id_.end()) {
    return std::unexpected(make_error(ErrorCode::NotFound, "job with id '" + id + "' was not found"));
  }
  it->second = job;
  return {};
}

Result<std::vector<domain::Job>> InMemoryJobRepository::list_by_status(domain::JobStatus status,
                                                                       std::size_t limit) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Job> result;
  for (const auto& id : insertion_order_) {
    if (result.size() >= limit) {
      break;
    }
    const domain::Job& job = jobs_by_id_.at(id);
    if (job.status() == status) {
      result.push_back(job);
    }
  }
  return result;
}

Result<std::vector<domain::Job>> InMemoryJobRepository::list_by_workload_id(
    const infra::WorkloadId& workload_id, std::size_t limit, std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Job> result;
  std::size_t skipped = 0;
  for (const auto& id : insertion_order_) {
    if (result.size() >= limit) {
      break;
    }
    const domain::Job& job = jobs_by_id_.at(id);
    if (!job.workload_id().has_value() || *job.workload_id() != workload_id) {
      continue;
    }
    if (skipped < offset) {
      ++skipped;
      continue;
    }
    result.push_back(job);
  }
  return result;
}

// --- InMemoryWorkloadRepository -------------------------------------------

Result<void> InMemoryWorkloadRepository::insert(const domain::Workload& workload) {
  std::lock_guard lock(mutex_);
  const std::string& id = workload.id().value();
  if (workloads_by_id_.contains(id)) {
    return std::unexpected(make_error(ErrorCode::Conflict, "workload with id '" + id + "' already exists"));
  }
  workloads_by_id_.emplace(id, workload);
  insertion_order_.push_back(id);
  return {};
}

Result<domain::Workload> InMemoryWorkloadRepository::find_by_id(const infra::WorkloadId& id) const {
  std::lock_guard lock(mutex_);
  auto it = workloads_by_id_.find(id.value());
  if (it == workloads_by_id_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "workload with id '" + id.value() + "' was not found"));
  }
  return it->second;
}

Result<std::vector<domain::Workload>> InMemoryWorkloadRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Workload> result;
  if (offset >= insertion_order_.size()) {
    return result;
  }
  const std::size_t end = std::min(insertion_order_.size(), offset + limit);
  result.reserve(end - offset);
  for (std::size_t i = offset; i < end; ++i) {
    result.push_back(workloads_by_id_.at(insertion_order_[i]));
  }
  return result;
}

// --- InMemoryProductRepository -------------------------------------------

Result<void> InMemoryProductRepository::upsert(const infra::JobId& job_id,
                                               const domain::NormalizedProductRecord& record) {
  std::lock_guard lock(mutex_);
  const auto now = clock_->now();
  auto existing_it = id_by_sku_.find(record.sku);
  if (existing_it != id_by_sku_.end()) {
    domain::Product& product = products_by_id_.at(existing_it->second);
    product.name = record.name;
    product.price = record.price;
    product.currency = record.currency;
    product.category = record.category;
    product.description = record.description;
    product.stock_quantity = record.stock_quantity;
    product.job_id = job_id;
    product.updated_at = now;
    return {};
  }

  domain::Product product{.id = infra::ProductId::generate(),
                          .sku = record.sku,
                          .name = record.name,
                          .price = record.price,
                          .currency = record.currency,
                          .category = record.category,
                          .description = record.description,
                          .stock_quantity = record.stock_quantity,
                          .job_id = job_id,
                          .created_at = now,
                          .updated_at = now};
  const std::string id = product.id.value();
  id_by_sku_.emplace(record.sku, id);
  products_by_id_.emplace(id, std::move(product));
  insertion_order_.push_back(id);
  return {};
}

Result<std::optional<domain::Product>> InMemoryProductRepository::find_by_sku(const std::string& sku) const {
  std::lock_guard lock(mutex_);
  auto it = id_by_sku_.find(sku);
  if (it == id_by_sku_.end()) {
    return std::optional<domain::Product>(std::nullopt);
  }
  return std::optional<domain::Product>(products_by_id_.at(it->second));
}

Result<std::vector<domain::Product>> InMemoryProductRepository::list(std::size_t limit,
                                                                     std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Product> result;
  if (offset >= insertion_order_.size()) {
    return result;
  }
  const std::size_t end = std::min(insertion_order_.size(), offset + limit);
  result.reserve(end - offset);
  for (std::size_t i = offset; i < end; ++i) {
    result.push_back(products_by_id_.at(insertion_order_[i]));
  }
  return result;
}

Result<std::size_t> InMemoryProductRepository::count() const {
  std::lock_guard lock(mutex_);
  return insertion_order_.size();
}

// --- InMemoryCategoryRepository -------------------------------------------

Result<void> InMemoryCategoryRepository::upsert(const infra::JobId& job_id,
                                                const domain::NormalizedCategoryRecord& record) {
  std::lock_guard lock(mutex_);
  const auto now = clock_->now();
  auto existing_it = id_by_slug_.find(record.slug);
  if (existing_it != id_by_slug_.end()) {
    domain::Category& category = categories_by_id_.at(existing_it->second);
    category.name = record.name;
    category.description = record.description;
    category.parent_slug = record.parent_slug;
    category.job_id = job_id;
    category.updated_at = now;
    return {};
  }

  domain::Category category{.id = infra::CategoryId::generate(),
                            .name = record.name,
                            .slug = record.slug,
                            .description = record.description,
                            .parent_slug = record.parent_slug,
                            .job_id = job_id,
                            .created_at = now,
                            .updated_at = now};
  const std::string id = category.id.value();
  id_by_slug_.emplace(record.slug, id);
  categories_by_id_.emplace(id, std::move(category));
  insertion_order_.push_back(id);
  return {};
}

Result<std::optional<domain::Category>> InMemoryCategoryRepository::find_by_slug(
    const std::string& slug) const {
  std::lock_guard lock(mutex_);
  auto it = id_by_slug_.find(slug);
  if (it == id_by_slug_.end()) {
    return std::optional<domain::Category>(std::nullopt);
  }
  return std::optional<domain::Category>(categories_by_id_.at(it->second));
}

Result<std::vector<domain::Category>> InMemoryCategoryRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Category> result;
  if (offset >= insertion_order_.size()) {
    return result;
  }
  const std::size_t end = std::min(insertion_order_.size(), offset + limit);
  result.reserve(end - offset);
  for (std::size_t i = offset; i < end; ++i) {
    result.push_back(categories_by_id_.at(insertion_order_[i]));
  }
  return result;
}

Result<std::size_t> InMemoryCategoryRepository::count() const {
  std::lock_guard lock(mutex_);
  return insertion_order_.size();
}

// --- InMemoryWorkflowRepository ------------------------------------------

Result<void> InMemoryWorkflowRepository::insert(const domain::Workflow& workflow) {
  std::lock_guard lock(mutex_);
  const std::string& id = workflow.id().value();
  if (workflows_by_id_.contains(id)) {
    return std::unexpected(make_error(ErrorCode::Conflict, "workflow with id '" + id + "' already exists"));
  }
  workflows_by_id_.emplace(id, workflow);
  insertion_order_.push_back(id);
  return {};
}

Result<domain::Workflow> InMemoryWorkflowRepository::find_by_id(const infra::WorkflowId& id) const {
  std::lock_guard lock(mutex_);
  auto it = workflows_by_id_.find(id.value());
  if (it == workflows_by_id_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "workflow with id '" + id.value() + "' was not found"));
  }
  return it->second;
}

Result<std::vector<domain::Workflow>> InMemoryWorkflowRepository::list(std::size_t limit,
                                                                       std::size_t offset) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Workflow> result;
  if (offset >= insertion_order_.size()) {
    return result;
  }
  const std::size_t end = std::min(insertion_order_.size(), offset + limit);
  result.reserve(end - offset);
  for (std::size_t i = offset; i < end; ++i) {
    result.push_back(workflows_by_id_.at(insertion_order_[i]));
  }
  return result;
}

Result<void> InMemoryWorkflowRepository::update(const domain::Workflow& workflow) {
  std::lock_guard lock(mutex_);
  const std::string& id = workflow.id().value();
  auto it = workflows_by_id_.find(id);
  if (it == workflows_by_id_.end()) {
    return std::unexpected(make_error(ErrorCode::NotFound, "workflow with id '" + id + "' was not found"));
  }
  it->second = workflow;
  return {};
}

// --- InMemoryWorkerRepository ---------------------------------------------

Result<void> InMemoryWorkerRepository::insert(const domain::Worker& worker) {
  std::lock_guard lock(mutex_);
  const std::string& id = worker.id().value();
  if (workers_by_id_.contains(id)) {
    return std::unexpected(make_error(ErrorCode::Conflict, "worker with id '" + id + "' already exists"));
  }
  workers_by_id_.emplace(id, worker);
  insertion_order_.push_back(id);
  return {};
}

Result<domain::Worker> InMemoryWorkerRepository::find_by_id(const infra::WorkerId& id) const {
  std::lock_guard lock(mutex_);
  auto it = workers_by_id_.find(id.value());
  if (it == workers_by_id_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "worker with id '" + id.value() + "' was not found"));
  }
  return it->second;
}

Result<std::vector<domain::Worker>> InMemoryWorkerRepository::list() const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Worker> result;
  result.reserve(insertion_order_.size());
  for (const auto& id : insertion_order_) {
    result.push_back(workers_by_id_.at(id));
  }
  return result;
}

Result<void> InMemoryWorkerRepository::update(const domain::Worker& worker) {
  std::lock_guard lock(mutex_);
  const std::string& id = worker.id().value();
  auto it = workers_by_id_.find(id);
  if (it == workers_by_id_.end()) {
    return std::unexpected(make_error(ErrorCode::NotFound, "worker with id '" + id + "' was not found"));
  }
  it->second = worker;
  return {};
}

// --- InMemoryExecutionRepository -------------------------------------------

Result<void> InMemoryExecutionRepository::record(const domain::Execution& execution) {
  std::lock_guard lock(mutex_);
  const std::string& id = execution.id.value();
  if (executions_by_id_.contains(id)) {
    return std::unexpected(make_error(ErrorCode::Conflict, "execution with id '" + id + "' already exists"));
  }
  executions_by_id_.emplace(id, execution);
  execution_ids_by_job_id_[execution.job_id.value()].push_back(id);
  return {};
}

Result<std::vector<domain::Execution>> InMemoryExecutionRepository::history_for(
    const infra::JobId& job_id) const {
  std::lock_guard lock(mutex_);
  std::vector<domain::Execution> result;
  auto it = execution_ids_by_job_id_.find(job_id.value());
  if (it == execution_ids_by_job_id_.end()) {
    return result;
  }
  result.reserve(it->second.size());
  for (const auto& execution_id : it->second) {
    result.push_back(executions_by_id_.at(execution_id));
  }
  return result;
}

}  // namespace flowforge::persistence
