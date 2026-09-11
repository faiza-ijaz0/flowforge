#include "flowforge/providers/tesseract_ocr_provider.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include "flowforge/infra/ids.hpp"
#include "flowforge/infra/image_format.hpp"
#include "flowforge/infra/subprocess.hpp"

namespace flowforge::providers {

namespace {

namespace fs = std::filesystem;

/// Deletes every path it was constructed with, best-effort, on
/// destruction -- guarantees the temporary input/output files `recognize`
/// creates are cleaned up on every return path (success, validation
/// failure, OCR failure, timeout) without a `try`/`catch` at every one of
/// them. See docs/architecture/input-processing.md, "Security: temporary
/// file handling".
class TempFileGuard {
 public:
  explicit TempFileGuard(std::vector<fs::path> paths) : paths_(std::move(paths)) {}
  ~TempFileGuard() {
    for (const auto& path : paths_) {
      std::error_code ignored;
      fs::remove(path, ignored);
    }
  }
  TempFileGuard(const TempFileGuard&) = delete;
  TempFileGuard& operator=(const TempFileGuard&) = delete;
  TempFileGuard(TempFileGuard&&) = delete;
  TempFileGuard& operator=(TempFileGuard&&) = delete;

 private:
  std::vector<fs::path> paths_;
};

/// Splits one Tesseract TSV data line into its 11 leading tab-separated
/// numeric/id fields plus a final `text` field holding everything after
/// the 11th tab (rather than splitting `text` on tabs too) -- defensive
/// against the unlikely case of a recognized word containing a literal
/// tab character.
struct TsvFields {
  std::vector<std::string_view> leading;
  std::string_view text;
};

std::optional<TsvFields> split_tsv_line(std::string_view line) {
  TsvFields fields;
  std::size_t start = 0;
  for (int i = 0; i < 11; ++i) {
    const std::size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      return std::nullopt;
    }
    fields.leading.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  fields.text = line.substr(start);
  return fields;
}

template <typename T>
std::optional<T> parse_number(std::string_view text) {
  T value{};
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

TesseractCliOcrProvider::TesseractCliOcrProvider(std::string executable_path,
                                                 std::chrono::milliseconds timeout)
    : executable_path_(std::move(executable_path)), timeout_(timeout) {}

std::optional<std::string> TesseractCliOcrProvider::discover_executable() {
  std::vector<std::string> candidates;
  if (const char* override_path = std::getenv("FLOWFORGE_TESSERACT_PATH");
      override_path != nullptr && *override_path != '\0') {
    candidates.emplace_back(override_path);
  }
#ifdef _WIN32
  candidates.emplace_back("tesseract.exe");
  candidates.emplace_back("C:\\Program Files\\Tesseract-OCR\\tesseract.exe");
  candidates.emplace_back("C:\\Program Files (x86)\\Tesseract-OCR\\tesseract.exe");
#else
  candidates.emplace_back("tesseract");
  candidates.emplace_back("/usr/bin/tesseract");
  candidates.emplace_back("/usr/local/bin/tesseract");
#endif

  for (const auto& candidate : candidates) {
    auto probed = infra::run_subprocess(candidate, {"--version"}, std::chrono::milliseconds(3000));
    if (probed && !probed->timed_out && probed->exit_code == 0) {
      return candidate;
    }
  }
  return std::nullopt;
}

Result<engine::OcrResult> TesseractCliOcrProvider::recognize(std::string_view image_bytes) const {
  auto format = infra::validate_image(image_bytes);
  if (!format) {
    return std::unexpected(format.error());
  }

  const std::string uid = infra::generate_uuid_v4();
  const fs::path temp_dir = fs::temp_directory_path();
  const fs::path input_path =
      temp_dir / ("flowforge_ocr_" + uid + "." + std::string(infra::to_string(*format)));
  const fs::path output_base = temp_dir / ("flowforge_ocr_" + uid + "_out");
  const fs::path output_path = temp_dir / (output_base.filename().string() + ".tsv");
  const TempFileGuard cleanup({input_path, output_path});

  {
    std::ofstream out(input_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(make_error(ErrorCode::Infrastructure, "failed to write temporary image file"));
    }
    out.write(image_bytes.data(), static_cast<std::streamsize>(image_bytes.size()));
    if (!out) {
      return std::unexpected(make_error(ErrorCode::Infrastructure, "failed to write temporary image file"));
    }
  }

  auto spawned = infra::run_subprocess(
      executable_path_, {input_path.string(), output_base.string(), "-l", "eng", "--psm", "6", "tsv"},
      timeout_);
  if (!spawned) {
    return std::unexpected(spawned.error());
  }
  if (spawned->timed_out) {
    return std::unexpected(
        make_error(ErrorCode::Infrastructure, "OCR engine timed out processing the image"));
  }
  if (spawned->exit_code != 0) {
    return std::unexpected(make_error(ErrorCode::Infrastructure, "OCR engine failed to process the image"));
  }

  std::ifstream in(output_path, std::ios::binary);
  if (!in) {
    return std::unexpected(make_error(ErrorCode::Infrastructure, "OCR engine produced no output"));
  }

  engine::OcrResult result;
  std::string line;
  std::getline(in, line);  // header row: "level\tpage_num\t...\ttext"
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    auto fields = split_tsv_line(line);
    if (!fields) {
      continue;
    }
    // Column order (Tesseract's fixed TSV schema): level, page_num,
    // block_num, par_num, line_num, word_num, left, top, width, height,
    // conf, text. Only level 5 ("word") rows carry recognized text --
    // levels 1-4 are page/block/paragraph/line summary rows this
    // extractor has no use for.
    auto level = parse_number<int>(fields->leading[0]);
    if (!level || *level != 5 || fields->text.empty()) {
      continue;
    }
    auto block_num = parse_number<int>(fields->leading[2]);
    auto par_num = parse_number<int>(fields->leading[3]);
    auto line_num = parse_number<int>(fields->leading[4]);
    auto left = parse_number<int>(fields->leading[6]);
    auto top = parse_number<int>(fields->leading[7]);
    auto width = parse_number<int>(fields->leading[8]);
    auto height = parse_number<int>(fields->leading[9]);
    auto conf = parse_number<double>(fields->leading[10]);
    if (!block_num || !par_num || !line_num || !left || !top || !width || !height || !conf) {
      continue;
    }
    result.words.push_back(engine::OcrWord{.text = std::string(fields->text),
                                           .left = *left,
                                           .top = *top,
                                           .width = *width,
                                           .height = *height,
                                           .confidence = *conf,
                                           .block_num = *block_num,
                                           .par_num = *par_num,
                                           .line_num = *line_num});
  }

  return result;
}

}  // namespace flowforge::providers
