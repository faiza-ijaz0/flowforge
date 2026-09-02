# FlowForge Input Processing Architecture (Phase 3C)

This document describes the architectural foundation for FlowForge accepting input from multiple
*source* types (CSV today; Image, Screenshot, Text, and URL as future work) and routing it to
multiple *processing targets* (Users today; Products and Categories as future work), all through
the same engine every other workload already uses. It complements
[`workload-model.md`](workload-model.md) (the generic Workload abstraction) and
[`user-import.md`](user-import.md) (the first concrete, CSV+Users pipeline, unchanged by this
phase) rather than replacing either.

**This phase does not implement OCR, image recognition, or any external AI/LLM service.** The
`Image`/`Screenshot`/`Text`/`Url` source types exist in the domain model so the architecture has a
real place to grow into, but every one of them is rejected today with an explicit "not yet
supported" error -- never a fake success. See §7.

## 1. What this phase adds

```
Next.js Web Application ("Processing Center", /processing)
        |
   FlowForge API  (POST /api/v1/process)
        |
InputProcessingService  -- orchestrates, owns no scheduler/worker/db logic itself
        |
   +--------------------------+
   |                          |
IInputExtractor         WorkloadService  (Phase 3A, still domain-agnostic, unchanged)
(CsvExtractor)                |
   |                     JobService / PriorityScheduler / LocalWorkerPool / JobExecutor
StructuredRecord              |
                          PostgreSQL
```

Two new, independent pieces sit above the unchanged engine:

- **A generic extraction pipeline** (`engine::IInputExtractor`, `extractors::CsvExtractor`,
  `domain::StructuredRecord`/`ExtractionResult`) that turns raw input bytes into a source-agnostic,
  target-agnostic record shape. This is genuinely new code, fully tested, and is what a future
  Image/Screenshot/URL extractor and a future Products/Categories target both build on -- but it is
  **not** on the request path of anything this phase actually serves (see §5).
- **`services::InputProcessingService`**, a thin orchestrator with one public method,
  `process(ProcessRequest) -> Result<ProcessResult>`. It decides whether a `(source, target)`
  combination is implemented; for the one combination that is (CSV + Users), it delegates to the
  same `services::import_users_from_csv()` that already backs `POST
  /api/v1/workloads/user-imports` (see §5). It contains no scheduler, worker-pool, retry, or
  database logic of its own -- exactly like `services::import_users_from_csv()` itself, it is a
  composition point over `WorkloadService`, never a reimplementation of anything underneath it.

Nothing about `WorkloadService`, `JobService`, `PriorityScheduler`, `LocalWorkerPool`,
`JobExecutor`, `HandlerRegistry`, `IJobRepository`, `IWorkloadRepository`, or the
`workloads`/`jobs` schema changed in this phase. `WorkloadService` remains exactly as
domain-agnostic as `user-import.md` §1.1 left it -- grep its header and implementation and neither
"input", "extractor", "csv", nor "process" (in the new sense) appears.

## 2. Domain model

| Type | File | Purpose |
|---|---|---|
| `domain::InputSourceType` | `domain/input_source.hpp` | `Csv \| Image \| Screenshot \| Text \| Url`. `to_string`/`input_source_type_from_string` round-trip the wire representation (lowercase strings, matching the JSON API). |
| `domain::InputPayload` | `domain/input_source.hpp` | `{source_type, content}` -- the raw bytes/text an extractor consumes. Content is opaque to this type; only a specific `IInputExtractor` knows how to interpret it. |
| `domain::ProcessingTarget` | `domain/processing_target.hpp` | `Users \| Products \| Categories`. `to_string`/`processing_target_from_string` round-trip; `job_type_for_processing_target` maps each target to the job type string its handler is registered under (`"user.process"`/`"product.process"`/`"category.process"`). |
| `domain::StructuredRecord` | `domain/structured_record.hpp` | A generic `field(name) -> value` map (`std::map<std::string, std::string, std::less<>>`) -- one record extracted from one input document. Deliberately has no concept of "name"/"email"/"sku"/anything business-specific; those are target-specific interpretations layered on top by a future adapter, exactly the way `import_users_from_csv` layers user semantics on top of generic CSV tokenization today (§5). |
| `domain::RejectedRecord` | `domain/structured_record.hpp` | `{index, reason}` -- one record (or CSV row) an extractor could not turn into a `StructuredRecord`, and why. |
| `domain::ExtractionResult` | `domain/structured_record.hpp` | `{total_records, records, rejected_records, rejected_record_count, rejected_records_truncated}` -- the full result of one `IInputExtractor::extract()` call, mirroring the shape `user-import.md` §2.1's "reported rejected-row detail" cap already established (first 200 reported, `_truncated` flag for the rest). |

None of these four files depend on `services::`, `handlers::`, or anything user/product/category
-specific -- they sit at the same layer as `domain::Job`/`domain::Workload` (Phase 3A).

## 3. Extraction: `IInputExtractor` and `CsvExtractor`

`engine::IInputExtractor` (`engine/input_extractor.hpp`) mirrors the shape of `engine::IJobHandler`
(Phase 1/3A): a small interface with a `source_type()` identity method and one `extract(InputPayload)
-> Result<ExtractionResult>` method. Concrete extractors are registered against a source type the
same way `IJobHandler` implementations are registered against a job type -- a future
`ExtractorRegistry` (not needed yet, since exactly one extractor exists) would follow
`HandlerRegistry`'s existing pattern exactly.

`extractors::CsvExtractor` (`engine/extractors/csv_extractor.{hpp,cpp}`) is the only concrete
implementation this phase ships:

- Reuses `infra::tokenize_csv`/`infra::is_valid_utf8`/`infra::strip_utf8_bom`
  (`engine/infra/csv.hpp`) -- the exact same generic tokenizer `services::parse_user_import_csv`
  uses (`user-import.md` §1.1). There is exactly one RFC 4180 CSV implementation in this codebase;
  `CsvExtractor` does not duplicate it.
- Is genuinely generic: the header row's column names become each record's field names verbatim,
  with **no required columns and no business-rule validation** -- a CSV with `sku,price` columns or
  `title,slug` columns extracts identically to one with `name,email` columns. Only structural
  checks apply: non-empty input, size/row-count bounds, valid UTF-8, no duplicate header column,
  and (per-row, non-fatal) that every data row has the header's field count.
- Bounds (`engine/src/extractors/csv_extractor.cpp`): `kMaxCsvBytes = 2 MiB`, `kMaxRecords = 1000`,
  `kMaxReportedRejectedRecords = 200` -- numerically identical to `services::kMaxUserImportRows`
  and `user-import.md` §2.1's limits today, but defined independently and locally within
  `extractors::`. This is deliberate, not an oversight: `extractors::` sits below `services::` in
  this codebase's dependency direction (the same relationship `handlers::` has to `services::`), so
  it structurally cannot reference a `services::` constant. If the two bounds ever need to diverge
  (e.g. a future Products CSV wants a different row cap than Users), nothing here would need to
  change.

Tests: `engine/tests/extractors/csv_extractor_test.cpp` exercises generic (non-user) column
extraction, wrong source-type rejection, empty input, header-only input, wrong field count
(per-row, non-fatal), quoted fields, duplicate header, malformed CSV, invalid UTF-8, and the
row-count limit -- entirely independent of any "user" fixture data, to keep it honestly generic.

## 4. `InputProcessingService`

`services::InputProcessingService` (`services/input_processing_service.{hpp,cpp}`) is constructed
with the same `shared_ptr<WorkloadService>`/`shared_ptr<Logger>`/`shared_ptr<MetricsRegistry>`
triple every other service in this codebase takes. Its one method:

```cpp
Result<ProcessResult> process(const ProcessRequest& request);
// ProcessRequest { InputSourceType source_type; ProcessingTarget target; std::string payload; }
```

first checks `is_supported(source_type, target)` -- today, `true` only for `(Csv, Users)`. Every
other combination returns a `Result` error (`ErrorCode::Validation`, message `"source '<x>' is not
yet supported for target '<y>'"`), logs a warning, and increments the
`flowforge_process_unsupported_total` counter -- **no `Workload` row, no `Job` row, nothing is
created**. For the one supported combination, it delegates entirely to
`services::import_users_from_csv()` (§5) and translates the result into the generic `ProcessResult`
shape.

`ProcessResult` deliberately mirrors `services::UserImportResult`
(`total_records`/`valid_records`/`invalid_records`/`rejected_records`/`rejected_records_truncated`
alongside the created `Workload` and its dispatch `items`) under source-agnostic field names, so a
future non-CSV or non-Users combination extends the same response shape rather than inventing a
new one.

## 5. Why `/api/v1/process` delegates rather than reimplements

The one implemented combination, CSV + Users, could have been built two ways: route it through the
new `CsvExtractor`/`StructuredRecord` pipeline and a new user-specific adapter on top of it, or
delegate straight to the already-shipped `services::import_users_from_csv()` (`user-import.md`
§1.1) that `POST /api/v1/workloads/user-imports` already uses. This phase chose to **delegate**:

- **Zero duplication risk.** `import_users_from_csv` already owns the full CSV contract
  (`user-import.md` §2), bulk-submission semantics (§5 there), and is exercised by
  `engine/tests/services/user_import_test.cpp` and the 100-user acceptance test. Routing
  `/api/v1/process` through it means the two endpoints can never silently drift into different
  behavior for the same input.
- **Guaranteed behavioral parity.** A CSV that `/api/v1/workloads/user-imports` accepts is
  guaranteed to be accepted identically by `/api/v1/process`, because it is the same function call
  underneath, not a second implementation of "what makes a user record valid."
- **`CsvExtractor` isn't ready to own this today.** It is deliberately generic -- it has no concept
  of "name"/"email" being required, no email-format validation, no per-field length limits, none of
  `domain::validate_and_normalize_user_record`'s rules (`user-import.md` §4). Wiring
  `CsvExtractor` + `StructuredRecord` into the Users path today would mean either duplicating all of
  that validation in a new `StructuredRecord -> NormalizedUserRecord` adapter (exactly the
  duplication this phase's brief prohibits), or loosening `CsvExtractor` itself to know about users
  (violating its own reason for existing).

### Why `CsvExtractor` is not yet wired into `/users`

`CsvExtractor`/`StructuredRecord`/`IInputExtractor` are real, tested, standalone components --
not scaffolding -- but they exist to serve **future** targets that have no legacy pipeline to
protect, not to replace the proven one. The mechanical path for a future target to use them:

1. A `services::structured_record_to_<target>()` adapter (mirrors
   `domain::validate_and_normalize_user_record`) that turns a generic `StructuredRecord` into a
   normalized, target-specific record, applying that target's own required-field and length rules.
2. `InputProcessingService::is_supported()` gains the new `(source, target)` pair; its `process()`
   branches to call `CsvExtractor::extract()` -> the new adapter -> `WorkloadService::create_workload()`
   for that combination, instead of a `import_<target>_from_csv()` free function duplicating
   `import_users_from_csv`'s shape.
3. Once a *second* source type exists for the same target (e.g. Products via both CSV and a future
   structured URL feed), `CsvExtractor` and the new source's extractor both feed the *same* adapter
   -- this is the point where having a shared `StructuredRecord` intermediate representation instead
   of two independent CSV-shaped and URL-shaped user-record parsers starts paying for itself. For
   Users today, with exactly one source (CSV) and one already-shipped, fully-tested pipeline, that
   payoff doesn't yet exist -- which is why `/users` keeps using `import_users_from_csv` directly
   rather than being rewritten onto `CsvExtractor` for no behavioral benefit.

`/api/v1/workloads/user-imports` is not deprecated by `/api/v1/process` -- both remain live
endpoints calling into the same underlying function; `/api/v1/process` is the new,
source-/target-agnostic front door, while the original endpoint remains available for any existing
caller that only ever wants CSV + Users.

## 6. API

```
POST /api/v1/process    multipart/form-data, fields "source", "target", "file"
```

`source` and `target` are plain multipart text fields (not files) but arrive through
`req.has_file("source")`/`req.get_file_value("source").content` -- httplib places every multipart
part, file or plain text, into `req.files`; there is no separate `req.params` population for
multipart text fields (confirmed by inspecting the vendored `httplib.h`). This is unrelated to any
FlowForge design choice; it is simply how the HTTP library used here represents multipart bodies.

### 6.1 Request validation, and why each rejection is a `400`

| Condition | Response |
|---|---|
| Not `multipart/form-data` | `400`, `"request must be multipart/form-data with 'source', 'target', and 'file' fields"` |
| Missing `source`/`target`/`file` field | `400`, `"missing required '<field>' field"` |
| `source` value not one of `csv\|image\|screenshot\|text\|url` | `400`, `"unrecognized 'source' value '<value>'"` |
| `target` value not one of `users\|products\|categories` | `400`, `"unrecognized 'target' value '<value>'"` |
| Recognized `source`/`target`, but not an implemented combination | `400`, `"source '<x>' is not yet supported for target '<y>'"` (from `InputProcessingService`, §4) |
| `(csv, users)`, but the CSV itself fails `import_users_from_csv`'s whole-file checks (`user-import.md` §5) | `400`, that check's own message (e.g. missing header column) |
| `(csv, users)`, CSV structurally valid | `201`, `ProcessResult` JSON (§6.2) -- **even if every row was rejected at the row level**, since a workload with 0 valid items is still a real, created workload (`user-import.md` §5, "zero valid rows") |

Every rejection path returns before any `Workload` or `Job` row is created -- there is no
partial-success path that creates a workload for an unsupported combination and then reports it as
an error after the fact.

### 6.2 Response shape (the one implemented combination)

```json
{
  "id": "…", "type": "user.process", "status": "running",
  "total_items": 98, "queued_items": 1, "running_items": 0, "completed_items": 97, "failed_items": 0,
  "created_at": "…", "updated_at": "…",
  "items": [ { "job_id": "…", "scheduled": true }, … ],
  "total_records": 100, "valid_records": 98, "invalid_records": 2,
  "rejected_records": [ { "index": 99, "reason": "'name' must not be blank" } ],
  "rejected_records_truncated": false
}
```

Identical in shape to `POST /api/v1/workloads/user-imports`'s response (`user-import.md` §3.1)
except for the source-agnostic field names (`total_records` vs. `total_rows`, `rejected_records`
vs. `rejected_rows`) -- this is a direct consequence of delegating to the same underlying result
(§5), not a coincidence to maintain by hand.

## 7. What is, and is not, implemented

**Implemented and feature-complete**: `source=csv`, `target=users`. Uploads a CSV, creates a real
`Workload`, dispatches one real `user.process` `Job` per valid row through the unchanged
scheduler/worker-pool/executor pipeline, persists to PostgreSQL -- identical in every observable
way to `POST /api/v1/workloads/user-imports` (§5).

**Explicitly not implemented, and never faked**: every other `(source, target)` pair --
`image`/`screenshot`/`text`/`url` for any target, and `products`/`categories` for any source
(including `csv`). Each returns the `400` "not yet supported" response in §6.1. No OCR, image
recognition, or external AI/LLM call happens anywhere in this codebase for `image`/`screenshot`;
those source types exist only as domain enum values and rejected-at-validation strings. There is no
code path, tested or otherwise, that returns a `201` for any of these combinations.

## 8. Security review

This phase's only new *data* path is CSV + Users, which is `services::import_users_from_csv()`
itself -- `user-import.md` §7's security review (payload size bounds, row/field length bounds,
malformed-CSV handling, no raw-upload logging, parameterized SQL, formula/HTML injection analysis)
applies unchanged, since `/api/v1/process` calls the exact same function. Additionally, specific to
the new endpoint:

- **Unrecognized `source`/`target` strings** are rejected by `input_source_type_from_string`/
  `processing_target_from_string` before anything touches `InputProcessingService` -- an
  attacker-supplied value never reaches a `switch`/branch as an unvalidated string.
- **No fake success as a side channel.** Because unsupported combinations are rejected before any
  `Workload`/`Job` row is created, there is no way to use this endpoint to create workload rows for
  an unimplemented target/source pair and have them silently sit in a stuck or fabricated state --
  `is_supported()` is checked first, synchronously, every time.
- **`CsvExtractor` itself** (§3), though not on this phase's request path, was still built to the
  same bounds discipline as the rest of the codebase (2 MiB/1000-row caps, bounded rejected-record
  reporting) so that wiring it into a future target's endpoint is a mechanical, not a security,
  exercise.

## 9. Testing

| Layer | File | Covers |
|---|---|---|
| Domain | `engine/tests/domain/input_source_test.cpp` | `InputSourceType` round-trip, unrecognized-string rejection |
| Domain | `engine/tests/domain/processing_target_test.cpp` | `ProcessingTarget` round-trip, `job_type_for_processing_target` mapping, unrecognized-string rejection |
| Domain | `engine/tests/domain/structured_record_test.cpp` | `StructuredRecord::field()` lookup (present/absent), `ExtractionResult` default state |
| Extraction | `engine/tests/extractors/csv_extractor_test.cpp` | Generic (non-user) extraction, wrong source type, empty/header-only input, wrong field count (non-fatal), quoting, duplicate header, malformed CSV, invalid UTF-8, row-count limit |
| Service | `engine/tests/services/input_processing_service_test.cpp` | CSV+Users creates a real workload with real dispatched jobs (verified against the job repository); partial-invalid CSV reports `rejected_records`; every `Image`/`Screenshot`/`Text`/`Url` source rejected without creating a workload; `Products`/`Categories` rejected even for CSV; empty payload rejected |
| HTTP | `apps/server/tests/process_routes_test.cpp` | `201` end-to-end for CSV+Users (verifying response fields); `400` for each unsupported source and unsupported target; unrecognized `source`/`target` strings; missing `file` field; non-multipart request; invalid rows reported (not silently dropped or silently succeeded) |
| Regression | `engine/tests/services/user_import_test.cpp`, `apps/server/tests/user_import_routes_test.cpp` | Unchanged -- `POST /api/v1/workloads/user-imports` and `import_users_from_csv` are untouched by this phase; both suites continue to pass, proving no regression |

## 10. Frontend

- `/processing` (`apps/dashboard/src/app/processing/page.tsx`) -- "Processing Center": a
  server-rendered shell wrapping `<ProcessingUploadPanel>`, a client component with a target
  selector (Users/Products/Categories) and a source selector (CSV/Image/Screenshot/Text/URL).
  Every option is always visibly present; only `Users` and `CSV` are clickable -- every other
  option is disabled and carries a visible "Coming soon" pill (`<SelectorButton>`), never a
  silently-do-nothing button. Selecting an unsupported combination shows an inline explanation
  instead of a file-upload control. Selecting the one supported combination shows the same
  upload -> result -> `<WorkloadProgressPanel>` flow `<UserImportWizard>` uses, through the new
  `apiClient.process(source, target, file)` (`lib/api-client.ts`), calling `POST /api/v1/process`.
- `<ProcessingUploadPanel>` (`components/processing/processing-upload-panel.tsx`) is intentionally
  a thinner panel than `<UserImportWizard>` (`user-import.md` §10) -- it has no client-side CSV
  preview (`csv-preview.ts` stays `/users`-specific), because it exists to prove out the *generic*
  source x target flow, not to duplicate the richer, User-specific import wizard. Every future
  target gets this generic panel by default once its extractor and adapter ship (§5); `/users`
  keeps its own richer wizard because it already has one.
- `/users`'s known UX gap (no link from a successful import to its workload) is fixed:
  `<UserImportWizard>`'s "created" state now shows a **View Processing Workload** button
  navigating to `/workloads/{id}`, alongside the existing "New import" action. `/users` also gains
  an **Open Processing Center** link in its page header, pointing at `/processing`.
- Sidebar (`components/layout/sidebar.tsx`) gains a **Processing** entry between **Users** and
  **Jobs**.
- `packages/shared/src/processing.ts` -- `InputSourceType`, `ProcessingTarget`, `RejectedRecord`,
  `ProcessResponse` (extends `Workload`, mirroring `apps/server/src/json/process_json.hpp`'s
  `to_json(ProcessResult)`), exported from the package's `index.ts` alongside the existing
  `workload`/`job`/etc. modules.

## 11. Known limitations

- `CsvExtractor`/`StructuredRecord`/`IInputExtractor` are real and tested but are not on any
  request path this phase actually serves end-to-end (§5) -- they are foundation for a future
  target, not yet exercised by a live HTTP endpoint.
- `Image`/`Screenshot`/`Text`/`Url` are domain-modeled enum values with no corresponding extractor,
  by design (§7) -- adding a real one is a distinct future phase's scope, not partially started
  here.
- `Products`/`Categories` have `job_type_for_processing_target` mappings (`"product.process"`/
  `"category.process"`) but no registered `IJobHandler` for either job type and no
  `import_products_from_csv`/`import_categories_from_csv` adapter -- consistent with
  `user-import.md` §1.1's stated future path, still entirely unbuilt.
- `<ProcessingUploadPanel>` has no client-side CSV preview, unlike `<UserImportWizard>` -- a
  deliberate scope choice (§10), not an oversight.

## 12. Deferred to a future phase

- A real `Image`/`Screenshot` extractor backed by OCR or an external vision service, and a `Text`/
  `Url` extractor -- explicitly out of scope for this phase (see this document's opening note).
- `product.process`/`category.process` handlers and their CSV-import adapters, following the
  mechanical pattern `user-import.md` §1.1 and this document's §5 both lay out.
- An `ExtractorRegistry` mirroring `HandlerRegistry`, once a second concrete `IInputExtractor`
  exists to justify one (today there is exactly one, so a registry would be unused indirection).
- Migrating `/api/v1/workloads/user-imports`/`import_users_from_csv` onto the
  `CsvExtractor`/`StructuredRecord` pipeline, if and when a second CSV-consuming target makes the
  shared intermediate representation actually pay for itself (§5).
