#include "flowforge/extractors/image_extractor.hpp"

#include <algorithm>
#include <unordered_map>

#include "flowforge/infra/image_format.hpp"

namespace flowforge::extractors {

namespace {

/// Deliberately smaller than `CsvExtractor`'s `kMaxRecords` (1000): a
/// screenshot of a business table is realistically dozens, not hundreds,
/// of rows, and OCR is far more compute-expensive per record than CSV
/// tokenization -- see docs/architecture/input-processing.md, "Limits".
constexpr std::size_t kMaxRecords = 200;
constexpr std::size_t kMaxReportedRejectedRecords = 50;

struct Line {
  std::vector<engine::OcrWord> words;
};

/// Groups OCR words into visual lines using the provider's own
/// block/paragraph/line hierarchy, preserving first-seen order (already
/// the OCR engine's natural reading order) rather than re-sorting by
/// position -- re-sorting by `top` alone would misorder multi-column
/// layouts where paragraphs interleave.
std::vector<Line> group_into_lines(const std::vector<engine::OcrWord>& words) {
  std::vector<Line> lines;
  std::unordered_map<long long, std::size_t> index_by_key;
  for (const auto& word : words) {
    const long long key = (static_cast<long long>(word.block_num) << 40) ^
                          (static_cast<long long>(word.par_num) << 20) ^
                          static_cast<long long>(word.line_num);
    auto [it, inserted] = index_by_key.try_emplace(key, lines.size());
    if (inserted) {
      lines.emplace_back();
    }
    lines[it->second].words.push_back(word);
  }
  return lines;
}

/// Splits one line's words into columns: consecutive words are joined
/// into the same column (space-separated) unless the horizontal gap
/// between them exceeds `gap_threshold` -- a proxy for "this is a column
/// separator, not normal inter-word spacing", scaled to the recognized
/// text's own size rather than a fixed pixel count so it holds up across
/// image resolutions. See docs/architecture/input-processing.md, "Table
/// reconstruction heuristic" for the rationale and its acknowledged
/// limits.
std::vector<std::string> split_into_columns(const Line& line, double gap_threshold) {
  std::vector<engine::OcrWord> words = line.words;
  std::sort(words.begin(), words.end(), [](const auto& a, const auto& b) { return a.left < b.left; });

  std::vector<std::string> columns;
  std::string current;
  int previous_right = 0;
  bool first = true;
  for (const auto& word : words) {
    if (first) {
      current = word.text;
      first = false;
    } else if (static_cast<double>(word.left - previous_right) > gap_threshold) {
      columns.push_back(current);
      current = word.text;
    } else {
      current += ' ';
      current += word.text;
    }
    previous_right = word.left + word.width;
  }
  if (!first) {
    columns.push_back(current);
  }
  return columns;
}

std::string trim(std::string_view s) {
  const std::size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  const std::size_t end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

}  // namespace

Result<domain::ExtractionResult> ImageExtractor::extract(const domain::InputPayload& payload) const {
  if (payload.source_type != domain::InputSourceType::Image &&
      payload.source_type != domain::InputSourceType::Screenshot) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "ImageExtractor only supports InputSourceType::Image/Screenshot"));
  }

  auto format = infra::validate_image(payload.content);
  if (!format) {
    return std::unexpected(format.error());
  }

  auto ocr = ocr_provider_->recognize(payload.content);
  if (!ocr) {
    return std::unexpected(ocr.error());
  }
  if (ocr->words.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "no text was detected in the image"));
  }

  // Confidence: mean over every word the OCR engine reported a
  // non-negative confidence for (Tesseract uses -1 for words it can't
  // score) -- generic extraction metadata, not business-specific.
  double confidence_sum = 0.0;
  std::size_t confidence_count = 0;
  double height_sum = 0.0;
  for (const auto& word : ocr->words) {
    if (word.confidence >= 0.0) {
      confidence_sum += word.confidence;
      ++confidence_count;
    }
    height_sum += static_cast<double>(word.height);
  }
  const double average_height = height_sum / static_cast<double>(ocr->words.size());
  // Column-separator gaps in a real table are comfortably wider than
  // normal inter-word spacing; 2x the mean recognized word height is a
  // conservative multiple validated against this phase's fixture image
  // (~50px threshold vs. ~10-15px real inter-word gaps and ~110-290px
  // real column gaps -- see docs/architecture/input-processing.md).
  const double gap_threshold = average_height * 2.0;

  const auto lines = group_into_lines(ocr->words);

  std::vector<std::vector<std::string>> rows;
  rows.reserve(lines.size());
  bool any_multi_column = false;
  for (const auto& line : lines) {
    auto columns = split_into_columns(line, gap_threshold);
    if (columns.size() > 1) {
      any_multi_column = true;
    }
    rows.push_back(std::move(columns));
  }

  if (rows.size() < 2 || !any_multi_column) {
    return std::unexpected(
        make_error(ErrorCode::Validation,
                   "no table structure was detected in the image -- expected a header row and at "
                   "least one data row split across two or more columns"));
  }
  if (rows.size() - 1 > kMaxRecords) {
    return std::unexpected(make_error(ErrorCode::Validation, "image contains too many table rows (max " +
                                                                 std::to_string(kMaxRecords) + ")"));
  }

  std::vector<std::string> header;
  header.reserve(rows[0].size());
  for (const auto& cell : rows[0]) {
    header.push_back(trim(cell));
  }

  domain::ExtractionResult result;
  if (confidence_count > 0) {
    result.average_confidence = confidence_sum / static_cast<double>(confidence_count);
  }
  result.total_records = rows.size() - 1;

  for (std::size_t i = 1; i < rows.size(); ++i) {
    const auto& row = rows[i];
    if (row.size() != header.size()) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back({.index = i,
                                           .reason = "expected " + std::to_string(header.size()) +
                                                     " columns, found " + std::to_string(row.size())});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }
    domain::StructuredRecord record;
    for (std::size_t col = 0; col < header.size(); ++col) {
      record.fields.emplace(header[col], trim(row[col]));
    }
    result.records.push_back(std::move(record));
  }

  if (result.average_confidence && *result.average_confidence < 70.0) {
    result.warnings.push_back("OCR confidence for this image is low (" +
                              std::to_string(static_cast<int>(*result.average_confidence)) +
                              "%) -- extracted data may contain errors; please review before confirming");
  }

  return result;
}

}  // namespace flowforge::extractors
