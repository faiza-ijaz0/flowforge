#pragma once

#include "flowforge/engine/job_handler.hpp"

namespace flowforge::handlers {

/// Simplest possible handler: returns the payload unchanged as output.
/// Exists primarily to prove the handler abstraction end-to-end
/// (registration, resolution, execution) with no business logic to get
/// wrong. Stateless, so a single instance is safe to share across
/// concurrent invocations (see IJobHandler's thread-safety note).
class EchoHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "echo";

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;
};

}  // namespace flowforge::handlers
