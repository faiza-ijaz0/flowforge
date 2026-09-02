#include "flowforge/services/user_import_parser.hpp"

#include <optional>
#include <unordered_map>

#include "flowforge/infra/csv.hpp"

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

}  // namespace

Result<ParsedUserImport> parse_user_import_csv(std::string_view csv_content) {
  if (csv_content.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV file is empty"));
  }
  if (csv_content.size() > kMaxCsvFileBytes) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "CSV file must be <= " + std::to_string(kMaxCsvFileBytes) + " bytes"));
  }

  const std::string_view content = infra::strip_utf8_bom(csv_content);

  if (!infra::is_valid_utf8(content)) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV file must be valid UTF-8"));
  }

  // Generic tokenization (quoting/row-splitting/row-count bound) is
  // shared with any future business-domain CSV import -- see
  // engine/include/flowforge/infra/csv.hpp. Everything below this point
  // (column names, per-row validation, user-record semantics) is what
  // makes this parser specifically about *users*.
  auto rows = infra::tokenize_csv(content, kMaxUserImportRows + 1);
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
