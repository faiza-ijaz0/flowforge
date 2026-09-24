#pragma once

#include <compare>
#include <functional>
#include <string>
#include <string_view>

namespace flowforge::infra {

/// Generates a random UUIDv4 string (RFC 4122, version 4, variant 1).
///
/// Implemented locally with `<random>` instead of pulling in a UUID
/// library: the requirement is "unique enough identifier for a job/worker
/// record", not cryptographic randomness or RFC-perfect parsing/formatting
/// support, so a small self-contained generator is the appropriate amount
/// of engineering for this phase.
[[nodiscard]] std::string generate_uuid_v4();

/// True when `value` is a canonical 8-4-4-4-12 hex UUID (either case).
/// Used at the HTTP boundary to reject malformed path IDs with a 400
/// before they reach a UUID-typed PostgreSQL column -- which would
/// otherwise fail the cast and surface as a 500 (Phase 3H security
/// probe finding). Deliberately does not check version/variant bits:
/// any UUID PostgreSQL would accept as an id is a well-formed request.
[[nodiscard]] bool is_uuid(std::string_view value) noexcept;

/// Strongly-typed identifier wrapper. Prevents accidentally passing a
/// JobId where a WorkerId is expected -- both are "just a string"
/// underneath, but the type system should still catch the mix-up.
template <typename Tag>
class Id {
 public:
  Id() = default;
  explicit Id(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] static Id generate() { return Id{generate_uuid_v4()}; }

  [[nodiscard]] const std::string& value() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  friend auto operator<=>(const Id&, const Id&) = default;

 private:
  std::string value_;
};

struct JobIdTag {};
struct WorkflowIdTag {};
struct WorkflowStepIdTag {};
struct WorkerIdTag {};
struct ExecutionIdTag {};
struct WorkloadIdTag {};
struct ProductIdTag {};
struct CategoryIdTag {};
struct UserIdTag {};

using JobId = Id<JobIdTag>;
using WorkflowId = Id<WorkflowIdTag>;
using WorkflowStepId = Id<WorkflowStepIdTag>;
using WorkerId = Id<WorkerIdTag>;
using ExecutionId = Id<ExecutionIdTag>;
using WorkloadId = Id<WorkloadIdTag>;
using ProductId = Id<ProductIdTag>;
using CategoryId = Id<CategoryIdTag>;
using UserId = Id<UserIdTag>;

}  // namespace flowforge::infra

template <typename Tag>
struct std::hash<flowforge::infra::Id<Tag>> {
  std::size_t operator()(const flowforge::infra::Id<Tag>& id) const noexcept {
    return std::hash<std::string>{}(id.value());
  }
};
