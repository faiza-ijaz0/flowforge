#include "flowforge/handlers/category_process_handler.hpp"

#include <chrono>
#include <optional>
#include <unordered_set>

#include "flowforge/domain/category_record.hpp"
#include "flowforge/infra/json_lite.hpp"

namespace flowforge::handlers {

namespace {

constexpr std::size_t kMaxPayloadBytes = std::size_t{16} * 1024;

// Defensive bound on how many links of a parent chain are walked before
// giving up -- the cycle check below makes an unbounded chain impossible
// to construct through this handler, so this only guards against a
// pathological chain slipping in some other way (e.g. a future direct
// database edit) turning into an unbounded loop.
constexpr int kMaxParentChainDepth = 64;

using infra::extract_json_string_field;

}  // namespace

Result<domain::ExecutionResult> CategoryProcessHandler::execute(const engine::ExecutionContext& context,
                                                                const std::string& payload) {
  if (payload.size() > kMaxPayloadBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "category.process payload must be <= " + std::to_string(kMaxPayloadBytes) + " bytes"));
  }

  const auto start = std::chrono::steady_clock::now();

  if (context.is_cancelled()) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return domain::ExecutionResult::failure(ErrorCode::JobExecution,
                                            "category.process cancelled before execution",
                                            /*retryable=*/false, duration);
  }

  auto raw_name = extract_json_string_field(payload, "name");
  if (!raw_name) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' is required and must be a JSON string"));
  }
  auto raw_slug = extract_json_string_field(payload, "slug");
  auto raw_description = extract_json_string_field(payload, "description");
  auto raw_parent_slug = extract_json_string_field(payload, "parent_slug");

  // Shared with the Processing Center's preview/confirm path (see
  // domain/category_record.hpp's class comment) -- "what makes a valid
  // category record" is defined exactly once. This only catches a
  // self-referencing parent, not a missing/cyclic one -- see below.
  auto normalized = domain::validate_and_normalize_category_record(
      *raw_name, raw_slug ? std::optional<std::string_view>(*raw_slug) : std::nullopt,
      raw_description ? std::optional<std::string_view>(*raw_description) : std::nullopt,
      raw_parent_slug ? std::optional<std::string_view>(*raw_parent_slug) : std::nullopt);
  if (!normalized) {
    return std::unexpected(normalized.error());
  }

  // Parent-existence and cycle validation -- see this handler's class
  // comment, "Parent validation". Pure/self-reference was already ruled
  // out by validate_and_normalize_category_record above, so `current`
  // below is never initially equal to normalized->slug.
  if (normalized->parent_slug) {
    std::unordered_set<std::string> visited{normalized->slug};
    std::string current = *normalized->parent_slug;
    for (int depth = 0;; ++depth) {
      if (visited.contains(current)) {
        return std::unexpected(
            make_error(ErrorCode::Validation,
                       "'parent_slug' (\"" + current + "\") would create a cyclic parent relationship"));
      }
      if (depth > kMaxParentChainDepth) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "parent category chain exceeds the maximum supported depth"));
      }
      visited.insert(current);

      auto parent = category_repository_->find_by_slug(current);
      if (!parent) {
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        context.logger().warn("category_process_handler", "parent lookup failed",
                              {{.key = "job_id", .value = context.job_id().value()},
                               {.key = "parent_slug", .value = current},
                               {.key = "reason", .value = parent.error().message()}});
        return domain::ExecutionResult::failure(parent.error().code(), parent.error().message(),
                                                /*retryable=*/true, duration);
      }
      if (!parent->has_value()) {
        return std::unexpected(make_error(
            ErrorCode::Validation, "parent category '" + current +
                                       "' does not exist -- import it first, then re-import this record"));
      }
      if (!(*parent)->parent_slug) {
        break;  // Reached a root category: the chain is valid and acyclic.
      }
      current = *(*parent)->parent_slug;
    }
  }

  auto upserted = category_repository_->upsert(context.job_id(), *normalized);
  if (!upserted) {
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    context.logger().warn("category_process_handler", "category persistence failed",
                          {{.key = "job_id", .value = context.job_id().value()},
                           {.key = "slug", .value = normalized->slug},
                           {.key = "reason", .value = upserted.error().message()}});
    // Unlike a malformed payload (never retryable), a persistence
    // failure may be transient -- see the header's "Retryability" note.
    return domain::ExecutionResult::failure(upserted.error().code(), upserted.error().message(),
                                            /*retryable=*/true, duration);
  }

  context.logger().debug(
      "category_process_handler", "upserted category record",
      {{.key = "job_id", .value = context.job_id().value()}, {.key = "slug", .value = normalized->slug}});

  const std::string output = domain::serialize_category_record_as_job_payload(*normalized);
  const auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return domain::ExecutionResult::success(output, duration, {{"operation", "category_process"}});
}

}  // namespace flowforge::handlers
