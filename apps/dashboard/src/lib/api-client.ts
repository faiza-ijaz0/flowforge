import type {
  ApiErrorBody,
  CreateJobInput,
  CreateJobResponse,
  CreateWorkloadInput,
  CreateWorkloadResponse,
  InputSourceType,
  Job,
  ListAttemptsResponse,
  ListJobsResponse,
  ListWorkersResponse,
  ListWorkflowsResponse,
  ListWorkloadItemsResponse,
  ListProductsResponse,
  ListCategoriesResponse,
  ListUsersResponse,
  ListWorkloadsResponse,
  PreviewResponse,
  ProcessingTarget,
  ProcessResponse,
  ReadyResponse,
  StructuredRecord,
  UserImportResponse,
  Workload,
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

async function handleResponse<T>(fetchPromise: Promise<Response>): Promise<T> {
  let response: Response;
  try {
    response = await fetchPromise;
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

export type ReadinessResult =
  | { reachable: true; body: ReadyResponse }
  | { reachable: false; message: string };

/**
 * GET /ready intentionally responds with HTTP 503 (not 200) whenever any
 * readiness check fails (see apps/server/src/http/routes/health_routes.cpp)
 * -- the JSON body's `checks` breakdown is well-formed either way. Routing
 * this through request()/handleResponse() (which throws ApiError on any
 * non-ok status) would discard that breakdown exactly when callers most
 * need to render it, and would also conflate "API unreachable" with "API
 * reachable but unhealthy" -- two states the dashboard home/health pages
 * must distinguish (see docs/architecture/phase-3g-audit.md §2.5).
 */
async function fetchReadiness(): Promise<ReadinessResult> {
  let response: Response;
  try {
    response = await fetch(`${apiBaseUrl}/ready`, { cache: "no-store" });
  } catch {
    return {
      reachable: false,
      message: `Could not reach FlowForge API at ${apiBaseUrl}. Is the server running?`,
    };
  }
  const body = (await response.json().catch(() => null)) as ReadyResponse | null;
  if (!body) {
    return { reachable: false, message: `Received a malformed response from ${apiBaseUrl}/ready.` };
  }
  return { reachable: true, body };
}

function request<T>(path: string, init?: RequestInit): Promise<T> {
  return handleResponse<T>(
    fetch(`${apiBaseUrl}${path}`, {
      ...init,
      headers: { "Content-Type": "application/json", ...init?.headers },
      cache: "no-store",
    }),
  );
}

/**
 * Like request(), but for a `multipart/form-data` body (file upload): the
 * browser must set its own `Content-Type` header (with the multipart
 * boundary) from the `FormData` body -- explicitly setting
 * "application/json" here, as request() does, would send the wrong
 * content type and break server-side multipart parsing.
 */
function requestForm<T>(path: string, formData: FormData): Promise<T> {
  return handleResponse<T>(fetch(`${apiBaseUrl}${path}`, { method: "POST", body: formData, cache: "no-store" }));
}

export const apiClient = {
  health: () => request<{ status: string }>("/health"),
  readiness: fetchReadiness,

  listJobs: (limit = 50, offset = 0) =>
    request<ListJobsResponse>(`/api/v1/jobs?limit=${limit}&offset=${offset}`),
  getJob: (id: string) => request<Job>(`/api/v1/jobs/${encodeURIComponent(id)}`),
  createJob: (input: CreateJobInput) =>
    request<CreateJobResponse>("/api/v1/jobs", { method: "POST", body: JSON.stringify(input) }),
  cancelJob: (id: string) =>
    request<Job>(`/api/v1/jobs/${encodeURIComponent(id)}/cancel`, { method: "POST" }),
  getJobAttempts: (id: string) =>
    request<ListAttemptsResponse>(`/api/v1/jobs/${encodeURIComponent(id)}/attempts`),

  listWorkflows: () => request<ListWorkflowsResponse>("/api/v1/workflows"),
  listWorkers: () => request<ListWorkersResponse>("/api/v1/workers"),

  // Phase 3A: the workload platform foundation (see
  // docs/architecture/workload-model.md).
  createWorkload: (input: CreateWorkloadInput) =>
    request<CreateWorkloadResponse>("/api/v1/workloads", { method: "POST", body: JSON.stringify(input) }),
  getWorkload: (id: string) => request<Workload>(`/api/v1/workloads/${encodeURIComponent(id)}`),
  listWorkloads: (limit = 50, offset = 0) =>
    request<ListWorkloadsResponse>(`/api/v1/workloads?limit=${limit}&offset=${offset}`),

  // Phase 3B: User Import (see docs/architecture/user-import.md).
  createUserImportWorkload: (file: File) => {
    const formData = new FormData();
    formData.append("file", file, file.name);
    return requestForm<UserImportResponse>("/api/v1/workloads/user-imports", formData);
  },
  getWorkloadItems: (id: string, limit = 50, offset = 0) =>
    request<ListWorkloadItemsResponse>(
      `/api/v1/workloads/${encodeURIComponent(id)}/items?limit=${limit}&offset=${offset}`,
    ),

  // Phase 3C: the source-/target-agnostic Processing Center entry point
  // (see docs/architecture/input-processing.md). Today only source="csv"
  // combined with target="users" is feature-complete; every other
  // combination is rejected by the server with a clear "not yet supported"
  // error rather than a fake success.
  process: (source: InputSourceType, target: ProcessingTarget, file: File) => {
    const formData = new FormData();
    formData.append("source", source);
    formData.append("target", target);
    formData.append("file", file, file.name);
    return requestForm<ProcessResponse>("/api/v1/process", formData);
  },

  // Phase 3D-1: image/screenshot extraction preview + confirmation (see
  // docs/architecture/input-processing.md, "Preview" / "Confirmation").
  // previewProcess creates nothing server-side -- no workload, no job;
  // confirmProcess is the only one of the two that does.
  previewProcess: (source: InputSourceType, target: ProcessingTarget, file: File) => {
    const formData = new FormData();
    formData.append("source", source);
    formData.append("target", target);
    formData.append("file", file, file.name);
    return requestForm<PreviewResponse>("/api/v1/process/preview", formData);
  },
  confirmProcess: (target: ProcessingTarget, records: StructuredRecord[]) =>
    request<ProcessResponse>("/api/v1/process/confirm", {
      method: "POST",
      body: JSON.stringify({ target, records }),
    }),

  // Phase 3E: the Product domain's read API (see
  // docs/architecture/product-processing.md). Products are written only
  // by handlers::ProductProcessHandler at job-execution time -- there is
  // no createProduct() here, matching the read-only route this hits.
  listProducts: (limit = 50, offset = 0) =>
    request<ListProductsResponse>(`/api/v1/products?limit=${limit}&offset=${offset}`),

  // Phase 3F: the Category domain's read API (see
  // docs/architecture/category-processing.md). Same read-only shape as
  // listProducts -- categories are written only by
  // handlers::CategoryProcessHandler at job-execution time.
  listCategories: (limit = 50, offset = 0) =>
    request<ListCategoriesResponse>(`/api/v1/categories?limit=${limit}&offset=${offset}`),

  // Phase 3H: the Users domain's read API, closing the persistence gap
  // documented in docs/architecture/phase-3g-audit.md §2.4 -- same
  // read-only shape as listProducts/listCategories. Users are written
  // only by handlers::UserProcessHandler at job-execution time.
  listUsers: (limit = 50, offset = 0) =>
    request<ListUsersResponse>(`/api/v1/users?limit=${limit}&offset=${offset}`),
};
