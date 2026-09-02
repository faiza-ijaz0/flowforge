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

}  // namespace flowforge::server
