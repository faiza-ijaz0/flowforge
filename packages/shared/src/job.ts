/**
 * Mirrors flowforge::domain::JobStatus (engine/include/flowforge/domain/job.hpp)
 * as serialized by apps/server/src/json/job_json.cpp.
 */
export type JobStatus =
  | "pending"
  | "queued"
  | "running"
  | "succeeded"
  | "failed"
  | "retrying"
  | "cancelled"
  | "dead_letter";

export interface Job {
  id: string;
  queue_name: string;
  /** "" means not schedulable yet -- see docs/architecture/execution-model.md. */
  job_type: string;
  payload: unknown;
  priority: number;
  status: JobStatus;
  attempt_count: number;
  max_attempts: number;
  last_error: string | null;
  created_at: string;
  updated_at: string;
}

export interface RetryPolicyInput {
  max_attempts?: number;
  initial_backoff_ms?: number;
  max_backoff_ms?: number;
  backoff_multiplier?: number;
}

export interface CreateJobInput {
  queue_name: string;
  payload: unknown;
  /** Optional. Non-empty + registered (e.g. "echo") submits the job to the Scheduler on creation. */
  job_type?: string;
  priority?: number;
  retry_policy?: RetryPolicyInput;
}

/** Additive response field from POST /api/v1/jobs (Phase 2B-2) reporting
 *  whether the Scheduler accepted the job -- see
 *  docs/architecture/execution-model.md, "HTTP integration". */
export interface SchedulingOutcome {
  scheduled: boolean;
  reason?: string;
}

export interface CreateJobResponse extends Job {
  scheduling: SchedulingOutcome;
}

export interface ListJobsResponse {
  jobs: Job[];
}

/**
 * Mirrors flowforge::domain::ExecutionOutcome (engine/include/flowforge/domain/execution.hpp).
 */
export type ExecutionOutcome = "running" | "succeeded" | "failed" | "timed_out" | "cancelled";

/**
 * One real execution attempt (Phase 2B-3) -- mirrors flowforge::domain::Execution,
 * as serialized by apps/server/src/json/job_json.cpp's `to_json(const domain::Execution&)`.
 * Fetched via `GET /api/v1/jobs/{id}/attempts`.
 */
export interface Attempt {
  id: string;
  job_id: string;
  worker_id: string | null;
  attempt_number: number;
  outcome: ExecutionOutcome;
  started_at: string;
  finished_at: string | null;
  error_message: string | null;
}

export interface ListAttemptsResponse {
  attempts: Attempt[];
}
