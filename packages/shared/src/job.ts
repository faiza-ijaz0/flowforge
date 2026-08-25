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
  priority?: number;
  retry_policy?: RetryPolicyInput;
}

export interface ListJobsResponse {
  jobs: Job[];
}
