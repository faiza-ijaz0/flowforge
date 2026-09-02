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
