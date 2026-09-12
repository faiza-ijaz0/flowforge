#include "flowforge/infra/json_lite.hpp"

namespace flowforge::infra {

std::optional<std::string> extract_json_string_field(std::string_view payload, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const auto key_pos = payload.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t pos = key_pos + needle.size();
  while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t')) {
    ++pos;
  }
  if (pos >= payload.size() || payload[pos] != ':') {
    return std::nullopt;
  }
  ++pos;
  while (pos < payload.size() && (payload[pos] == ' ' || payload[pos] == '\t')) {
    ++pos;
  }
  if (pos >= payload.size() || payload[pos] != '"') {
    return std::nullopt;
  }
  ++pos;

  std::string value;
  while (pos < payload.size() && payload[pos] != '"') {
    if (payload[pos] == '\\') {
      if (pos + 1 >= payload.size()) {
        return std::nullopt;
      }
      const char escaped = payload[pos + 1];
      if (escaped != '"' && escaped != '\\') {
        return std::nullopt;  // Unsupported escape -- see header's class comment.
      }
      value.push_back(escaped);
      pos += 2;
      continue;
    }
    value.push_back(payload[pos]);
    ++pos;
  }
  if (pos >= payload.size()) {
    return std::nullopt;  // Unterminated string.
  }
  return value;
}

std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace flowforge::infra
