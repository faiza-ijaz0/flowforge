#include "json/category_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::Category& category) {
  return nlohmann::json{
      {"id", category.id.value()},
      {"name", category.name},
      {"slug", category.slug},
      {"description", category.description ? nlohmann::json(*category.description) : nlohmann::json(nullptr)},
      {"parent_slug", category.parent_slug ? nlohmann::json(*category.parent_slug) : nlohmann::json(nullptr)},
      {"job_id", category.job_id ? nlohmann::json(category.job_id->value()) : nlohmann::json(nullptr)},
      {"created_at", to_iso8601(category.created_at)},
      {"updated_at", to_iso8601(category.updated_at)},
  };
}

}  // namespace flowforge::server
