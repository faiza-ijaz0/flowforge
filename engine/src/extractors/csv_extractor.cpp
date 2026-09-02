#include "flowforge/extractors/csv_extractor.hpp"

#include <unordered_set>

#include "flowforge/infra/csv.hpp"

namespace flowforge::extractors {

namespace {

/// Generic defaults, independent of any single business domain's own
/// bounds (e.g. `services::kMaxUserImportRows`) -- `extractors::` sits
/// below `services::` in this codebase's dependency direction (mirrors
/// `handlers::`), so it cannot reference a `services::` constant even if
/// the numbers happen to match. See docs/architecture/
/// input-processing.md, "Limits".
constexpr std::size_t kMaxCsvBytes = std::size_t{2} * 1024 * 1024;
constexpr std::size_t kMaxRecords = 1000;
constexpr std::size_t kMaxReportedRejectedRecords = 200;

}  // namespace

Result<domain::ExtractionResult> CsvExtractor::extract(const domain::InputPayload& payload) const {
  if (payload.source_type != domain::InputSourceType::Csv) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "CsvExtractor only supports InputSourceType::Csv"));
  }
  if (payload.content.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV input is empty"));
  }
  if (payload.content.size() > kMaxCsvBytes) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "CSV input must be <= " + std::to_string(kMaxCsvBytes) + " bytes"));
  }

  const std::string_view content = infra::strip_utf8_bom(payload.content);
  if (!infra::is_valid_utf8(content)) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV input must be valid UTF-8"));
  }

  auto rows = infra::tokenize_csv(content, kMaxRecords + 1);
  if (!rows) {
    return std::unexpected(rows.error());
  }
  if (rows->empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "CSV input must contain a header row"));
  }

  const std::vector<std::string>& header = (*rows)[0];
  std::unordered_set<std::string> seen_columns;
  for (const auto& column : header) {
    if (!seen_columns.insert(column).second) {
      return std::unexpected(
          make_error(ErrorCode::Validation, "CSV header has duplicate column '" + column + "'"));
    }
  }

  domain::ExtractionResult result;
  result.total_records = rows->size() - 1;

  for (std::size_t i = 1; i < rows->size(); ++i) {
    const std::vector<std::string>& row = (*rows)[i];
    if (row.size() != header.size()) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back({.index = i,
                                           .reason = "expected " + std::to_string(header.size()) +
                                                     " fields, found " + std::to_string(row.size())});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    domain::StructuredRecord record;
    for (std::size_t col = 0; col < header.size(); ++col) {
      record.fields.emplace(header[col], row[col]);
    }
    result.records.push_back(std::move(record));
  }

  return result;
}

}  // namespace flowforge::extractors
