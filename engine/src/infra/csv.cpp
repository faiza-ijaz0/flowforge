#include "flowforge/infra/csv.hpp"

namespace flowforge::infra {

std::string_view strip_utf8_bom(std::string_view content) noexcept {
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
    content.remove_prefix(3);
  }
  return content;
}

bool is_valid_utf8(std::string_view s) noexcept {
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t extra = 0;
    if (c <= 0x7F) {
      extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
    } else {
      return false;
    }
    if (i + extra >= s.size()) {
      return false;
    }
    for (std::size_t j = 1; j <= extra; ++j) {
      if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
        return false;
      }
    }
    i += extra + 1;
  }
  return true;
}

Result<std::vector<std::vector<std::string>>> tokenize_csv(std::string_view content, std::size_t max_rows) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> current_row;
  std::string field;
  bool in_quotes = false;
  const std::size_t n = content.size();

  auto end_field = [&] {
    current_row.push_back(std::move(field));
    field.clear();
  };
  auto end_row = [&]() -> Result<void> {
    end_field();
    const bool blank_line = current_row.size() == 1 && current_row[0].empty();
    if (!blank_line) {
      rows.push_back(current_row);
      if (rows.size() > max_rows) {
        return std::unexpected(make_error(
            ErrorCode::Validation,
            "CSV contains more than " + std::to_string(max_rows - 1) + " data rows (including header)"));
      }
    }
    current_row.clear();
    return {};
  };

  std::size_t i = 0;
  while (i < n) {
    const char c = content[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < n && content[i + 1] == '"') {
          field.push_back('"');
          i += 2;
          continue;
        }
        in_quotes = false;
        ++i;
        continue;
      }
      field.push_back(c);
      ++i;
      continue;
    }
    if (c == '"') {
      if (!field.empty()) {
        return std::unexpected(
            make_error(ErrorCode::Validation, "malformed CSV: unexpected '\"' inside an unquoted field"));
      }
      in_quotes = true;
      ++i;
      continue;
    }
    if (c == ',') {
      end_field();
      ++i;
      continue;
    }
    if (c == '\r') {
      if (i + 1 < n && content[i + 1] == '\n') {
        if (auto r = end_row(); !r) {
          return std::unexpected(r.error());
        }
        i += 2;
        continue;
      }
      if (auto r = end_row(); !r) {
        return std::unexpected(r.error());
      }
      ++i;
      continue;
    }
    if (c == '\n') {
      if (auto r = end_row(); !r) {
        return std::unexpected(r.error());
      }
      ++i;
      continue;
    }
    field.push_back(c);
    ++i;
  }
  if (in_quotes) {
    return std::unexpected(make_error(ErrorCode::Validation, "malformed CSV: unterminated quoted field"));
  }
  if (!field.empty() || !current_row.empty()) {
    if (auto r = end_row(); !r) {
      return std::unexpected(r.error());
    }
  }
  return rows;
}

}  // namespace flowforge::infra
