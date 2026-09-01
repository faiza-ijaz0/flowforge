#include "flowforge/engine/handler_registry.hpp"

namespace flowforge::engine {

Result<void> HandlerRegistry::register_handler(std::shared_ptr<IJobHandler> handler) {
  if (!handler) {
    return std::unexpected(make_error(ErrorCode::Validation, "handler must not be null"));
  }
  const std::string job_type(handler->job_type());
  if (job_type.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "handler job_type must not be empty"));
  }

  std::unique_lock lock(mutex_);
  auto [it, inserted] = handlers_.try_emplace(job_type, std::move(handler));
  if (!inserted) {
    return std::unexpected(
        make_error(ErrorCode::Conflict, "a handler is already registered for job type '" + job_type + "'"));
  }
  return {};
}

Result<std::shared_ptr<IJobHandler>> HandlerRegistry::resolve(const std::string& job_type) const {
  std::shared_lock lock(mutex_);
  auto it = handlers_.find(job_type);
  if (it == handlers_.end()) {
    return std::unexpected(
        make_error(ErrorCode::NotFound, "no handler registered for job type '" + job_type + "'"));
  }
  return it->second;
}

bool HandlerRegistry::contains(const std::string& job_type) const {
  std::shared_lock lock(mutex_);
  return handlers_.contains(job_type);
}

std::vector<std::string> HandlerRegistry::registered_types() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> types;
  types.reserve(handlers_.size());
  for (const auto& [type, handler] : handlers_) {
    types.push_back(type);
  }
  return types;
}

}  // namespace flowforge::engine
