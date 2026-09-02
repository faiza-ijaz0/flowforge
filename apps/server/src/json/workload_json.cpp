#include "json/workload_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::Workload& workload) {
  return nlohmann::json{
      {"id", workload.id().value()},
      {"type", workload.type()},
      {"status", std::string(domain::to_string(workload.status()))},
      {"total_items", workload.total_items()},
      {"completed_items", workload.completed_items()},
      {"failed_items", workload.failed_items()},
      {"created_at", to_iso8601(workload.created_at())},
      {"updated_at", to_iso8601(workload.updated_at())},
  };
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
