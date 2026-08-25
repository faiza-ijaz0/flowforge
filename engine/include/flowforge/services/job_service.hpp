#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flowforge/domain/job.hpp"
#include "flowforge/domain/retry_policy.hpp"
#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/logger.hpp"
#include "flowforge/persistence/job_repository.hpp"
#include "flowforge/result.hpp"

namespace flowforge::services {

struct CreateJobRequest {
  std::string queue_name;
  std::string payload;
  int priority = 0;
  std::optional<domain::RetryPolicy> retry_policy;
};

/// Application-level orchestration for job CRUD, sitting between the
/// transport layer (apps/server's HTTP routes) and the persistence layer.
/// This is where request validation and domain invariants are enforced --
/// route handlers should be a thin translation of HTTP <-> this service,
/// nothing more. Scheduling/execution are intentionally out of scope here
/// (see engine::IScheduler); this phase only supports creating,
/// retrieving, listing, and cancelling job records.
class JobService {
 public:
  JobService(std::shared_ptr<persistence::IJobRepository> repository, std::shared_ptr<infra::Clock> clock,
             std::shared_ptr<infra::Logger> logger)
      : repository_(std::move(repository)), clock_(std::move(clock)), logger_(std::move(logger)) {}

  [[nodiscard]] Result<domain::Job> create_job(const CreateJobRequest& request);
  [[nodiscard]] Result<domain::Job> get_job(const std::string& id) const;
  [[nodiscard]] Result<std::vector<domain::Job>> list_jobs(std::size_t limit, std::size_t offset) const;
  [[nodiscard]] Result<domain::Job> cancel_job(const std::string& id);

 private:
  std::shared_ptr<persistence::IJobRepository> repository_;
  std::shared_ptr<infra::Clock> clock_;
  std::shared_ptr<infra::Logger> logger_;
};

}  // namespace flowforge::services
