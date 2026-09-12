import type { Workload, WorkloadItemDispatchOutcome } from "./workload";

/**
 * Mirrors flowforge::domain::InputSourceType
 * (engine/include/flowforge/domain/input_source.hpp). The architecture
 * anticipates all five; only "csv" is actually implemented today -- see
 * docs/architecture/input-processing.md.
 */
export type InputSourceType = "csv" | "image" | "screenshot" | "text" | "url";

/**
 * Mirrors flowforge::domain::ProcessingTarget
 * (engine/include/flowforge/domain/processing_target.hpp). Only "users"
 * has a registered handler ("user.process") today.
 */
export type ProcessingTarget = "users" | "products" | "categories";

/** One record that failed extraction -- mirrors flowforge::domain::RejectedRecord. */
export interface RejectedRecord {
  index: number;
  reason: string;
}

/**
 * Response of POST /api/v1/process -- mirrors flowforge::services::
 * ProcessResult, the source-/target-agnostic counterpart of
 * UserImportResponse (workload.ts). Only the csv+users combination
 * returns this today; every other combination is rejected before a
 * workload is created (see docs/architecture/input-processing.md).
 */
export interface ProcessResponse extends Workload {
  items: WorkloadItemDispatchOutcome[];
  total_records: number;
  valid_records: number;
  invalid_records: number;
  rejected_records: RejectedRecord[];
  rejected_records_truncated: boolean;
}

/**
 * A normalized, ready-to-confirm record -- a flat map of canonical field
 * name to string value, mirroring flowforge::domain::StructuredRecord
 * (Phase 3E: generalized from the Users-only `{name,email,phone}` shape
 * so the same Processing Center UI serves every target -- see
 * docs/architecture/product-processing.md, "Why InputProcessingService is
 * not duplicated for Products"). Which keys are present depends entirely
 * on the request's `target`: `name`/`email`/`phone`? for Users;
 * `sku`/`name`/`price`/`currency`/`category`?/`description`?/
 * `stock_quantity` for Products. Round-tripped, unmodified, from a
 * PreviewResponse's `records` into `apiClient.confirmProcess`'s `records`
 * (Phase 3D-1 -- see docs/architecture/input-processing.md,
 * "Confirmation").
 */
export type StructuredRecord = Record<string, string>;

/** @deprecated Phase 3D-1 name for {@link StructuredRecord}; kept only as an alias for any external caller. */
export type NormalizedUserRecord = StructuredRecord;

/**
 * Response of POST /api/v1/process/preview -- mirrors
 * flowforge::services::PreviewResult (Phase 3D-1). No workload/items
 * fields: nothing is created by a preview call -- see
 * docs/architecture/input-processing.md, "Preview".
 */
export interface PreviewResponse {
  source: InputSourceType;
  target: ProcessingTarget;
  total_records: number;
  valid_records: number;
  invalid_records: number;
  records: StructuredRecord[];
  rejected_records: RejectedRecord[];
  rejected_records_truncated: boolean;
  warnings: string[];
  average_confidence: number | null;
}

/**
 * A persisted product row -- mirrors flowforge::domain::Product
 * (Phase 3E, see docs/architecture/product-processing.md). Written only
 * by handlers::ProductProcessHandler at job-execution time; read via
 * `GET /api/v1/products`.
 */
export interface Product {
  id: string;
  sku: string;
  name: string;
  price: number;
  currency: string;
  category: string | null;
  description: string | null;
  stock_quantity: number;
  job_id: string | null;
  created_at: string;
  updated_at: string;
}

/** Response of GET /api/v1/products -- bounded, offset-paginated. */
export interface ListProductsResponse {
  products: Product[];
  total: number;
  limit: number;
  offset: number;
}
