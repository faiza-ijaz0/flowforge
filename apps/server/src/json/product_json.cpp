#include "json/product_json.hpp"

#include "time_format.hpp"

namespace flowforge::server {

nlohmann::json to_json(const domain::Product& product) {
  return nlohmann::json{
      {"id", product.id.value()},
      {"sku", product.sku},
      {"name", product.name},
      {"price", product.price},
      {"currency", product.currency},
      {"category", product.category ? nlohmann::json(*product.category) : nlohmann::json(nullptr)},
      {"description", product.description ? nlohmann::json(*product.description) : nlohmann::json(nullptr)},
      {"stock_quantity", product.stock_quantity},
      {"job_id", product.job_id ? nlohmann::json(product.job_id->value()) : nlohmann::json(nullptr)},
      {"created_at", to_iso8601(product.created_at)},
      {"updated_at", to_iso8601(product.updated_at)},
  };
}

}  // namespace flowforge::server
