#include "json/process_json.hpp"

#include "json/workload_json.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::RejectedRecord& record) {
  return nlohmann::json{{"index", record.index}, {"reason", record.reason}};
}

nlohmann::json to_json(const services::ProcessResult& result) {
  nlohmann::json items = nlohmann::json::array();
  for (const auto& outcome : result.items) {
    items.push_back(to_json(outcome));
  }
  nlohmann::json rejected_records = nlohmann::json::array();
  for (const auto& record : result.rejected_records) {
    rejected_records.push_back(to_json(record));
  }

  nlohmann::json body = to_json(result.workload);
  body["items"] = items;
  body["total_records"] = result.total_records;
  body["valid_records"] = result.valid_records;
  body["invalid_records"] = result.invalid_records;
  body["rejected_records"] = rejected_records;
  body["rejected_records_truncated"] = result.rejected_records_truncated;
  return body;
}

nlohmann::json to_json(const domain::NormalizedUserRecord& record) {
  nlohmann::json body{{"name", record.name}, {"email", record.email}};
  if (record.phone) {
    body["phone"] = *record.phone;
  }
  return body;
}

nlohmann::json to_json(const services::PreviewResult& result) {
  nlohmann::json records = nlohmann::json::array();
  for (const auto& record : result.records) {
    records.push_back(to_json(record));
  }
  nlohmann::json rejected_records = nlohmann::json::array();
  for (const auto& record : result.rejected_records) {
    rejected_records.push_back(to_json(record));
  }
  nlohmann::json warnings = nlohmann::json::array();
  for (const auto& warning : result.warnings) {
    warnings.push_back(warning);
  }

  nlohmann::json body{
      {"source", std::string(domain::to_string(result.source_type))},
      {"target", std::string(domain::to_string(result.target))},
      {"total_records", result.total_records},
      {"valid_records", result.records.size()},
      {"invalid_records", result.rejected_records.size()},
      {"records", records},
      {"rejected_records", rejected_records},
      {"rejected_records_truncated", result.rejected_records_truncated},
      {"warnings", warnings},
  };
  if (result.average_confidence) {
    body["average_confidence"] = *result.average_confidence;
  } else {
    body["average_confidence"] = nullptr;
  }
  return body;
}

Result<services::ConfirmRequest> parse_confirm_request(const nlohmann::json& body) {
  if (!body.is_object()) {
    return std::unexpected(make_error(ErrorCode::Validation, "request body must be a JSON object"));
  }

  auto target_it = body.find("target");
  if (target_it == body.end() || !target_it->is_string()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'target' must be a string"));
  }
  auto target = domain::processing_target_from_string(target_it->get<std::string>());
  if (!target) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "unrecognized 'target' value '" + target_it->get<std::string>() + "'"));
  }

  auto records_it = body.find("records");
  if (records_it == body.end() || !records_it->is_array() || records_it->empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'records' must be a non-empty array"));
  }

  services::ConfirmRequest request;
  request.target = *target;
  request.records.reserve(records_it->size());
  for (const auto& item : *records_it) {
    if (!item.is_object()) {
      return std::unexpected(
          make_error(ErrorCode::Validation, "each element of 'records' must be an object"));
    }
    auto name_it = item.find("name");
    auto email_it = item.find("email");
    if (name_it == item.end() || !name_it->is_string() || email_it == item.end() || !email_it->is_string()) {
      return std::unexpected(
          make_error(ErrorCode::Validation, "each record must have string 'name' and 'email' fields"));
    }
    domain::NormalizedUserRecord record;
    record.name = name_it->get<std::string>();
    record.email = email_it->get<std::string>();
    if (auto phone_it = item.find("phone"); phone_it != item.end() && phone_it->is_string()) {
      record.phone = phone_it->get<std::string>();
    }
    request.records.push_back(std::move(record));
  }

  return request;
}

}  // namespace flowforge::server
