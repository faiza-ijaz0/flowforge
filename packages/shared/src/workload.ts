/**
 * Mirrors flowforge::domain::WorkloadStatus (engine/include/flowforge/domain/workload.hpp).
 * See docs/architecture/workload-model.md for the full lifecycle.
 */
export type WorkloadStatus = "pending" | "queued" | "running" | "succeeded" | "failed";

/**
 * A logical grouping of related jobs submitted as one unit (e.g. one CSV
 * import) -- mirrors flowforge::domain::Workload as serialized by
 * apps/server/src/json/workload_json.cpp. completed_items/failed_items/
 * status are always computed live from the workload's child jobs by the
 * server, never a stale cached snapshot.
 */
export interface Workload {
  id: string;
  type: string;
  status: WorkloadStatus;
  total_items: number;
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
