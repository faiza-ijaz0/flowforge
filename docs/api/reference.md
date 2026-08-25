# API reference (v1)

Base path: `/api/v1`. All request/response bodies are JSON. There is no authentication yet (see
`docs/architecture/overview.md` §9) — every endpoint below is open.

This document only lists endpoints that actually exist and are wired to `apps/server`. For the full
list of endpoints planned but not yet implemented, see the README's Roadmap section.

## Health and observability

### `GET /health`
Liveness check. Always returns `200` if the process is running.
```json
{ "status": "ok" }
```

### `GET /ready`
Readiness check. Currently identical to `/health` (no external dependency to check yet).
```json
{ "status": "ok", "environment": "development", "uptime_seconds": 42 }
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
{ "jobs": [ /* Job[] */ ] }
```

### `GET /api/v1/jobs/{id}`
Fetches a single job. `404` (`not_found`) if the id doesn't exist.

### `POST /api/v1/jobs/{id}/cancel`
Transitions a job to `cancelled`. `404` if unknown, `409` (`conflict`) if the job is already in a
terminal state (`succeeded`, `cancelled`, `dead_letter`).

### Job shape
```json
{
  "id": "3f9a7e2a-...-uuid",
  "queue_name": "emails",
  "payload": { "to": "a@example.com" },
  "priority": 0,
  "status": "pending",
  "attempt_count": 0,
  "max_attempts": 3,
  "last_error": null,
  "created_at": "2026-08-25T14:03:21.123Z",
  "updated_at": "2026-08-25T14:03:21.123Z"
}
```
`status` is one of: `pending`, `queued`, `running`, `succeeded`, `failed`, `retrying`, `cancelled`,
`dead_letter`. Note: nothing currently transitions a job past `pending`/`cancelled` — there is no
scheduler yet (see README Roadmap), so `queued`/`running`/`succeeded`/`failed`/`retrying`/
`dead_letter` are reachable in the domain model and API contract but not yet produced by any code
path.

## Workflows

### `GET /api/v1/workflows`
Lists workflows. Currently always returns an empty list — there is no `POST` to create one yet.
```json
{ "workflows": [] }
```

## Workers

### `GET /api/v1/workers`
Lists registered workers. Currently always returns an empty list — no worker process registers
itself yet.
```json
{ "workers": [] }
```

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
