/**
 * Mirrors flowforge::domain::WorkloadStatus (engine/include/flowforge/domain/workload.hpp).
 * See docs/architecture/workload-model.md for the full lifecycle.
 */
export type WorkloadStatus = "pending" | "queued" | "running" | "succeeded" | "failed";

/**
 * A logical grouping of related jobs submitted as one unit (e.g. one CSV
 * import) -- mirrors flowforge::domain::Workload as serialized by
 * apps/server/src/json/workload_json.cpp. status and all four item counts
 * are always computed live from the workload's child jobs by the server,
 * never a stale cached snapshot. queued_items/running_items +
 * completed_items/failed_items always sum to total_items.
 */
export interface Workload {
  id: string;
  type: string;
  status: WorkloadStatus;
  total_items: number;
  queued_items: number;
  running_items: number;
  completed_items: number;
  failed_items: number;
  created_at: string;
  updated_at: string;
}

/**
 * One item submitted as part of a workload's creation request. A JSON
 * object (e.g. `{ name, email }` for a "user.process" workload) or a raw
 * string -- see parse_create_workload_request (workload_json.cpp).
 */
export type WorkloadItemInput = Record<string, unknown> | string;

export interface CreateWorkloadInput {
  /** Doubles as the job_type every item's Job is created with. */
  type: string;
  items?: WorkloadItemInput[];
}

/** Per-item dispatch outcome, additive on POST /api/v1/workloads's response -- mirrors
 *  CreateJobResponse's `scheduling` field (job.ts). */
export interface WorkloadItemDispatchOutcome {
  job_id: string;
  scheduled: boolean;
  reason?: string;
}

export interface CreateWorkloadResponse extends Workload {
  items: WorkloadItemDispatchOutcome[];
}

export interface ListWorkloadsResponse {
  workloads: Workload[];
}

/**
 * One CSV data row that failed validation -- mirrors
 * flowforge::services::RejectedImportRow. row_number is 1-indexed over
 * data rows only (the header row is never counted).
 */
export interface RejectedImportRow {
  row_number: number;
  reason: string;
}

/**
 * Response of POST /api/v1/workloads/user-imports -- mirrors
 * flowforge::services::UserImportResult. Distinguishes "how many rows
 * were in the CSV" from "how many became jobs" -- see
 * docs/architecture/user-import.md, "Bulk submission semantics".
 * rejected_rows is bounded even when invalid_rows is larger (see
 * rejected_rows_truncated).
 */
export interface UserImportResponse extends Workload {
  items: WorkloadItemDispatchOutcome[];
  total_rows: number;
  valid_rows: number;
  invalid_rows: number;
  rejected_rows: RejectedImportRow[];
  rejected_rows_truncated: boolean;
}

/**
 * One row of a workload's paginated item list -- mirrors
 * apps/server/src/json/workload_json.cpp's `to_json_workload_item`.
 * name/email are parsed server-side from the job's own payload and come
 * back `null` for a job whose payload isn't the expected shape.
 */
export interface WorkloadJobItem {
  job_id: string;
  name: string | null;
  email: string | null;
  status: string;
  attempt_count: number;
  last_error: string | null;
  updated_at: string;
}

export interface ListWorkloadItemsResponse {
  items: WorkloadJobItem[];
  total: number;
  limit: number;
  offset: number;
}
