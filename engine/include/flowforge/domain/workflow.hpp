#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flowforge/infra/clock.hpp"
#include "flowforge/infra/ids.hpp"

namespace flowforge::domain {

enum class WorkflowStatus : std::uint8_t { Pending, Running, Succeeded, Failed, Cancelled };

[[nodiscard]] std::string_view to_string(WorkflowStatus status) noexcept;

/// A single step in a workflow DAG. `depends_on` references other steps'
/// `id` within the same workflow; the scheduler (Phase 2) is responsible
/// for only starting a step once all of its dependencies have succeeded.
/// Cycle detection belongs to the workflow validation logic that will be
/// added alongside the scheduler, not to this plain data type.
struct WorkflowStep {
  infra::WorkflowStepId id;
  std::string name;
  infra::JobId job_id;
  std::vector<infra::WorkflowStepId> depends_on;
};

class Workflow {
 public:
  Workflow(infra::WorkflowId id, std::string name, std::vector<WorkflowStep> steps,
           infra::TimePoint created_at)
      : id_(std::move(id)),
        name_(std::move(name)),
        steps_(std::move(steps)),
        created_at_(created_at),
        updated_at_(created_at) {}

  [[nodiscard]] const infra::WorkflowId& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] const std::vector<WorkflowStep>& steps() const noexcept { return steps_; }
  [[nodiscard]] WorkflowStatus status() const noexcept { return status_; }
  [[nodiscard]] infra::TimePoint created_at() const noexcept { return created_at_; }
  [[nodiscard]] infra::TimePoint updated_at() const noexcept { return updated_at_; }

  void transition_to(WorkflowStatus status, infra::TimePoint now) {
    status_ = status;
    updated_at_ = now;
  }

 private:
  infra::WorkflowId id_;
  std::string name_;
  std::vector<WorkflowStep> steps_;
  WorkflowStatus status_ = WorkflowStatus::Pending;
  infra::TimePoint created_at_;
  infra::TimePoint updated_at_;
};

}  // namespace flowforge::domain
