#include "flowforge/domain/product_record.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

#include "flowforge/infra/json_lite.hpp"

namespace flowforge::domain {

namespace {

constexpr std::size_t kMaxNameLength = 200;
constexpr std::size_t kMaxSkuLength = 64;
constexpr std::size_t kMaxCategoryLength = 100;
constexpr std::size_t kMaxDescriptionLength = 2000;
constexpr double kMaxPrice = 10'000'000.0;
constexpr std::int64_t kMaxStockQuantity = 10'000'000;

std::string trim(std::string_view value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string to_upper(std::string value) {
  std::ranges::transform(value, value.begin(),
                         [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

/// SKUs are machine-oriented codes, not free text: uppercase letters,
/// digits, `-`, and `_` only. Deliberately not a full catalog/barcode
/// standard (no EAN/UPC checksum, no vendor-prefix convention) -- see
/// docs/architecture/product-processing.md, "Known limitations".
bool looks_like_sku(std::string_view sku) {
  return std::ranges::all_of(sku,
                             [](unsigned char c) { return std::isalnum(c) != 0 || c == '-' || c == '_'; });
}

/// Structural currency check: exactly 3 alphabetic characters. Not a
/// real ISO 4217 registry lookup -- deliberately bounded, mirroring
/// `domain::user_record.cpp`'s `looks_like_email`'s "structural, not
/// exhaustive" rationale (see docs/architecture/product-processing.md,
/// "Known limitations").
bool looks_like_currency_code(std::string_view currency) {
  return currency.size() == 3 &&
         std::ranges::all_of(currency, [](unsigned char c) { return std::isalpha(c) != 0; });
}

/// Rejects a price string with more than 2 digits after a decimal point
/// -- `std::from_chars` alone would silently accept "19.999" as 19.999,
/// which is not a valid amount of money in any currency this system
/// models. A structural, pre-parse check (not a rounding step) so the
/// rejection is honest about what was actually typed/OCR'd, never
/// silently truncated.
bool has_at_most_two_decimal_places(std::string_view value) {
  const auto dot = value.find('.');
  if (dot == std::string_view::npos) {
    return true;
  }
  return value.size() - dot - 1 <= 2;
}

}  // namespace

Result<NormalizedProductRecord> validate_and_normalize_product_record(
    std::string_view sku, std::string_view name, std::string_view price,
    std::optional<std::string_view> currency, std::optional<std::string_view> category,
    std::optional<std::string_view> description, std::optional<std::string_view> stock_quantity) {
  const std::string normalized_sku = to_upper(trim(sku));
  if (normalized_sku.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'sku' must not be blank"));
  }
  if (normalized_sku.size() > kMaxSkuLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'sku' must be <= " + std::to_string(kMaxSkuLength) + " characters"));
  }
  if (!looks_like_sku(normalized_sku)) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'sku' must contain only letters, digits, '-', or '_'"));
  }

  const std::string trimmed_name = trim(name);
  if (trimmed_name.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'name' must not be blank"));
  }
  if (trimmed_name.size() > kMaxNameLength) {
    return std::unexpected(make_error(ErrorCode::Validation,
                                      "'name' must be <= " + std::to_string(kMaxNameLength) + " characters"));
  }

  const std::string trimmed_price = trim(price);
  if (trimmed_price.empty()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'price' must not be blank"));
  }
  if (!has_at_most_two_decimal_places(trimmed_price)) {
    return std::unexpected(make_error(ErrorCode::Validation, "'price' must have at most 2 decimal places"));
  }
  double parsed_price = 0.0;
  const auto price_result =
      std::from_chars(trimmed_price.data(), trimmed_price.data() + trimmed_price.size(), parsed_price);
  if (price_result.ec != std::errc{} || price_result.ptr != trimmed_price.data() + trimmed_price.size()) {
    return std::unexpected(make_error(ErrorCode::Validation, "'price' must be a valid number"));
  }
  if (!std::isfinite(parsed_price) || parsed_price < 0.0 || parsed_price > kMaxPrice) {
    return std::unexpected(
        make_error(ErrorCode::Validation, "'price' must be between 0 and " + std::to_string(kMaxPrice)));
  }

  std::string normalized_currency = currency ? to_upper(trim(*currency)) : std::string();
  if (normalized_currency.empty()) {
    normalized_currency = "USD";
  } else if (!looks_like_currency_code(normalized_currency)) {
    return std::unexpected(make_error(ErrorCode::Validation, "'currency' must be a 3-letter currency code"));
  }

  std::optional<std::string> normalized_category;
  if (category) {
    std::string trimmed_category = trim(*category);
    if (trimmed_category.size() > kMaxCategoryLength) {
      return std::unexpected(
          make_error(ErrorCode::Validation,
                     "'category' must be <= " + std::to_string(kMaxCategoryLength) + " characters"));
    }
    if (!trimmed_category.empty()) {
      normalized_category = std::move(trimmed_category);
    }
  }

  std::optional<std::string> normalized_description;
  if (description) {
    std::string trimmed_description = trim(*description);
    if (trimmed_description.size() > kMaxDescriptionLength) {
      return std::unexpected(
          make_error(ErrorCode::Validation,
                     "'description' must be <= " + std::to_string(kMaxDescriptionLength) + " characters"));
    }
    if (!trimmed_description.empty()) {
      normalized_description = std::move(trimmed_description);
    }
  }

  std::int64_t parsed_stock = 0;
  const std::string trimmed_stock = stock_quantity ? trim(*stock_quantity) : std::string();
  if (!trimmed_stock.empty()) {
    const auto stock_result =
        std::from_chars(trimmed_stock.data(), trimmed_stock.data() + trimmed_stock.size(), parsed_stock);
    if (stock_result.ec != std::errc{} || stock_result.ptr != trimmed_stock.data() + trimmed_stock.size()) {
      return std::unexpected(make_error(ErrorCode::Validation, "'stock_quantity' must be a whole number"));
    }
    if (parsed_stock < 0 || parsed_stock > kMaxStockQuantity) {
      return std::unexpected(make_error(ErrorCode::Validation, "'stock_quantity' must be between 0 and " +
                                                                   std::to_string(kMaxStockQuantity)));
    }
  }

  return NormalizedProductRecord{.sku = normalized_sku,
                                 .name = trimmed_name,
                                 .price = parsed_price,
                                 .currency = normalized_currency,
                                 .category = std::move(normalized_category),
                                 .description = std::move(normalized_description),
                                 .stock_quantity = parsed_stock};
}

std::string serialize_product_record_as_job_payload(const NormalizedProductRecord& record) {
  char price_buffer[64];
  std::snprintf(price_buffer, sizeof(price_buffer), "%.2f", record.price);

  std::string payload = R"({"sku":")" + infra::json_escape(record.sku) + R"(","name":")" +
                        infra::json_escape(record.name) + R"(","price":")" + price_buffer +
                        R"(","currency":")" + infra::json_escape(record.currency) + R"(")";
  if (record.category) {
    payload += R"(,"category":")" + infra::json_escape(*record.category) + R"(")";
  }
  if (record.description) {
    payload += R"(,"description":")" + infra::json_escape(*record.description) + R"(")";
  }
  payload += R"(,"stock_quantity":")" + std::to_string(record.stock_quantity) + R"(")";
  payload += "}";
  return payload;
}

}  // namespace flowforge::domain
