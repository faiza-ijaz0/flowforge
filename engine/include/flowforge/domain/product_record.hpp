#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "flowforge/result.hpp"

namespace flowforge::domain {

/// A validated, normalized product record ready to become a
/// `product.process` job payload -- the single, shared representation
/// both `handlers::ProductProcessHandler` (validating one job's payload
/// at execution time) and the Processing Center's CSV/image import path
/// (validating one imported record at preview/confirm time, see
/// `services::map_structured_records_to_products`) produce via the same
/// function below, mirroring `domain::NormalizedUserRecord`'s pattern
/// (docs/architecture/user-import.md) exactly -- see
/// docs/architecture/product-processing.md, "Why fields mirror
/// NormalizedUserRecord's pattern".
///
/// Field choice (Phase 3E): `name`/`sku`/`price` are required --
/// meaningless as a product without them. `currency`/`stock_quantity`
/// default (to `"USD"`/`0`) rather than being required: a real import
/// source (a supplier's screenshot, a simple CSV) very often omits either
/// when there's one obvious default, and forcing every row to repeat
/// "USD" or "0" would reject otherwise-good data for no safety benefit.
/// `category`/`description` are optional free text, mirroring
/// `NormalizedUserRecord::phone`. Deliberately excludes fields the brief
/// warned against adding without justification -- no `weight`,
/// `dimensions`, `tax_class`, `vendor`, `barcode`, etc.: none of those are
/// implied by "the minimum professional Product domain model required
/// for bulk processing", and this codebase's own convention (see
/// `NormalizedUserRecord`) is to add a field only once a concrete need
/// exists for it.
struct NormalizedProductRecord {
  std::string sku;
  std::string name;
  double price = 0.0;
  std::string currency;
  std::optional<std::string> category;
  std::optional<std::string> description;
  std::int64_t stock_quantity = 0;
};

/// Trims/uppercases `sku`, trims `name`, parses+bounds-checks `price`,
/// trims/uppercases `currency` (defaulting to `"USD"` if absent/blank),
/// trims optional `category`/`description`, parses+bounds-checks
/// `stock_quantity` (defaulting to `0` if absent/blank). Returns
/// `ErrorCode::Validation` describing the first rule that failed --
/// deterministic, not accumulating every violation, exactly like
/// `validate_and_normalize_user_record`. See .cpp for exact limits and
/// rationale.
[[nodiscard]] Result<NormalizedProductRecord> validate_and_normalize_product_record(
    std::string_view sku, std::string_view name, std::string_view price,
    std::optional<std::string_view> currency, std::optional<std::string_view> category,
    std::optional<std::string_view> description, std::optional<std::string_view> stock_quantity);

/// Serializes `record` as the flat JSON object `product.process` expects
/// as its job payload -- every field as a JSON string (including
/// `price`/`stock_quantity`, formatted canonically), mirroring
/// `serialize_user_record_as_job_payload`'s "no JSON library in engine/"
/// rationale (docs/architecture/user-import.md) exactly: hand-rolled, not
/// `nlohmann::json`.
[[nodiscard]] std::string serialize_product_record_as_job_payload(const NormalizedProductRecord& record);

}  // namespace flowforge::domain
