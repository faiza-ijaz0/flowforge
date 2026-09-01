#pragma once

#include "flowforge/engine/job_handler.hpp"

namespace flowforge::handlers {

/// Deterministic text transform: uppercases the payload (e.g. "hello
/// world" -> "HELLO WORLD"). Deliberately not a general-purpose
/// transform pipeline -- see IJobHandler's ban on arbitrary code
/// execution -- just enough logic to prove a handler can do real,
/// non-trivial-but-bounded work on its payload. Stateless.
class TransformHandler final : public engine::IJobHandler {
 public:
  static constexpr std::string_view kJobType = "transform";

  [[nodiscard]] std::string_view job_type() const noexcept override { return kJobType; }
  [[nodiscard]] Result<domain::ExecutionResult> execute(const engine::ExecutionContext& context,
                                                        const std::string& payload) override;
};

}  // namespace flowforge::handlers
