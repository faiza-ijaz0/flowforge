#include "flowforge/services/product_mapping.hpp"

namespace flowforge::services {

namespace {

/// Bounded the same way `domain::ExtractionResult::rejected_records` is --
/// see its class comment.
constexpr std::size_t kMaxReportedRejectedRecords = 200;

}  // namespace

MappedProductRecords map_structured_records_to_products(
    const std::vector<domain::StructuredRecord>& records) {
  MappedProductRecords result;
  result.total_records = records.size();

  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    const auto sku = record.field_by_aliases({"sku", "product sku", "product_sku", "code", "product code"});
    const auto name = record.field_by_aliases({"name", "product name", "product_name", "title"});
    const auto price = record.field_by_aliases({"price", "unit price", "unit_price", "cost"});
    const auto currency = record.field_by_aliases({"currency", "currency code", "currency_code"});
    const auto category = record.field_by_aliases({"category", "product category", "product_category"});
    const auto description = record.field_by_aliases({"description", "details", "product description"});
    const auto stock_quantity = record.field_by_aliases(
        {"stock_quantity", "stock quantity", "stock", "quantity", "qty", "inventory"});

    if (!sku || !name || !price) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        const char* missing = !sku ? "sku" : (!name ? "name" : "price");
        result.rejected_records.push_back(
            {.index = i + 1,
             .reason = "no '" + std::string(missing) + "' column could be found for this record"});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    auto normalized = domain::validate_and_normalize_product_record(*sku, *name, *price, currency, category,
                                                                    description, stock_quantity);
    if (!normalized) {
      ++result.rejected_record_count;
      if (result.rejected_records.size() < kMaxReportedRejectedRecords) {
        result.rejected_records.push_back({.index = i + 1, .reason = normalized.error().message()});
      } else {
        result.rejected_records_truncated = true;
      }
      continue;
    }

    result.valid_records.push_back(std::move(*normalized));
  }

  return result;
}

}  // namespace flowforge::services
