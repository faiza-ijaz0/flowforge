# API reference (v1)

Base path: `/api/v1`. All request/response bodies are JSON. There is no authentication yet (see
`docs/architecture/overview.md` §9) — every endpoint below is open.

This document only lists endpoints that actually exist and are wired to `apps/server`. For the full
list of endpoints planned but not yet implemented, see the README's Roadmap section. Products,
Categories, Users, and the Processing Center's upload/preview/confirm endpoints are documented in
their own architecture docs rather than duplicated here in full request/response detail — see
`docs/architecture/product-processing.md`, `docs/architecture/category-processing.md`,
`docs/architecture/user-import.md`, and `docs/architecture/input-processing.md`.

## Health and observability

### `GET /health`
Liveness check. Always returns `200` if the process is running.
```json
{ "status": "ok" }
```

### `GET /ready`
Readiness check: real, cheap, non-blocking checks against PostgreSQL, the scheduler, the worker
pool, and the retry dispatcher (see `docs/architecture/execution-model.md` §20). Returns `503`
(never a lying `200`) the moment any one of them is unavailable.
```json
{
  "status": "ok",
  "environment": "development",
  "uptime_seconds": 42,
  "checks": {
    "database": "ok",
    "scheduler": "ok",
    "worker_pool": "ok",
    "retry_dispatcher": "ok"
  }
}
```

### `GET /metrics`
Plain-text rendering of the in-memory metrics registry (not Prometheus exposition format — see
`docs/architecture/overview.md` §8).
```
flowforge_jobs_created_total 3
flowforge_jobs_cancelled_total 1
```

## Jobs

### `POST /api/v1/jobs`
Creates a job. `payload` may be any JSON value or a pre-serialized string.

Request:
```json
{
  "queue_name": "emails",
  "payload": { "to": "a@example.com" },
  "priority": 0,
  "retry_policy": {
    "max_attempts": 3,
    "initial_backoff_ms": 1000,
    "max_backoff_ms": 60000,
    "backoff_multiplier": 2.0
  }
}
```
`priority` and `retry_policy` (and every field within it) are optional. Response: `201` with the
created job (see shape below). Validation failures return `400` with
`{"error":{"code":"validation_error","message":"..."}}`.

### `GET /api/v1/jobs?limit=50&offset=0`
Lists jobs in creation order. `limit` defaults to 50, capped at 500; `offset` defaults to 0.
```json
{ "jobs": [ /* Job[] */ ], "total": 344, "limit": 50, "offset": 0 }
```

### `GET /api/v1/jobs/{id}`
Fetches a single job. `404` (`not_found`) if the id doesn't exist.

### `GET /api/v1/jobs/{id}/attempts`
Real execution attempt history for one job (one row per attempt — worker, outcome, duration,
error), backed by `job_attempts`.
```json
{ "attempts": [ /* Attempt[] */ ] }
```

### `POST /api/v1/jobs/{id}/cancel`
Transitions a job to `cancelled`. `404` if unknown, `409` (`conflict`) if the job is already in a
terminal state (`succeeded`, `cancelled`, `dead_letter`).

### Job shape
```json
{
  "id": "3f9a7e2a-...-uuid",
  "queue_name": "emails",
  "job_type": "user.process",
  "payload": { "to": "a@example.com" },
  "priority": 0,
  "status": "pending",
  "attempt_count": 0,
  "max_attempts": 3,
  "last_error": null,
  "workload_id": "9e2f...-uuid",
  "created_at": "2026-08-25T14:03:21.123Z",
  "updated_at": "2026-08-25T14:03:21.123Z"
}
```
`status` is one of: `pending`, `queued`, `running`, `succeeded`, `failed`, `retrying`, `cancelled`,
`dead_letter`, driven by a real `PriorityScheduler`/`LocalWorkerPool`/`RetryDispatcher` pipeline
(see `docs/architecture/execution-model.md`). `workload_id` is `null` for a job created directly
via `POST /api/v1/jobs` rather than as part of a workload.

## Workloads

A workload is a logical grouping of related jobs submitted as one unit (e.g. one CSV import) — see
`docs/architecture/workload-model.md`. `status` and every item count below are computed live from
the workload's current child jobs on every read, never a persisted/cached counter.

### `POST /api/v1/workloads`
Creates a workload and one job per item (each created-then-scheduled exactly like
`POST /api/v1/jobs`). `items` may be omitted/empty (a valid, immediately-`succeeded` workload).
```json
{ "type": "user.process", "items": [ { "name": "Alice", "email": "alice@example.com" } ] }
```
Response: `201` with the workload plus an `items` array of per-item dispatch outcomes
(`{"job_id": "...", "scheduled": true}`).

### `GET /api/v1/workloads?limit=50&offset=0`
Lists workloads in creation order, each with live-computed progress. `limit` defaults to 50, capped
at 500.
```json
{ "workloads": [ /* Workload[] */ ], "total": 10, "limit": 50, "offset": 0 }
```

### `GET /api/v1/workloads/{id}`
Fetches one workload with live-computed progress. `404` if unknown.

### `GET /api/v1/workloads/{id}/items?limit=50&offset=0`
Bounded, paginated view of a workload's individual child-job outcomes (name/email/status/attempts/
last_error per item — parsed best-effort from each job's payload).
```json
{ "items": [ /* WorkloadJobItem[] */ ], "total": 137, "limit": 50, "offset": 0 }
```

### Workload shape
```json
{
  "id": "9e2f...-uuid",
  "type": "user.process",
  "status": "running",
  "total_items": 100,
  "queued_items": 10,
  "running_items": 2,
  "completed_items": 85,
  "failed_items": 3,
  "retrying_items": 1,
  "dead_letter_items": 1,
  "created_at": "2026-09-02T15:03:13.745Z",
  "updated_at": "2026-09-02T15:03:13.745Z"
}
```
`status` is one of `pending`, `queued`, `running`, `succeeded`, `failed`. `retrying_items` is a
sub-count of `queued_items` (a job that failed once and is backing off before another attempt is
still "queued" for status-derivation purposes, but distinguishably retrying); `dead_letter_items`
is a sub-count of `failed_items` (retries exhausted, vs. an outright non-retryable failure). See
`docs/architecture/phase-3g-audit.md` §3.2 for the full rationale.

## Users

Real, persisted user records (Phase 3H — see `docs/architecture/phase-3h-production-readiness.md`
§2). Written only by `handlers::UserProcessHandler` at job-execution time; there is no
`POST /api/v1/users` — creating one is always "confirm an import"
(`POST /api/v1/process/confirm` or `POST /api/v1/workloads/user-imports`).

### `GET /api/v1/users?limit=50&offset=0`
Lists users in creation order. `limit` defaults to 50, capped at 200.
```json
{ "users": [ /* User[] */ ], "total": 95, "limit": 50, "offset": 0 }
```

### User shape
```json
{
  "id": "9e2f...-uuid",
  "name": "Alice Khan",
  "email": "alice@example.com",
  "phone": "555-1234",
  "job_id": "3f9a7e2a-...-uuid",
  "created_at": "2026-09-24T11:00:00.000Z",
  "updated_at": "2026-09-24T11:00:00.000Z"
}
```
`email` is unique (case-normalized at the application layer); re-submitting the same email updates
the existing row in place rather than creating a duplicate or conflicting. `phone`/`job_id` are
`null` when unset.

## Workflows

### `GET /api/v1/workflows`
Lists workflows. Currently always returns an empty list — there is no `POST` to create one yet.
```json
{ "workflows": [] }
```

## Workers

### `GET /api/v1/workers`
Lists worker records. The server's in-process worker pool registers one record per worker thread
at startup (`FLOWFORGE_WORKER_POOL_SIZE`). Records are never removed, so with PostgreSQL the list
also contains `idle` records from earlier server runs. There is no standalone worker process and no
pagination on this endpoint.
```json
{ "workers": [ { "id": "…", "hostname": "…", "status": "idle",
                 "registered_at": "…", "last_heartbeat": "…" } ] }
```

## Path IDs

Every `{id}` path segment (`/jobs/{id}`, `/jobs/{id}/attempts`, `/jobs/{id}/cancel`,
`/workloads/{id}`, `/workloads/{id}/items`) must be a canonical `8-4-4-4-12` hex UUID. Anything else
is rejected with `400 validation_error` ("invalid job id: expected a UUID") before any database
access; a well-formed UUID that does not exist is `404 not_found`.

## Error shape

Every non-2xx response body:
```json
{ "error": { "code": "not_found", "message": "job with id '...' was not found" } }
```

| `code` | HTTP status |
|---|---|
| `validation_error` | 400 |
| `not_found` | 404 |
| `conflict` | 409 |
| `network_error` | 502 |
| `configuration_error`, `infrastructure_error`, `database_error`, `job_execution_error`, `internal_error` | 500 |
