# Phase 3G Audit — Unified FlowForge Platform

Status: audit only, no code changed. This document is the reference for the rest of Phase 3G
(hardening + polish). It assumes the reader has **not** read the codebase yet, so it cites exact
files/routes throughout. See also the prior-phase docs it builds on: `overview.md`,
`execution-model.md`, `workload-model.md`, `input-processing.md`, `user-import.md`,
`product-processing.md`, `category-processing.md` — those remain accurate for how each subsystem
was designed; this document is the gap analysis across all of them, plus what's needed to make the
platform feel unified end-to-end.

**Headline finding**: this codebase is much further along than "prototype." Backend validation,
pagination caps, CORS, upload-size/magic-byte checks, parameterized SQL, structured error mapping,
and real polling-based progress all already exist and are well-reasoned (see §6, §7). The gaps are
concentrated in **frontend consistency** (three different list-page patterns coexist), **one
missing dashboard route** (`/workloads`), **a few small, additive backend/API fields** (job→workload
linkage, retrying/dead-letter visibility at the workload level, pagination totals on two endpoints),
and **stale copy** (nav footer, page descriptions) that undersells what's already built. Nothing
here requires an architecture change.

---

## 1. apps/dashboard — routes, navigation, patterns

### 1.1 Existing routes

| Route | Exists | Pattern | Notes |
|---|---|---|---|
| `/` | ✅ | RSC | Minimal: API reachability + environment + uptime only. Ignores `/ready`'s `checks` breakdown entirely. Copy says "Job/workflow/worker summary panels arrive once the scheduler (Phase 2) is implemented" — **stale**, the scheduler has existed since Phase 2B-2. |
| `/processing` | ✅ | Client component (`ProcessingUploadPanel`) | Full CSV + image/screenshot flow, drag/drop, client-side pre-validation mirroring server checks, preview→confirm, rejected-row display. One of the most complete pages in the app. Page-level description text says "Today only Users + CSV is implemented" — **stale**, Products/Categories and image/screenshot are all live per the component itself. |
| `/workloads` | ❌ **missing** | — | No list page exists. `apiClient.listWorkloads()` is already implemented and unused. Not in the sidebar. The only way to reach a workload is a direct link from Processing Center's post-confirm result or from `/users`' import result — there is no browse/discover path. |
| `/workloads/[id]` | ✅ | RSC + 2 client islands | Solid: real progress via polling (`WorkloadProgressPanel`), paginated items table (`WorkloadItemsTable`). Bug: back-link is hardcoded `"Back to users"` regardless of workload type (line 25) — wrong for product/category workloads. No breadcrumb to a workload list (because none exists). No link from here to constituent jobs' detail pages (item rows likely show job_id but should deep-link — see `workload-items-table.tsx`, not yet read in this pass — verify during implementation). |
| `/jobs` | ✅ | RSC, **no pagination controls** | Calls `listJobs(100, 0)` hardcoded — ignores the API's `limit`/`offset` support. No loading/error separation (single inline try/catch). No `workload_id` column/link (the API doesn't expose it — see §3.1). |
| `/jobs/[id]` | ✅ | RSC | Strong: status, attempts table (real execution history), last error, payload. Missing: no link back to parent workload (again, API doesn't expose `workload_id` on `Job`). |
| `/users` | ✅ | Client (`UserImportWizard`) | **Not a list page** — it's an import wizard only. There is no way to browse actually-persisted user records. No `GET /api/v1/users` exists at all (see §2.4). This is the one domain (of Users/Products/Categories) without a read API or list UI, despite being the oldest (Phase 3B). |
| `/products` | ✅ | Client, real pagination | **Reference-quality implementation**: loading/empty/error states, `total`-driven Previous/Next, truncated job_id (linkable but not yet linked to `/jobs/[id]`). Use this as the template for `/workloads` and for hardening `/jobs`. |
| `/categories` | ✅ | Client, same pattern as `/products` | Same quality, same job_id-not-linked gap. |
| `/workflows` | ✅ | Real list | Read-only per `overview.md` (§5) — no workflow creation exists yet; out of Phase 3G scope. |
| `/workers` | ✅ | Real list | Out of Phase 3G scope beyond nav consistency. |
| `/queues` | ✅ | `NotYetImplemented` placeholder | Honest placeholder, not a fake empty state — good pattern, keep. |
| `/logs` | ✅ | `NotYetImplemented` placeholder | Same. |
| `/settings` | ✅ | `NotYetImplemented` placeholder | Same. |
| `/metrics` | ✅ | Real, renders `/metrics` text | Raw plain-text metrics feed. Could be paired with a `/health`-style structured page (see §1.3) but current form is honest and functional. |
| `/health` (dashboard route) | ❌ **missing** | — | No dedicated health/observability dashboard page. Backend `/ready` already returns a structured `checks: {database, scheduler, worker_pool, retry_dispatcher}` breakdown (see §2.5) that nothing in the UI surfaces beyond the homepage's coarse reachable/unreachable dot. |

### 1.2 Navigation (`components/layout/sidebar.tsx`)

Twelve items, all pointing to real pages except the missing `/workloads` (not linked at all — dead
functionality, not a dead link) and the missing `/health` page. Footer text hardcodes **"v0.1.0 ·
Phase 1 foundation"** — badly stale (the project is deep into Phase 3). No dead links exist today;
the risk is entirely sins of omission.

No breadcrumbs anywhere; `/workloads/[id]` and `/jobs/[id]` each have a single hand-rolled "back to
X" link (and one of those, per §1.1, is wrong for non-user workloads).

### 1.3 Cross-cutting frontend patterns

- **Three different list-page architectures coexist**: `/jobs` (RSC, no pagination UI, single
  inline error path), `/products`/`/categories` (client component, real `total`-driven pagination,
  separated loading/empty/error branches), `/workloads/[id]`'s items table (a third, not yet read in
  depth). Phase 3G should converge on one pattern — the Products page is the strongest candidate.
- **No `loading.tsx` or `error.tsx` files anywhere** in `apps/dashboard/src/app` (confirmed via
  directory search) — Next.js route-level loading/error boundaries are unused; every page
  hand-rolls its own (inconsistently). A slow API response on an RSC page renders nothing until data
  resolves; an uncaught throw hits Next's default (unstyled) error page.
- **`api-client.ts`** is a single, well-factored module — every page goes through it, errors are
  typed (`ApiError` with `status`/`code`), and there's a dedicated `requestForm` path for
  multipart uploads. This is a good foundation to build consistent error-state UI on top of; no
  need to touch this file's shape, only to use it more uniformly.
- **Polling** (`WorkloadProgressPanel`) is already implemented correctly: starts on mount even with
  initial data, stops at terminal state (`succeeded`/`failed` only — see §3.2 for why this
  under-covers `retrying`/`dead_letter`), cleans up its timer on unmount, and surfaces (not hides)
  transient poll errors while continuing to retry. This is the reference pattern for any other
  live-updating view Phase 3G adds (e.g. a live jobs list, if one becomes necessary).

---

## 2. apps/server — API surface

### 2.1 Routes inventory

`workload_routes.cpp`: `POST /api/v1/workloads`, `POST /api/v1/workloads/user-imports`,
`GET /api/v1/workloads` (list), `GET /api/v1/workloads/:id`, `GET /api/v1/workloads/:id/items`.

`job_routes.cpp`: `POST /api/v1/jobs`, `GET /api/v1/jobs` (list), `GET /api/v1/jobs/:id`,
`GET /api/v1/jobs/:id/attempts`, `POST /api/v1/jobs/:id/cancel`.

`process_routes.cpp`: `POST /api/v1/process`, `POST /api/v1/process/preview`,
`POST /api/v1/process/confirm` (not read in full this pass — behavior confirmed via the dashboard
component and `api-client.ts` call sites).

`product_routes.cpp` / `category_routes.cpp`: `GET /api/v1/products` / `GET /api/v1/categories`,
each with `limit`/`offset` **and `total`** (via a `->count()` repository call) — the most complete
pagination contract in the API.

`health_routes.cpp`: `GET /health`, `GET /ready`, `GET /metrics`.
`worker_routes.cpp`: `GET /api/v1/workers`. `workflow_routes.cpp`: `GET /api/v1/workflows`.

No `GET /api/v1/users` exists anywhere.

### 2.2 Validation & error handling

Consistent and good: every route parses JSON defensively (catches `parse_error` before touching
business logic), delegates shape validation to a per-domain `parse_*_request` function, and business
validation to the relevant `*Service`. `write_error` uniformly maps `Error` → HTTP status + JSON
body via `http_status_for`/`to_error_body` (`http/error_response.cpp`, not re-read this pass, but
referenced consistently from every route file touched). 5xx error bodies are scrubbed to a generic
message before leaving the process (per `overview.md` §7.6) — raw exception text never reaches a
client, only the server log.

### 2.3 Pagination inconsistency (concrete)

| Endpoint | `limit`/`offset` accepted | `total` in response | Cap |
|---|---|---|---|
| `GET /api/v1/products` | ✅ | ✅ | 200 |
| `GET /api/v1/categories` | ✅ (same pattern) | ✅ | 200 |
| `GET /api/v1/workloads/:id/items` | ✅ | ✅ | 200 |
| `GET /api/v1/workloads` | ✅ | ❌ **missing** | none visible in route (service-level cap not confirmed this pass — verify `WorkloadService::list_workloads`) |
| `GET /api/v1/jobs` | ✅ | ❌ **missing** | 500 (`JobService::list_jobs`, `kMaxLimit`) |

Impact: a `/workloads` list page or a paginated `/jobs` page cannot render a correct "Showing X–Y of
N" / disable-Next-at-end control (the Products page's pattern) without this. Small, additive,
backwards-compatible fix: add `"total"` to both response bodies the same way `product_routes.cpp`
does (a `->count()`-equivalent on `IJobRepository`/`IWorkloadRepository`, or reuse
`list_workloads`'s/`list_jobs`'s already-fetched page size if a full count is expensive — verify
repository capability before choosing).

### 2.4 Users domain asymmetry

Users (Phase 3B) predates Products (3E) and Categories (3F) but has no read API and no persisted-user
listing anywhere in the dashboard — only the import wizard. Verify during implementation whether a
`users` table / `UserRepository` already exists server-side (per `user-import.md`, users are written
by a handler at job-execution time, mirroring `handlers::ProductProcessHandler`); if so, adding
`GET /api/v1/users` + a `/users` list view (reusing the Products page pattern) is a small,
consistent, in-scope addition. If no `UserRepository` exists at all, this is a larger addition and
should be scoped down for Phase 3G (flag rather than silently build).

### 2.5 Health/readiness — already rich, underused

`GET /ready` (§`health_routes.cpp`) returns a real per-component breakdown:
`checks: {database, scheduler, worker_pool, retry_dispatcher}`, each independently `"ok"`/
`"unavailable"`, and a top-level `503` the moment any one fails — no live DB query per check (cheap,
non-blocking, safe to poll frequently). **Nothing in the dashboard consumes `checks`** — the
homepage only reads `status`/`environment`/`uptime_seconds`. This is a pure frontend gap; no backend
change needed for a `/health` dashboard page (Phase 3G §9).

### 2.6 Security posture (already hardened)

- CORS: origin-echoed (never wildcard `*`), centralized in `cors.cpp`, single place headers are set
  (`app.cpp:188`).
- Upload limits, enforced server-side (not just client hints): CSV ≤ 2 MB
  (`csv_extractor.cpp`/`user_import_parser.cpp`, two independent call sites both capped),
  image ≤ 6 MB (`infra/image_format.hpp::kMaxImageBytes`), server-wide payload cap 8 MB
  (`app.cpp:198`, `set_payload_max_length`), workload item count ≤ 1000
  (`workload_service.cpp::kMaxWorkloadItems`).
- Image validation is magic-byte-based, not trust-the-`Content-Type`/extension
  (`infra/image_format.cpp`) — the dashboard's client-side check explicitly documents that it's a
  UX hint only, real validation happens server-side (`processing-upload-panel.tsx` comment, lines
  60–65).
- SQL: parameterized throughout (`overview.md` §9, spot-confirmed via `postgres_*_repository.cpp`
  file names/comments, not re-audited line-by-line this pass — re-verify in §16 hardening pass if
  time allows, but no contrary evidence found).
- No secrets in the client bundle: `apiBaseUrl` is the only config the dashboard reads
  (`lib/config.ts`, not read this pass but referenced consistently).

This means Phase 3G's "security hardening" step is much more audit/confirm than build — see §7.

---

## 3. engine — domain model, status, retry/dead-letter

### 3.1 `Job` has `workload_id` in the domain model, not in the wire format

`domain::Job::workload_id()` exists (`engine/include/flowforge/domain/job.hpp:82`, an
`optional<infra::WorkloadId>`) and is populated when a job is created as part of a workload. But
`apps/server/src/json/job_json.cpp::to_json(const domain::Job&)` never serializes it — confirmed by
reading the full function (lines 7–30). This is *the* concrete, additive, architecture-consistent
backend change Phase 3G should make: add `"workload_id"` (nullable) to the job JSON. It unblocks:
- `/jobs/[id]` linking back to its parent workload.
- `/jobs` optionally filtering/showing which workload a job belongs to.
- The "Workload → Jobs → Execution Attempts" relationship the phase brief asks for being visible
  from either direction, not just workload→items (which already works via
  `list_by_workload_id`/`/workloads/:id/items`).

A `?workload_id=` filter on `GET /api/v1/jobs` would be a nice-to-have but is not required — the
existing `/workloads/:id/items` endpoint already serves the "jobs for this workload" need; the gap
is purely the reverse direction (a job page knowing its own workload).

### 3.2 Workload-level status collapses retrying/dead-letter into queued/failed

`domain::classify_job_status_for_workload` (`engine/src/domain/workload.cpp:58-74`):

```
Succeeded            -> Succeeded
Cancelled/DeadLetter/Failed -> Failed
Running               -> Running
Pending/Queued/Retrying -> Queued
```

So a job that has failed once and is backing off before a retry attempt is indistinguishable, at
the workload level, from a job that has never run yet — both count as "queued." And a job that has
exhausted all retries and landed in `DeadLetter` is indistinguishable from one that failed outright
with no retries configured — both count as "failed." Job-level detail (`/jobs/[id]`,
`/jobs/[id]`'s `StatusBadge`, which already has correct `retrying`/`dead_letter` styling — see
`status-badge.tsx`) is not affected; only the **workload-level aggregate** loses this distinction.

This is exactly Phase 3G Step 6's target. The fix is additive and backwards-compatible:
1. Add `retrying_items`/`dead_letter_items` (or fold `dead_letter` into a distinct bucket from
   `cancelled`+`failed`) to `domain::Workload::apply_progress` and the JSON it serializes.
2. Update `WorkloadService::with_progress`'s classification switch to produce the new buckets
   without changing `WorkloadItemOutcome`'s existing four values if avoidable (check whether adding
   two new enumerators or computing the split inline in `with_progress` is less invasive — the
   count loop already switches per-job, so adding two more `case`-branches and two more counters is
   a small diff).
3. Update `packages/shared/src/workload.ts`'s `Workload` interface (additive fields — existing
   consumers reading only `queued_items` etc. are unaffected) and `WorkloadProgressPanel`'s
   `StatTile` grid to show Retrying/Dead-letter tiles when non-zero (avoid cluttering the UI with
   permanent zero-tiles for workloads with no retries).
4. `WorkloadProgressPanel`'s `TERMINAL_STATUSES` (`"succeeded" | "failed"`) — verify whether a
   workload-level `WorkloadStatus` needs a new terminal value, or whether "failed" already correctly
   subsumes "some items dead-lettered, no more progress possible." Likely no new `WorkloadStatus`
   value is needed (dead-letter is still a form of "this item failed permanently"), only the
   *count breakdown* needs to distinguish it — keep `derive_workload_status`'s existing
   pending/queued/running/succeeded/failed model unless a concrete reason to add a sixth status
   surfaces during implementation.

### 3.3 Job status enum (reference, no gap)

`JobStatus`: `pending | queued | running | succeeded | failed | retrying | cancelled | dead_letter`
(`packages/shared/src/job.ts`, mirrors `domain::JobStatus`). Already fully modeled and already
styled correctly in `StatusBadge`. Phase 3G must not invent new status values (per the phase brief)
— none of the above changes require that; everything needed already exists at the `Job` level, the
gap is purely in how `Workload` aggregates it.

### 3.4 Retry dispatcher / execution model (reference, no gap found this pass)

Per `overview.md` §6 and `execution-model.md` (not re-read line-by-line this pass — large existing
doc, high confidence based on the job-status enum and `/ready`'s `retry_dispatcher` check both being
real): `engine::RetryDispatcher` re-submits retryable failures via `RetryPolicy::compute_backoff()`;
exhausted retries land on `DeadLetter`. This is real, tested (per `overview.md`), and already
observable at the job level. No engine change needed here — only the workload-aggregation and
JSON-exposure gaps in §3.1–3.2.

---

## 4. database/migrations

Not re-read line-by-line this pass (large prior audit exists in `overview.md` §7.1, high
confidence). Key facts relevant to Phase 3G, confirmed via `overview.md` and cross-checked against
live code behavior above:
- `jobs.workload_id` (migration 0013) — nullable FK, `ON DELETE SET NULL`. Already exists; §3.1's
  fix is JSON-exposure only, no migration needed.
- `job_attempts` — one row per attempt, already fully wired end-to-end (confirmed via
  `/jobs/[id]/attempts` UI + `GET /api/v1/jobs/:id/attempts` route both being real).
- No `status`/`completed_items`/`failed_items` columns on `workloads` (deliberate — always computed
  live). §3.2's fix stays entirely in the computation layer (`WorkloadService::with_progress`), not
  the schema.

No migration is required for any Phase 3G Step 2–18 item identified in this audit.

---

## 5. Docker / CI / production config

`docker-compose.yml`: `postgres` (with a real `pg_isready` healthcheck, 5s interval/timeout, 10
retries), `server`, `migrate`, `dashboard`. Healthcheck present for Postgres; not yet confirmed
whether `server`/`dashboard` services have their own healthchecks wired to `/health`/`/ready` — spot
this during §17 (verify `docker-compose.yml` fully before editing, this pass only grepped service
names).

CI (`.github/workflows/ci.yml`) has six jobs: `cpp-build-test`, `cpp-sanitizers`,
`postgres-integration` (runs real migrations + PostgreSQL-backed tests — good, this is the E2E-ish
coverage that exists today), `cpp-format`, `frontend` (lint/typecheck/build — no dashboard test
runner exists, consistent with no `*.test.ts(x)` files found in `apps/dashboard`), `docker-validate`
(`docker compose config --quiet`). This is a solid CI baseline; Phase 3G's testing step should run
these as-is rather than adding a new framework, and add regression tests only for what Phase 3G
actually changes (per the phase brief's "don't introduce a huge new testing framework").

`tests/e2e/` and `tests/integration/` **contain only `README.md` files — no actual test code**. The
real integration coverage lives elsewhere: `apps/server/tests/*.cpp` (six files, including
`workload_routes_test.cpp`, `process_routes_test.cpp`, `user_import_routes_test.cpp`) and
`engine/tests/` (57 `.cpp` files). Whatever `tests/e2e`/`tests/integration` were meant to hold never
materialized — worth a one-line note in the audit rather than an attempt to fill them out wholesale
in this phase (out of scope unless the user asks for dedicated E2E scripts).

---

## 6. What already works (do not rebuild)

- Real polling with correct cleanup (`WorkloadProgressPanel`).
- Real preview→confirm flow for image/screenshot (`ProcessingUploadPanel`), including OCR
  confidence display, rejected-row detail, drag/drop, and server-mirroring client-side validation.
- Real, honest placeholders (`NotYetImplemented`) for unbuilt features — never a fake empty state.
- Real pagination with correct empty/loading/error separation on `/products` and `/categories` —
  use as the template.
- Real, structured `/ready` health breakdown server-side.
- Real security baseline: CORS, upload caps, magic-byte image validation, parameterized SQL,
  scrubbed 5xx bodies.
- Real job execution attempt history end-to-end (`/jobs/[id]`).
- Real workload progress computed live from child jobs, never a stale counter.

## 7. What's incomplete / inconsistent (punch-list, mapped to Phase 3G steps 2–18)

| # | Item | Status | Files |
|---|---|---|---|
| Step 2 (Nav) | Add `/workloads` link; add `/health` link (if built); fix stale footer version/phase text | needs small fix | `sidebar.tsx` |
| Step 3 (Workloads list) | Build `/workloads` page reusing the Products pagination pattern | needs new implementation | new `apps/dashboard/src/app/workloads/page.tsx`; needs `total` added to `GET /api/v1/workloads` first (§2.3) |
| Step 4 (Workload detail) | Fix hardcoded "Back to users" link; add retrying/dead-letter tiles once §3.2 lands; link items to `/jobs/[id]` if not already (verify `workload-items-table.tsx`) | needs small fix | `app/workloads/[id]/page.tsx:25`, `components/workloads/workload-progress-panel.tsx`, `components/workloads/workload-items-table.tsx` |
| Step 5 (Jobs list/detail) | Add pagination UI to `/jobs` (needs `total` in API first, §2.3); add workload link to `/jobs/[id]` (needs `workload_id` in JSON first, §3.1); align `/jobs` to the Products loading/error pattern | needs new implementation (API) + small fix (UI) | `job_routes.cpp`, `job_json.cpp`, `packages/shared/src/job.ts`, `app/jobs/page.tsx`, `app/jobs/[id]/page.tsx` |
| Step 6 (Retry/dead-letter visibility) | Workload-level aggregate collapses retrying→queued, dead_letter→failed | needs new implementation (small, additive) | `engine/src/domain/workload.cpp` (`classify_job_status_for_workload`), `engine/src/services/workload_service.cpp` (`with_progress`), `apps/server/src/json/workload_json.cpp`, `packages/shared/src/workload.ts`, `workload-progress-panel.tsx` |
| Step 7 (Processing Center polish) | Stale "Users + CSV only" description text; no explicit "View Workload" link after confirmation (currently shows an inline progress panel only, no route out) | needs small fix | `app/processing/page.tsx` (description copy), `processing-upload-panel.tsx` (add link using existing `result.id` → `/workloads/[id]`, once that route exists) |
| Step 8 (Dashboard home) | No workload/job/retry counts shown; ignores `/ready`'s `checks` object; stale "Phase 2" copy | needs new implementation | `app/page.tsx` |
| Step 9 (Health/observability UI) | No `/health` dashboard page; backend data already exists and needs no change | needs new implementation (frontend only) | new `apps/dashboard/src/app/health/page.tsx`, consumes existing `GET /ready` |
| Step 10 (Frontend error handling) | `ApiError` typed and centralized already; inconsistent presentation across pages (RSC inline vs. client-component branches) | needs small fix (consistency, not new mechanism) | all `app/**/page.tsx` |
| Step 11 (Loading/empty/error states) | No `loading.tsx`/`error.tsx` anywhere; `/jobs`, `/workloads/[id]`, `/jobs/[id]` lack the loading separation `/products` has | needs new implementation (route-level boundary files) + small fixes | `apps/dashboard/src/app/**` |
| Step 12 (Pagination) | See Step 3/5 — root cause is §2.3's missing `total` on two endpoints | needs new implementation (API) | `job_routes.cpp`, `workload_routes.cpp` |
| Step 13 (Real-time/progress) | Pattern already correct (`WorkloadProgressPanel`); extend the same pattern to a live jobs view only if a concrete need surfaces — do not add WebSockets | already satisfied (pattern); extend, don't rearchitect | — |
| Step 14 (Accessibility/responsive) | `WorkloadProgressPanel`'s progress bar already has `role="progressbar"` + `aria-value*` — good precedent. Not yet audited: table keyboard nav, focus states on `SelectorButton`/file inputs, color contrast of status badges. Flag for a dedicated pass. | needs audit | across `components/` |
| Step 15 (API contract consistency) | `workload_id` missing from job JSON (§3.1); `total` missing from jobs/workloads list responses (§2.3); Users domain has no read API at all (§2.4) | needs new implementation (small, additive) | see §2.3, §2.4, §3.1 |
| Step 16 (Security hardening) | Baseline already strong (§2.6); no weaknesses found this pass; re-verify SQL parameterization and Docker non-root user claims by direct inspection before signing off (not yet re-read this pass, only cross-referenced against `overview.md`) | needs verification, not new work | `infra/docker/Dockerfile.*`, `persistence/postgres/*.cpp` |
| Step 17 (Production config) | `docker-compose.yml` service healthchecks beyond Postgres not yet confirmed | needs verification | `docker-compose.yml` |
| Step 18 (Performance) | Nothing measured yet in this pass; `list_jobs`/`list_workloads` caps exist (500/unconfirmed) — confirm `WorkloadService`'s list cap matches the `product`/`category` 200 convention or document why it differs | needs verification | `workload_service.cpp` |

## 8. Explicitly out of scope for Phase 3G (per the phase brief, confirmed still true)

- Workflow DAG execution, distributed worker coordination, Prometheus-format `/metrics`, auth/authz
  — all still deferred per `overview.md` §6, nothing found this pass suggests otherwise.
- Any new processing target/source beyond Users/Products/Categories × CSV/Image/Screenshot.
- Introducing WebSockets/SSE — polling is already the established, correct pattern (§1.3).
- Filling out `tests/e2e`/`tests/integration` wholesale — flagged (§5) but not a Phase 3G punch-list
  item unless real regressions are found that need a home there.

## 9. Recommended sequencing for the rest of Phase 3G

1. Small additive backend/API changes first (§3.1 `workload_id`, §2.3 `total` fields, §3.2
   retrying/dead-letter counts) — everything else in the frontend punch-list depends on these.
2. Build `/workloads` (Step 3) using the Products page as a template — this is the biggest single
   missing piece and unblocks Step 2's nav completeness.
3. Harden `/jobs` and `/workloads/[id]` to match (Steps 4–5, 11).
4. Dashboard home + `/health` page (Steps 8–9) — pure frontend, backend already supports both.
5. Processing Center copy fixes + post-confirm workload link (Step 7) — trivial once `/workloads`
   exists.
6. Loading/error boundary files + accessibility pass (Steps 10–11, 14) — cross-cutting, do last so
   the page set is stable.
7. Verification passes (Steps 16–18) — re-confirm security/Docker/perf claims by direct inspection,
   document findings, fix only what's actually broken.
