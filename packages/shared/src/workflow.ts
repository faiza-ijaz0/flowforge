/** Mirrors flowforge::domain::WorkflowStatus (engine/include/flowforge/domain/workflow.hpp). */
export type WorkflowStatus = "pending" | "running" | "succeeded" | "failed" | "cancelled";

export interface WorkflowStep {
  id: string;
  name: string;
  job_id: string;
  depends_on: string[];
}

export interface Workflow {
  id: string;
  name: string;
  status: WorkflowStatus;
  steps: WorkflowStep[];
  created_at: string;
  updated_at: string;
}

export interface ListWorkflowsResponse {
  workflows: Workflow[];
}
