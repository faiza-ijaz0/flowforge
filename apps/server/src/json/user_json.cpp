#include "json/user_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::User& user) {
  return nlohmann::json{
      {"id", user.id.value()},
      {"name", user.name},
      {"email", user.email},
      {"phone", user.phone ? nlohmann::json(*user.phone) : nlohmann::json(nullptr)},
      {"job_id", user.job_id ? nlohmann::json(user.job_id->value()) : nlohmann::json(nullptr)},
      {"created_at", to_iso8601(user.created_at)},
      {"updated_at", to_iso8601(user.updated_at)},
  };
}

}  // namespace flowforge::server
