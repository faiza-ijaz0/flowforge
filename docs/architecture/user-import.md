# FlowForge User Import (Phase 3B)

This document describes User Import: the first real product workload built on top of Phase 3A's
Workload/Batch foundation ([`workload-model.md`](workload-model.md)). It covers the CSV contract,
the API, bulk-submission semantics, progress calculation, and what's deliberately deferred. It
complements [`workload-model.md`](workload-model.md) (the generic Workload abstraction) and
[`execution-model.md`](execution-model.md) (the job execution pipeline every imported row's job
runs through, completely unchanged) rather than replacing either.

## 1. What changed, and what didn't

The product goal: a user opens `/users` in the dashboard, uploads a CSV, sees a validation preview,
starts the import, and watches real progress as FlowForge's existing engine processes every row.

```
CSV upload
    |
Workload created  (services::WorkloadService::create_user_import_workload)
    |
One Job per valid row  (services::JobService -- unchanged)
    |
PriorityScheduler  (engine::PriorityScheduler -- unchanged)
    |
LocalWorkerPool  (engine::LocalWorkerPool -- unchanged)
    |
JobExecutor  (engine::JobExecutor -- unchanged)
    |
HandlerRegistry -> UserProcessHandler  ("user.process" -- unchanged)
    |
PostgreSQL persistence  (unchanged)
    |
Dashboard progress (polling GET /api/v1/workloads/{id})
```

Nothing below "Workload created" is new. This phase adds exactly two things to the engine: a CSV
parser (`services::parse_user_import_csv`) and one new `WorkloadService` method
(`create_user_import_workload`) that parses a CSV and then calls the *existing*
`WorkloadService::create_workload` (Phase 3A) -- the same create-then-schedule loop that already
creates and dispatches one `Job` per item through `JobService`/`PriorityScheduler`. There is no
second scheduler, no second executor, and no second handler: `UserProcessHandler` is reused exactly
as it was in Phase 3A, unmodified except that its validation/normalization logic was extracted into
a function shared with the CSV parser (see §4).

## 2. CSV contract

**Encoding**: UTF-8. A leading UTF-8 BOM (`EF BB BF`, common from spreadsheet exports) is stripped
before parsing. Any other byte sequence that isn't well-formed UTF-8 rejects the whole file --
`nlohmann::json` (which serializes every job payload) requires valid UTF-8, so this is enforced
before any row is processed, not discovered later as a 500.

**Header row**: required. Columns `name` and `email` are required (case-sensitive, exact match);
`phone` is optional. Column order does not matter. Any other column name in the header --
misspelled, extra, or unexpected -- rejects the whole file with an error naming the offending
column, rather than silently ignoring it. A duplicate column name (e.g. two `name` columns) is
also rejected.

**Quoting**: RFC 4180-style. A field may be wrapped in `"..."`; inside a quoted field, `""`
represents a literal `"`, and commas/newlines are part of the field's value rather than row/field
separators. A `"` appearing inside an already-started *unquoted* field is rejected as malformed
CSV (a deliberate strictness choice over silently accepting it) -- see §8.

**Row endings**: `\n`, `\r\n`, or a bare `\r` are all accepted as row separators. A wholly blank
line (a common trailing-newline artifact) is skipped rather than treated as a row or an error.

**Whitespace / normalization**: every field is trimmed; `email` is additionally lowercased. This is
the exact same normalization `user.process` has always performed (Phase 3A) -- see §4, "one shared
validator".

**Duplicate rows**: "duplicate" means the same normalized (trimmed, lowercased) email appears in
more than one otherwise-valid row. The *first* occurrence is kept; every later row with that email
is rejected with a reason naming the first row it collided with. This is deterministic and does not
depend on anything but the file's own row order.

**Empty required values**: a blank `name` or `email` (after trimming) rejects only that row, not
the whole file.

### 2.1 Limits

| Limit | Value | Rationale |
|---|---|---|
| File size | 2 MiB | Comfortably covers 1000 small rows with quoting overhead; bounds worst-case memory for a single upload regardless of what the server's own multipart safety net (§7) is set to. |
| Data rows | 1000 | Identical to `services::kMaxUserImportRows`, which is identical to `WorkloadService`'s own per-workload item cap (Phase 3A) -- a CSV that fits this bound can never be rejected a second time, redundantly, once it reaches workload creation. Comfortably covers the stated product range ("50, 100, 500+ users") with headroom, while bounding how long one synchronous HTTP request (§5) can hold the request thread. |
| `name` length | 200 chars | Unchanged from Phase 3A's `user.process` handler. |
| `email` length | 320 chars | RFC 5321's upper bound on a full email address (unchanged from Phase 3A). |
| `phone` length | 32 chars | Unchanged from Phase 3A. |
| Reported rejected-row detail | first 200 | `total_rows`/`invalid_rows` are always exact; the *list* of per-row reasons returned to the caller is capped so a 1000-row, entirely-invalid CSV doesn't force the client to render 1000 error rows. `rejected_rows_truncated` signals when this cap was hit. |

## 3. API

```
POST /api/v1/workloads/user-imports    multipart/form-data, field "file" -- create a user-import workload
GET  /api/v1/workloads/{id}            unchanged (Phase 3A) -- now also reports queued_items/running_items (see §6)
GET  /api/v1/workloads/{id}/items      NEW -- bounded, paginated per-item results (see §8)
```

`POST /api/v1/workloads/user-imports` is a distinct, separately-registered endpoint rather than a
variant of `POST /api/v1/workloads` (Phase 3A's JSON-items endpoint, unchanged and still available
for non-CSV callers) -- multipart file upload and a JSON items array are different enough request
shapes that overloading one endpoint for both would only make each harder to read. Both endpoints
end up calling into the same `WorkloadService` machinery underneath.

### 3.1 Response shape

```json
{
  "id": "…", "type": "user.process", "status": "running",
  "total_items": 98, "queued_items": 1, "running_items": 0, "completed_items": 97, "failed_items": 0,
  "created_at": "…", "updated_at": "…",
  "items": [ { "job_id": "…", "scheduled": true }, … ],
  "total_rows": 100, "valid_rows": 98, "invalid_rows": 2,
  "rejected_rows": [ { "row_number": 99, "reason": "'name' must not be blank" },
                     { "row_number": 100, "reason": "duplicate email (first seen at row 1)" } ],
  "rejected_rows_truncated": false
}
```

`total_rows`/`valid_rows`/`invalid_rows`/`rejected_rows` distinguish "what was in the file" from
`total_items`/`items` ("what actually became a job") -- see §5 for why both are always present and
accurate rather than the response ever implying every uploaded row became a job when it didn't.

## 4. User processing: one shared validator, still one handler

Step 6 of this phase's brief was explicit: reuse `user.process`, do not add another handler. The
handler (`handlers::UserProcessHandler`) is unchanged in behavior. What moved is *where* the
validation/normalization rules live: `domain::validate_and_normalize_user_record()` (new,
`engine/include/flowforge/domain/user_record.hpp`) is the single function that trims `name`,
trims+lowercases `email`, validates both plus optional `phone` against the limits in §2.1, and
returns a `domain::NormalizedUserRecord`. Both call sites use it:

- `UserProcessHandler::execute()` -- extracts `name`/`email`/`phone` from its JSON payload (hand-
  rolled extraction, unchanged from Phase 3A -- the engine has no JSON library dependency), then
  calls the shared validator, then builds its own `ExecutionResult` output.
- `services::parse_user_import_csv()` -- extracts `name`/`email`/`phone` from each CSV row by
  column position, then calls the *same* shared validator.

This guarantees a row the import preview reports as "valid" is guaranteed to be accepted by the
handler at execution time too -- there is exactly one definition of "a valid user record", not two
that could silently drift apart.

`domain::serialize_user_record_as_job_payload()` (also new) is the inverse direction: it hand-
serializes a `NormalizedUserRecord` back into the flat JSON string `{"name": …, "email": …,
"phone": …}` that becomes each CSV-imported row's `Job::payload()` -- exactly the shape
`UserProcessHandler` already parses. `WorkloadService` calls this once per valid row when building
the `CreateWorkloadRequest` it hands to the existing (Phase 3A) `create_workload()`.

No external network calls are made anywhere in this path (Step 6's constraint, unchanged from Phase
3A) -- `UserProcessHandler`'s only failure paths are the deterministic validation rejections
(non-retryable `Result` errors, mirroring `DelayHandler`'s existing convention) and cooperative
cancellation (a non-retryable `ExecutionResult::failure`), exactly as in Phase 3A.

## 5. Bulk submission semantics

`WorkloadService::create_user_import_workload()` first calls `parse_user_import_csv()`, then --
only if that succeeds structurally -- delegates to the existing `create_workload()` for the valid
rows. This produces a clean split:

- **Whole-file rejection** (nothing created at all): empty/oversized file, invalid UTF-8, missing
  header row, missing/unrecognized/duplicate header column, an unterminated quoted field, or more
  data rows than the 1000-row limit allows. The HTTP response is a `400`; no `Workload` row and no
  `Job` rows exist afterward.
- **Row-level rejection** (that row is skipped, everything else proceeds): a blank required field,
  a malformed email, a field over its length limit, a row with the wrong field count, a duplicate
  email. The workload is still created, with `total_items` equal to the *valid* row count -- never
  the raw CSV row count, so the response can never imply a row became a job when it didn't (see
  §3.1's `total_rows` vs. `total_items` distinction).
- **Per-item dispatch failure** (already existing Phase 3A behavior, reused unchanged): even a
  structurally-valid row's `Job` can fail to schedule (scheduler at capacity) or, more rarely, fail
  to even insert (a transient database error). Reported per-item in the `"items"` array exactly
  like `POST /api/v1/jobs`'s existing `"scheduling"` field -- never fails the whole request.
- **Zero valid rows**: still creates a workload, with `total_items == 0` -- `domain::
  derive_workload_status` (Phase 3A) reports a zero-item workload as immediately `Succeeded`. The
  response's `invalid_rows`/`rejected_rows` still show exactly what was wrong with every row, so
  this is never silent -- see `workload-model.md` §3.
- **Repeated upload of the same file**: each `POST /api/v1/workloads/user-imports` call creates a
  brand-new `Workload` and a brand-new set of `Job` rows -- there is no request-level idempotency
  key or deduplication. This is a deliberate, documented choice (Step 5 of the phase brief
  explicitly permits it: "otherwise explicitly document that each upload creates a new workload,"
  and warns against over-engineering a distributed idempotency mechanism this product doesn't yet
  need). The only mitigation against an *accidental* double-submission is client-side: the
  dashboard's "Start Import" button disables itself while a request is in flight (see §9), the same
  pattern every other mutating action in this dashboard already uses
  (`create-job-form.tsx`/`cancel-job-button.tsx`). A user who deliberately re-uploads the same file
  gets two independent workloads and, if any rows share an email, two `user.process` jobs
  processing the same email independently -- accepted as out of scope for this phase.

## 6. Progress calculation

Phase 3A computed only `completed_items`/`failed_items` (a two-way split). This phase extends
`domain::Workload` to a four-way breakdown -- `queued_items`, `running_items`, `completed_items`,
`failed_items` -- because the dashboard needs to show `queued`/`running`/`succeeded`/`failed`
(Step 7 of the phase brief), not just "done" vs. "not done". `domain::WorkloadItemOutcome` gained
two buckets (`Queued`, `Running`, replacing the old single `Active` bucket); `domain::
classify_job_status_for_workload` now maps each child `JobStatus` into one of the four:

| `JobStatus` | Bucket | Why |
|---|---|---|
| `Pending`, `Queued`, `Retrying`, `Failed` (attempt) | Queued | Not currently executing; will run (again) soon. A failed *attempt* is grouped here, not "Failed" -- see workload-model.md §3: it isn't terminal, `RetryDispatcher` may still retry it. |
| `Running` | Running | Actively executing right now. |
| `Succeeded` | Succeeded (`completed_items`) | Terminal, successful. |
| `Cancelled`, `DeadLetter` | Failed | Terminal, unsuccessful. |

This is still computed entirely on read (Phase 3A's design, unchanged): `WorkloadService::
with_progress()` queries every child `Job` via `IJobRepository::list_by_workload_id` (bounded by
`total_items`, itself bounded to 1000) and tallies the four buckets fresh on every `GET
/api/v1/workloads/{id}` call. There is still no persisted/incrementally-updated counter and no
`JobExecutor` callback -- see workload-model.md §3 and §9 below for why, and what would change if
that ever becomes necessary.

`derive_workload_status` (Phase 3A) is unaffected: it only ever needed `completed_items`/
`failed_items`/`total_items` to decide `Running`/`Succeeded`/`Failed` -- `queued_items`/
`running_items` are purely additive display detail.

## 7. Security review

- **Multipart/CSV size**: bounded twice -- `httplib::Server::set_payload_max_length(8 MiB)`
  (`apps/server/src/http/app.cpp`) is a coarse, server-wide safety net on how much of *any* request
  body httplib will buffer before a handler runs; `parse_user_import_csv`'s own 2 MiB check (§2.1)
  is the precise, CSV-specific bound with a clear error message. Both exist because they serve
  different purposes -- the first protects the process regardless of which endpoint is hit, the
  second gives a caller a meaningful reason their specific upload was rejected.
- **Row count / field lengths**: bounded (§2.1); enforced before any `Job` is created.
- **Malformed CSV**: parsed by a bounded, single-pass, hand-rolled tokenizer (no recursion, no
  backtracking) that aborts as soon as the row-count bound would be exceeded rather than fully
  tokenizing an oversized file first.
- **Maliciously large single field**: bounded transitively by the 2 MiB whole-file cap -- no field
  can exceed the file's own bounded size, and the semantic length checks (§2.1) reject anything
  over the real per-field limits regardless.
- **Formula injection**: not applicable in this phase -- imported values are never written back out
  as CSV/spreadsheet content (no CSV export exists), only ever rendered as plain text in JSON API
  responses and the dashboard's React tree (which escapes text content by default; see the next
  bullet).
- **HTML/script injection**: the dashboard renders `name`/`email`/`reason`/error strings as plain
  React text children (`{value}`), never `dangerouslySetInnerHTML` -- React escapes these by
  construction, so a name like `<script>` in a CSV row is inert everywhere it's displayed.
- **Email/phone input**: validated structurally (§2.1); never interpreted as executable content
  anywhere in the pipeline.
- **Error leakage / raw CSV logging**: `WorkloadService` logs `workload_id`/row counts/rejection
  *reasons* on a parse failure -- never the uploaded file's raw bytes (see
  `create_user_import_workload`'s implementation comment). A 5xx-classified error's message is
  still sanitized by the existing, shared `to_error_body()` (`apps/server/src/http/
  error_response.cpp`, Phase 2B-5, unchanged) before it ever reaches an HTTP response; a 4xx
  validation message (e.g. "CSV header is missing the required 'email' column") is always
  application-generated and safe to show verbatim, exactly like every other endpoint.
- **Database parameterization**: unchanged -- every query this phase touches
  (`postgres_workload_repository.cpp`, `postgres_job_repository.cpp`) already used
  `pqxx::work::exec_params` before this phase; nothing new concatenates a value into SQL text.
- **Denial of service via huge imports**: the 1000-row/2 MiB bounds (§2.1) cap the cost of a single
  upload; `create_user_import_workload` processes the whole request synchronously (§9), so an
  attacker cannot use this endpoint to queue unbounded background work either -- the request itself
  bounds the work.

## 8. Bounded item retrieval

`GET /api/v1/workloads/{id}/items?limit=&offset=` (default `limit=50`, capped at 200, mirroring
`JobService::list_jobs`'s clamping convention) is the only way to see individual row results --
`GET /api/v1/workloads/{id}` itself never returns a per-item list, only aggregate counts. This keeps
both endpoints bounded regardless of whether a workload has 10 items or 1000:
`IJobRepository::list_by_workload_id` gained an `offset` parameter (both `InMemoryJobRepository` and
`PostgresJobRepository`) for exactly this, on top of its existing `limit` (Phase 3A).

Each returned item (`apps/server/src/json/workload_json.cpp::to_json_workload_item`) reuses the
job's own `payload()` -- the same normalized `{"name": …, "email": …}` JSON `user.process` parses
(§4) -- to surface `name`/`email` without a second query or a new persisted column; a job whose
payload isn't that exact shape (not created via CSV import) degrades to `null` fields rather than
erroring the whole page. `status`/`attempt_count`/`last_error`/`updated_at` come straight from
`domain::Job` -- never a stack trace or raw exception text.

## 9. Polling behavior, and a future streaming upgrade

The dashboard (`WorkloadProgressPanel`, `apps/dashboard/src/components/workloads/
workload-progress-panel.tsx`) polls `GET /api/v1/workloads/{id}` every 2 seconds, starting
immediately on mount (even when it already has an initial server-rendered `Workload`, so it reflects
genuinely live state rather than a snapshot that may already be stale). It stops polling the moment
the workload reaches a terminal status (`succeeded`/`failed`) -- no more progress will ever be made,
so there's nothing further to observe. The polling loop is cancelled on unmount (component
teardown/navigation) via a cleanup closure, and only one request is ever in flight at a time (the
next poll is scheduled only after the previous one resolves, not on a fixed `setInterval` that could
overlap a slow response). A network/API error during a poll is shown inline but does not stop
polling -- it retries on the same interval rather than freezing on stale data with no explanation.

This is a deliberate choice for this phase, not an oversight: WebSockets/SSE were explicitly out of
scope (Step 11 of the phase brief), and polling every 2 seconds against a `GET` that's already a
bounded, indexed query (§6) is more than adequate at the 100-1000-item scale this phase targets. A
future phase could replace this with a server-push mechanism (SSE is the more natural fit than a
full WebSocket, given the traffic is one-directional) if/when higher-frequency updates or many
concurrent viewers make polling's request volume a real cost -- nothing in this phase's API shape
(`GET /api/v1/workloads/{id}` returning a plain snapshot) would need to change for that; only the
delivery mechanism would.

## 10. Frontend

- `/users` (`apps/dashboard/src/app/users/page.tsx`) -- a server-rendered shell (title/description)
  wrapping `<UserImportWizard />`, a client component owning the whole upload -> preview -> import
  -> progress flow.
- `csv-preview.ts` (`apps/dashboard/src/lib/`) -- a small, **non-authoritative** client-side CSV
  tokenizer/validator used only to render an instant preview (row counts, a bounded table of the
  first 20 rows, obvious per-row problems) before the user clicks "Start Import". It deliberately
  mirrors the server's column/quoting rules closely enough to rarely disagree, but the server
  (`parse_user_import_csv`) is what actually decides what gets imported -- see the file's own
  header comment. A preview parse failure (or a file too large to preview -- skipped above 5 MiB)
  never blocks the real upload from being attempted.
- `/workloads/[id]` (`apps/dashboard/src/app/workloads/[id]/page.tsx`) -- a server-rendered detail
  page (id/type/total/created/updated, fetched once at request time, 404 via `notFound()` for an
  unknown id -- mirrors `/jobs/[id]`'s existing pattern) embedding the same `<WorkloadProgressPanel>`
  the import wizard uses, plus `<WorkloadItemsTable>` (paginated, §8).
- No new UI library was added -- drag-and-drop uses native HTML5 DnD events on a `<label>` wrapping
  a visually-hidden (`sr-only`, not `display:none`, so it stays keyboard/screen-reader reachable)
  `<input type="file">`; the file input is also reachable and operable via keyboard alone (native
  `<label for>` association).

## 11. Known limitations

- Client-side CSV preview is approximate (§10) -- it can disagree with the server's authoritative
  parse in rare edge cases (e.g. an unusual quoting pattern); this is expected and documented, never
  presented as the real outcome.
- `UserProcessHandler`'s hand-rolled JSON extraction and the CSV parser's UTF-8 check are both
  bounded-scope, not fully spec-complete (see workload-model.md §8 for the JSON-escape limitation,
  carried over unchanged; the UTF-8 check here validates structural well-formedness, not full
  Unicode semantics -- it does not reject overlong encodings or surrogate code points).
- Email validation is structural (`domain::looks_like_email`), not RFC 5322-complete -- unchanged
  from Phase 3A.
- `GET /api/v1/workloads/{id}/items` performance is proportional to the page requested, not the
  workload's total size (bounded, indexed) -- but `GET /api/v1/workloads/{id}`'s own progress
  computation (§6) still re-scans every child job on every call; at 1000 items this remains a small,
  indexed query, not a concern at this phase's scale (see workload-model.md §8 for the same
  tradeoff, unchanged).

## 12. Deferred to a future phase

- Chunked/streamed upload for files larger than this phase's 2 MiB/1000-row bounds.
- Asynchronous workload submission (create the `Workload` row immediately, dispatch jobs in the
  background) -- not needed while `create_user_import_workload` comfortably completes within one
  HTTP request at this phase's scale.
- A `JobExecutor` -> `WorkloadRepository` progress-push callback, replacing the read-time
  aggregation in §6, if that read cost ever becomes a real problem (see workload-model.md §9).
- Server-push progress updates (SSE), replacing polling (§9), if update frequency or viewer count
  ever make polling's request volume a real cost.
- `DELETE /api/v1/workloads/{id}` and any workload-level cancellation.
- Any workload type other than `user.process` (image processing, email jobs, report generation,
  webhook processing, data exports) -- see workload-model.md §1 and §9.
