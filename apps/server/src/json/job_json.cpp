#include "json/job_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::Job& job) {
  nlohmann::json payload_json;
  try {
    payload_json = nlohmann::json::parse(job.payload());
  } catch (const nlohmann::json::parse_error&) {
    payload_json = job.payload();
  }

  nlohmann::json result{
      {"id", job.id().value()},
      {"queue_name", job.queue_name()},
      {"payload", payload_json},
      {"priority", job.priority()},
      {"status", std::string(domain::to_string(job.status()))},
      {"attempt_count", job.attempt_count()},
      {"max_attempts", job.retry_policy().max_attempts},
      {"created_at", to_iso8601(job.created_at())},
      {"updated_at", to_iso8601(job.updated_at())},
  };
  const auto& last_error = job.last_error();
  result["last_error"] = last_error.has_value() ? nlohmann::json(*last_error) : nlohmann::json(nullptr);
  return result;
}

Result<services::CreateJobRequest> parse_create_job_request(const nlohmann::json& body) {
  if (!body.is_object()) {
    return std::unexpected(make_error(ErrorCode::Validation, "request body must be a JSON object"));
  }

  services::CreateJobRequest request;

  auto queue_it = body.find("queue_name");
  if (queue_it == body.end() || !queue_it->is_string()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'queue_name' must be a string"));
  }
  request.queue_name = queue_it->get<std::string>();

  auto payload_it = body.find("payload");
  if (payload_it == body.end()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'payload' is required"));
  }
  request.payload = payload_it->is_string() ? payload_it->get<std::string>() : payload_it->dump();

  if (auto priority_it = body.find("priority"); priority_it != body.end()) {
    if (!priority_it->is_number_integer()) {
      return std::unexpected(make_error(ErrorCode::Validation, "'priority' must be an integer"));
    }
    request.priority = priority_it->get<int>();
  }

  if (auto retry_it = body.find("retry_policy"); retry_it != body.end()) {
    if (!retry_it->is_object()) {
      return std::unexpected(make_error(ErrorCode::Validation, "'retry_policy' must be an object"));
    }
    domain::RetryPolicy policy;
    if (auto it = retry_it->find("max_attempts"); it != retry_it->end()) {
      if (!it->is_number_unsigned()) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "'retry_policy.max_attempts' must be a non-negative integer"));
      }
      policy.max_attempts = it->get<std::uint32_t>();
    }
    if (auto it = retry_it->find("initial_backoff_ms"); it != retry_it->end()) {
      if (!it->is_number_integer()) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "'retry_policy.initial_backoff_ms' must be an integer"));
      }
      policy.initial_backoff = std::chrono::milliseconds{it->get<std::int64_t>()};
    }
    if (auto it = retry_it->find("max_backoff_ms"); it != retry_it->end()) {
      if (!it->is_number_integer()) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "'retry_policy.max_backoff_ms' must be an integer"));
      }
      policy.max_backoff = std::chrono::milliseconds{it->get<std::int64_t>()};
    }
    if (auto it = retry_it->find("backoff_multiplier"); it != retry_it->end()) {
      if (!it->is_number()) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "'retry_policy.backoff_multiplier' must be a number"));
      }
      policy.backoff_multiplier = it->get<double>();
    }
    request.retry_policy = policy;
  }

  return request;
}

}  // namespace flowforge::server
