#include "json/workload_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::Workload& workload) {
  return nlohmann::json{
      {"id", workload.id().value()},
      {"type", workload.type()},
      {"status", std::string(domain::to_string(workload.status()))},
      {"total_items", workload.total_items()},
      {"queued_items", workload.queued_items()},
      {"running_items", workload.running_items()},
      {"completed_items", workload.completed_items()},
      {"failed_items", workload.failed_items()},
      {"retrying_items", workload.retrying_items()},
      {"dead_letter_items", workload.dead_letter_items()},
      {"created_at", to_iso8601(workload.created_at())},
      {"updated_at", to_iso8601(workload.updated_at())},
  };
}

nlohmann::json to_json(const services::WorkloadItemDispatchOutcome& outcome) {
  nlohmann::json entry{{"job_id", outcome.job_id.value()}, {"scheduled", outcome.scheduled}};
  if (outcome.reason) {
    entry["reason"] = *outcome.reason;
  }
  return entry;
}

nlohmann::json to_json(const services::RejectedImportRow& row) {
  return nlohmann::json{{"row_number", row.row_number}, {"reason", row.reason}};
}

nlohmann::json to_json(const services::UserImportResult& result) {
  nlohmann::json items = nlohmann::json::array();
  for (const auto& outcome : result.items) {
    items.push_back(to_json(outcome));
  }
  nlohmann::json rejected_rows = nlohmann::json::array();
  for (const auto& row : result.rejected_rows) {
    rejected_rows.push_back(to_json(row));
  }

  nlohmann::json body = to_json(result.workload);
  body["items"] = items;
  body["total_rows"] = result.total_rows;
  body["valid_rows"] = result.valid_rows;
  body["invalid_rows"] = result.invalid_rows;
  body["rejected_rows"] = rejected_rows;
  body["rejected_rows_truncated"] = result.rejected_rows_truncated;
  return body;
}

nlohmann::json to_json_workload_item(const domain::Job& job) {
  // job.payload() is the same flat JSON object user.process parses (see
  // domain::serialize_user_record_as_job_payload) for a CSV-imported job
  // -- parsed here best-effort so the item list can show name/email
  // without a second query per item. A job created outside the CSV import
  // path (or any payload that isn't that exact shape) degrades to `null`
  // fields rather than failing the whole response.
  nlohmann::json payload_json;
  bool parsed = false;
  try {
    payload_json = nlohmann::json::parse(job.payload());
    parsed = payload_json.is_object();
  } catch (const nlohmann::json::parse_error&) {
    parsed = false;
  }

  nlohmann::json result{
      {"job_id", job.id().value()},
      {"status", std::string(domain::to_string(job.status()))},
      {"attempt_count", job.attempt_count()},
      {"updated_at", to_iso8601(job.updated_at())},
  };
  result["name"] =
      (parsed && payload_json.contains("name")) ? payload_json.at("name") : nlohmann::json(nullptr);
  result["email"] =
      (parsed && payload_json.contains("email")) ? payload_json.at("email") : nlohmann::json(nullptr);
  const auto& last_error = job.last_error();
  result["last_error"] = last_error.has_value() ? nlohmann::json(*last_error) : nlohmann::json(nullptr);
  return result;
}

Result<services::CreateWorkloadRequest> parse_create_workload_request(const nlohmann::json& body) {
  if (!body.is_object()) {
    return std::unexpected(make_error(ErrorCode::Validation, "request body must be a JSON object"));
  }

  services::CreateWorkloadRequest request;

  auto type_it = body.find("type");
  if (type_it == body.end() || !type_it->is_string()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'type' must be a string"));
  }
  request.type = type_it->get<std::string>();

  auto items_it = body.find("items");
  if (items_it != body.end()) {
    if (!items_it->is_array()) {
      return std::unexpected(make_error(ErrorCode::Validation, "'items' must be an array"));
    }
    request.items.reserve(items_it->size());
    for (const auto& item : *items_it) {
      services::WorkloadItem workload_item;
      workload_item.payload = item.is_string() ? item.get<std::string>() : item.dump();
      request.items.push_back(std::move(workload_item));
    }
  }

  return request;
}

}  // namespace flowforge::server
