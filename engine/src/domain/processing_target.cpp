#include "flowforge/domain/processing_target.hpp"

namespace flowforge::domain {

std::string_view to_string(ProcessingTarget target) noexcept {
  switch (target) {
    case ProcessingTarget::Users:
      return "users";
    case ProcessingTarget::Products:
      return "products";
    case ProcessingTarget::Categories:
      return "categories";
  }
  return "unknown";
}

std::optional<ProcessingTarget> processing_target_from_string(std::string_view value) noexcept {
  if (value == "users")
    return ProcessingTarget::Users;
  if (value == "products")
    return ProcessingTarget::Products;
  if (value == "categories")
    return ProcessingTarget::Categories;
  return std::nullopt;
}

std::string_view job_type_for_processing_target(ProcessingTarget target) noexcept {
  switch (target) {
    case ProcessingTarget::Users:
      return "user.process";
    case ProcessingTarget::Products:
      return "product.process";
    case ProcessingTarget::Categories:
      return "category.process";
  }
  return "";
}

}  // namespace flowforge::domain
