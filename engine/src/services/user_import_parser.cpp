#include "flowforge/services/user_import_parser.hpp"

#include <optional>
#include <unordered_map>

namespace flowforge::services {

namespace {

/// Generous margin over `kMaxUserImportRows`' worth of small records --
/// bounds worst-case memory for a single upload regardless of what
/// `httplib::Server::set_payload_max_length` (a coarser, server-wide
/// safety net -- see apps/server/src/http/app.cpp) is configured to.
constexpr std::size_t kMaxCsvFileBytes = std::size_t{2} * 1024 * 1024;

/// Caps how many individual row-rejection reasons are kept for the API
/// response -- `rejected_row_count`/`total_rows` on `ParsedUserImport`
/// remain exact regardless. See docs/architecture/user-import.md, "Why
/// rejected_rows is bounded".
constexpr std::size_t kMaxReportedRejectedRows = 200;

/// Structural (not full Unicode-semantic) UTF-8 well-formedness check:
/// every multi-byte sequence's continuation bytes are well-formed. Enough
/// to guarantee downstream JSON serialization (nlohmann::json, which
/// requires valid UTF-8 for string values) never throws on this content --
/// not a complete validator (does not reject overlong encodings or
/// surrogate code points), which is an accepted, documented limitation.
bool is_valid_utf8(std::string_view s) {
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

/// Single-pass, RFC 4180-style CSV tokenizer: handles quoted fields
/// (`""` as an escaped quote, commas/newlines inside quotes), `\r\n`/`\n`/
/// bare-`\r` row endings, and skips wholly-blank lines (a common trailing-
/// newline artifact) without counting them as rows. Aborts as soon as the
/// row count would exceed `max_rows` (header included) -- bounded work,
/// not a full tokenize-then-check pass. A raw `"` appearing inside an
/// already-started unquoted field is rejected as malformed rather than
/// silently accepted -- a deliberate strictness choice (see
/// docs/architecture/user-import.md, "Known limitations").
Result<std::vector<std::vector<std::string>>> tokenize_csv_rows(std::string_view content,
                                                                std::size_t max_rows) {
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

}  // namespace

Result<ParsedUserImport> parse_user_import_csv(std::string_view csv_content) {
  if (csv_content.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV file is empty"));
  }
  if (csv_content.size() > kMaxCsvFileBytes) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "CSV file must be <= " + std::to_string(kMaxCsvFileBytes) + " bytes"));
  }

  // A UTF-8 BOM (EF BB BF) is a common artifact of CSVs exported from
  // spreadsheet tools -- stripped before parsing rather than treated as
  // part of the first header column's name.
  std::string_view content = csv_content;
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB && static_cast<unsigned char>(content[2]) == 0xBF) {
    content.remove_prefix(3);
  }

  if (!is_valid_utf8(content)) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV file must be valid UTF-8"));
  }

  auto rows = tokenize_csv_rows(content, kMaxUserImportRows + 1);
  if (!rows) {
    return std::unexpected(rows.error());
  }
  if (rows->empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV file must contain a header row"));
  }

  // --- Header validation --------------------------------------------
  const std::vector<std::string>& header = (*rows)[0];
  std::optional<std::size_t> name_col;
  std::optional<std::size_t> email_col;
  std::optional<std::size_t> phone_col;
  for (std::size_t col = 0; col < header.size(); ++col) {
    const std::string& raw = header[col];
    if (raw == "name") {
      if (name_col) {
        return std::unexpected(make_error(ErrorCode::Validation, "CSV header has duplicate column 'name'"));
      }
      name_col = col;
    } else if (raw == "email") {
      if (email_col) {
        return std::unexpected(make_error(ErrorCode::Validation, "CSV header has duplicate column 'email'"));
      }
      email_col = col;
    } else if (raw == "phone") {
      if (phone_col) {
        return std::unexpected(make_error(ErrorCode::Validation, "CSV header has duplicate column 'phone'"));
      }
      phone_col = col;
    } else {
      return std::unexpected(make_error(
          ErrorCode::Validation,
          "CSV header has an unexpected column '" + raw + "' -- only name, email, phone are supported"));
    }
  }
  if (!name_col) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "CSV header is missing the required 'name' column"));
  }
  if (!email_col) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "CSV header is missing the required 'email' column"));
  }

  // --- Data rows -------------------------------------------------------
  ParsedUserImport result;
  result.total_rows = rows->size() - 1;

  std::unordered_map<std::string, std::size_t> first_row_by_email;
  auto reject = [&](std::size_t row_number, std::string reason) {
    ++result.rejected_row_count;
    if (result.rejected_rows.size() < kMaxReportedRejectedRows) {
      result.rejected_rows.push_back({.row_number = row_number, .reason = std::move(reason)});
    } else {
      result.rejected_rows_truncated = true;
    }
  };

  for (std::size_t i = 1; i < rows->size(); ++i) {
    const std::size_t row_number = i;
    const std::vector<std::string>& fields = (*rows)[i];
    if (fields.size() != header.size()) {
      reject(row_number,
             "expected " + std::to_string(header.size()) + " fields, found " + std::to_string(fields.size()));
      continue;
    }

    const std::string& raw_name = fields[*name_col];
    const std::string& raw_email = fields[*email_col];
    const std::optional<std::string_view> raw_phone =
        phone_col ? std::optional<std::string_view>(fields[*phone_col]) : std::nullopt;

    auto normalized = domain::validate_and_normalize_user_record(raw_name, raw_email, raw_phone);
    if (!normalized) {
      reject(row_number, normalized.error().message());
      continue;
    }

    if (auto [it, inserted] = first_row_by_email.try_emplace(normalized->email, row_number); !inserted) {
      reject(row_number, "duplicate email (first seen at row " + std::to_string(it->second) + ")");
      continue;
    }

    result.valid_rows.push_back(std::move(*normalized));
  }

  return result;
}

}  // namespace flowforge::services
