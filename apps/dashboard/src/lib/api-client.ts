import type {
  ApiErrorBody,
  CreateJobInput,
  Job,
  ListJobsResponse,
  ListWorkersResponse,
  ListWorkflowsResponse,
} from "@flowforge/shared";

import { apiBaseUrl } from "./config";

/**
 * Thin typed wrapper over fetch() for the FlowForge REST API. Every
 * dashboard page goes through this module rather than calling fetch()
 * directly, so the base URL, error shape, and JSON parsing are handled in
 * exactly one place (see docs/architecture/overview.md, "API client
 * abstraction").
 */
export class ApiError extends Error {
  constructor(
    message: string,
    public readonly status: number,
    public readonly code: string,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let response: Response;
  try {
    response = await fetch(`${apiBaseUrl}${path}`, {
      ...init,
      headers: { "Content-Type": "application/json", ...init?.headers },
      cache: "no-store",
    });
  } catch {
    throw new ApiError(
      `Could not reach FlowForge API at ${apiBaseUrl}. Is the server running?`,
      0,
      "network_error",
    );
  }

  if (!response.ok) {
    const body = (await response.json().catch(() => null)) as ApiErrorBody | null;
    throw new ApiError(
      body?.error.message ?? `Request failed with status ${response.status}`,
      response.status,
      body?.error.code ?? "unknown_error",
    );
  }

  return (await response.json()) as T;
}

export const apiClient = {
  health: () => request<{ status: string }>("/health"),
  ready: () => request<{ status: string; environment: string; uptime_seconds: number }>("/ready"),

  listJobs: (limit = 50, offset = 0) =>
    request<ListJobsResponse>(`/api/v1/jobs?limit=${limit}&offset=${offset}`),
  getJob: (id: string) => request<Job>(`/api/v1/jobs/${encodeURIComponent(id)}`),
  createJob: (input: CreateJobInput) =>
    request<Job>("/api/v1/jobs", { method: "POST", body: JSON.stringify(input) }),
  cancelJob: (id: string) =>
    request<Job>(`/api/v1/jobs/${encodeURIComponent(id)}/cancel`, { method: "POST" }),

  listWorkflows: () => request<ListWorkflowsResponse>("/api/v1/workflows"),
  listWorkers: () => request<ListWorkersResponse>("/api/v1/workers"),
};
