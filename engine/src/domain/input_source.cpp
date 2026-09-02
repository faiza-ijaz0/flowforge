#include "flowforge/domain/input_source.hpp"

namespace flowforge::domain {

std::string_view to_string(InputSourceType type) noexcept {
  switch (type) {
    case InputSourceType::Csv:
      return "csv";
    case InputSourceType::Image:
      return "image";
    case InputSourceType::Screenshot:
      return "screenshot";
    case InputSourceType::Text:
      return "text";
    case InputSourceType::Url:
      return "url";
  }
  return "unknown";
}

std::optional<InputSourceType> input_source_type_from_string(std::string_view value) noexcept {
  if (value == "csv")
    return InputSourceType::Csv;
  if (value == "image")
    return InputSourceType::Image;
  if (value == "screenshot")
    return InputSourceType::Screenshot;
  if (value == "text")
    return InputSourceType::Text;
  if (value == "url")
    return InputSourceType::Url;
  return std::nullopt;
}

}  // namespace flowforge::domain
