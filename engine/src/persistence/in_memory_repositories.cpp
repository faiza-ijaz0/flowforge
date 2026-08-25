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

}  // namespace flowforge::persistence
