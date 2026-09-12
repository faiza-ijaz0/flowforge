# FlowForge Input Processing Architecture (Phase 3C, extended in Phase 3D-1)

This document describes the architectural foundation for FlowForge accepting input from multiple
*source* types (CSV and, as of Phase 3D-1, Image/Screenshot; Text and URL remain future work) and
routing it to multiple *processing targets* (Users today; Products and Categories as future work),
all through the same engine every other workload already uses. It complements
[`workload-model.md`](workload-model.md) (the generic Workload abstraction) and
[`user-import.md`](user-import.md) (the first concrete, CSV+Users pipeline, unchanged by this
phase) rather than replacing either.

**Phase 3C did not implement OCR, image recognition, or any external AI/LLM service** -- the
`Image`/`Screenshot` source types existed only as domain enum values, rejected with an explicit
"not yet supported" error. **Phase 3D-1 (this revision) implements real OCR-based image
extraction** for `(Image|Screenshot, Users)` via a real, locally-run Tesseract OCR engine -- see
§13 onward. `Text` and `Url` remain unimplemented, exactly as before; every combination this
document doesn't describe as implemented is still rejected with an honest, non-fake `400`, never a
silent no-op or fabricated success.

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

**Implemented and feature-complete (direct `POST /api/v1/process`)**: `source=csv`, `target=users`.
Uploads a CSV, creates a real `Workload`, dispatches one real `user.process` `Job` per valid row
through the unchanged scheduler/worker-pool/executor pipeline, persists to PostgreSQL -- identical
in every observable way to `POST /api/v1/workloads/user-imports` (§5).

**Implemented via preview + confirm (Phase 3D-1, §13 onward)**: `source=image` or `screenshot`,
`target=users`, when a Tesseract OCR binary is available on the machine running the server (see
§16, "Why the Tesseract CLI, not libtesseract"). `POST /api/v1/process/preview` runs real OCR (no
mock, no hardcoded sample records) and returns extracted, normalized records without creating
anything; `POST /api/v1/process/confirm` submits them into the same real workload pipeline CSV
uses. `image`/`screenshot` are deliberately **never** accepted by direct `POST /api/v1/process` --
see §17, "Why `process()` never accepts `Image`/`Screenshot`". If no Tesseract binary is found at
server startup, `preview()` cleanly returns the same "not yet supported" `400` as before -- never a
crash, never a fake extraction.

**Explicitly not implemented, and never faked**: `text`/`url` for any target, and
`products`/`categories` for any source (including `csv` and `image`). Each returns the `400` "not
yet supported" response in §6.1. No external AI/LLM call happens anywhere in this codebase for any
source type -- image extraction is 100% local (§16). There is no code path, tested or otherwise,
that returns a successful extraction or a `201` for any of these combinations.

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

- `CsvExtractor`/`StructuredRecord`/`IInputExtractor` are real and tested but `CsvExtractor`
  specifically is still not on any request path this phase actually serves end-to-end (§5) -- it
  is foundation for a future CSV-consuming target, not yet exercised by a live HTTP endpoint.
  `extractors::ImageExtractor` (Phase 3D-1), by contrast, *is* on a live path (`preview`/`confirm`).
- `Text`/`Url` are still domain-modeled enum values with no corresponding extractor, by design
  (§7) -- adding a real one is a distinct future phase's scope, not partially started here.
- `Products`/`Categories` have `job_type_for_processing_target` mappings (`"product.process"`/
  `"category.process"`) but no registered `IJobHandler` for either job type and no
  `import_products_from_csv`/`import_categories_from_csv` adapter -- consistent with
  `user-import.md` §1.1's stated future path, still entirely unbuilt. Image extraction for these
  targets is therefore also unbuilt, even though the OCR pipeline itself is target-agnostic in
  principle (§18).
- `<ProcessingUploadPanel>` has no client-side CSV preview, unlike `<UserImportWizard>` -- a
  deliberate scope choice (§10), not an oversight.
- The table-reconstruction heuristic (§14) is genuinely real but is a heuristic, not true layout
  analysis: it can mis-split a cell containing an unusually wide internal gap (e.g. a long dash) or
  under-split two visually close columns. Rows it cannot square with the header's column count are
  rejected, never silently guessed at (§14) -- but a row that *does* parse can still contain a
  wrong split in a genuinely ambiguous image. This is why the product's own design puts a mandatory
  human preview/confirm step in front of every image-sourced record (§15/§17), unlike CSV's
  deterministic parse.
- OCR accuracy depends on image quality (resolution, contrast, font, skew) like any OCR system;
  `average_confidence`/`warnings` (§13) surface this to the user rather than hiding it, but do not
  improve it.

## 12. Deferred to a future phase

- A real `Text`/`Url` extractor -- explicitly out of scope for this phase (see this document's
  opening note).
- `product.process` and its CSV-import adapter -- **done as of Phase 3E**: see
  [`product-processing.md`](product-processing.md), which follows exactly the mechanical pattern
  predicted here (`user-import.md` §1.1 and this document's §5). As anticipated, the same
  `IOcrProvider`/`ImageExtractor` pipeline (§13-§14) now also serves Image+Products, through its
  own mapping adapter (mirroring §15), with zero changes to `ImageExtractor` itself.
  `category.process` remains undone -- still deferred, same pattern.
- An `ExtractorRegistry` mirroring `HandlerRegistry`, once a second concrete `IInputExtractor`
  needing dynamic dispatch exists to justify one (today `InputProcessingService` holds `CsvExtractor`
  and `ImageExtractor` -- unused and injected, respectively -- directly, no registry).
- Migrating `/api/v1/workloads/user-imports`/`import_users_from_csv` onto the
  `CsvExtractor`/`StructuredRecord` pipeline, if and when a second CSV-consuming target makes the
  shared intermediate representation actually pay for itself (§5).
- A cloud/vision-API `IOcrProvider` implementation as an alternative to the local Tesseract CLI
  (§16) -- the interface already supports this; no such implementation exists or is called by
  anything in this codebase.
- Server-side preview-session storage (a `preview_id` the client references instead of
  round-tripping full record data to `confirm`) -- deliberately not built this phase; see §15's
  rationale for why the current stateless design is preferred for now.

---

# Phase 3D-1: Image/Screenshot input pipeline

The intended final pipeline for an image-sourced record:

```
Image/Screenshot
  |
Validation (infra::validate_image -- size + real file-signature bytes)
  |
IOcrProvider::recognize()  (TesseractCliOcrProvider -- a real, local OCR engine)
  |
ImageExtractor  (table-reconstruction heuristic over OCR word boxes -> StructuredRecord)
  |
map_structured_records_to_users  (target-specific field mapping + domain::validate_and_normalize_user_record)
  |
PreviewResult  (POST /api/v1/process/preview -- creates nothing)
  |
  ...human reviews the table, clicks "Process Valid Records"...
  |
ConfirmRequest -> InputProcessingService::confirm()  (POST /api/v1/process/confirm)
  |
WorkloadService::create_workload()
  |
Jobs -> PriorityScheduler -> LocalWorkerPool -> JobExecutor -> PostgreSQL
```

**Extraction** (turning pixels into generic `StructuredRecord`s) and **processing** (turning
records into `Workload`/`Job` rows) are deliberately two different operations reachable through two
different endpoints (`/preview` vs. `/confirm`) -- see §15 and §17. Nothing before the human's
explicit confirm click ever writes to PostgreSQL.

## 13. Image input model and validation

`domain::InputPayload` (§2, Phase 3C) is reused unchanged: `{source_type: Image|Screenshot,
content: <raw image bytes>}`. No new domain type was needed -- the phase 3C doc comment on
`InputPayload` already anticipated this ("a future `Image`/`Screenshot` implementation would store
raw image bytes here the same way").

**Why `Image` and `Screenshot` share one extractor.** To an OCR engine, a screenshot is just an
image -- there is no pixel-level distinction between "a photo of a printed table" and "a screen
capture of a table rendered in a browser" that would justify two separate extraction code paths.
`extractors::ImageExtractor::source_type()` reports `Image` as its nominal identity (`
IInputExtractor` has room for exactly one), but `extract()` explicitly accepts both
`InputSourceType::Image` and `::Screenshot` payloads (`engine/src/extractors/image_extractor.cpp`).
`InputProcessingService::preview()`'s `is_image_source()` helper mirrors this. If a future source
type ever needs genuinely different handling (e.g. a PDF page image with known DPI metadata), it
gets its own extractor at that point -- this is not a limitation baked into the architecture, just
the honest state of what two visually-identical input shapes need today.

**Image validation** (`infra::validate_image`, `engine/include/flowforge/infra/image_format.hpp`)
runs before anything else touches the bytes:

- **Size**: rejected above `kMaxImageBytes` (6 MiB) -- below the server's coarse 8 MiB
  request-body cap (`apps/server/src/http/app.cpp`), so this specific, clear message is what a
  caller actually sees for an oversized image, not httplib's generic body-too-large behavior.
- **Format, by real file-signature bytes, never by trusted MIME type or extension.** PNG (`89 50
  4E 47 0D 0A 1A 0A`), JPEG (`FF D8 FF`), and WebP (`RIFF....WEBP`, checking both fixed spans around
  the variable chunk-size field) are the only three formats accepted -- the three Tesseract's
  bundled Leptonica image-decoding library reliably reads. See §16, "Security: image validation"
  for why signature-sniffing (not the client-supplied `Content-Type` or filename) is the only
  input this check trusts.
- An empty upload, or one whose leading bytes match none of the three signatures (including a
  well-formed image in an unsupported format, e.g. GIF, BMP, TIFF), is rejected with a single
  generic message ("unsupported or corrupt image -- only PNG, JPEG, and WebP are supported") --
  deliberately not distinguishing "wrong format" from "corrupt bytes claiming to be one of the
  three", since neither case should leak parser-internals detail to a caller.

`extractors::ImageExtractor::extract()` runs this same check again (defense in depth: `IOcrProvider`
is a general-purpose interface a future caller could invoke directly, not only through
`ImageExtractor`) before ever invoking the OCR provider -- an invalid image never reaches, and
therefore never wastes CPU/process-spawn cost on, the OCR engine.

## 14. Extraction: `IOcrProvider`, `TesseractCliOcrProvider`, and table reconstruction

**`engine::IOcrProvider`** (`engine/include/flowforge/engine/ocr_provider.hpp`) is the swappable
seam between FlowForge and whatever technology actually reads text out of pixels -- one method,
`recognize(image_bytes) -> Result<OcrResult>`, where `OcrResult` is a flat list of `OcrWord`
(recognized text, a bounding box, a confidence score, and the OCR engine's own
block/paragraph/line grouping). No FlowForge type outside this file and its implementations knows a
concrete OCR vendor exists -- `extractors::ImageExtractor` depends only on this interface, so
swapping providers (a different local engine, a cloud vision API) is a new `IOcrProvider`
implementation, never a change to `ImageExtractor`, `StructuredRecord`, or anything upstream.

**`providers::TesseractCliOcrProvider`** (`engine/include/flowforge/providers/
tesseract_ocr_provider.hpp`) is the one concrete implementation this phase ships -- see §16 for why
it shells out to the Tesseract CLI rather than linking `libtesseract` directly. It writes the
(already-validated) image bytes to a process-unique temporary file, runs `tesseract <file>
<output_base> -l eng --psm 6 tsv` (word-level bounding boxes + per-word confidence -- never just
plain text, since `ImageExtractor` needs the boxes to reconstruct table structure), reads back the
resulting `.tsv` file, and deletes both temporary files before returning, on every return path
(success, OCR failure, or timeout) -- see §16, "Security: temporary file handling".
`TesseractCliOcrProvider::discover_executable()` probes `FLOWFORGE_TESSERACT_PATH`, then `PATH`,
then the standard per-platform install locations, actually running `--version` against each
candidate (not just checking the file exists) -- `apps/server/src/http/app.cpp` calls this once at
startup and wires a real `ImageExtractor` in only if it succeeds, so a deployment without Tesseract
installed gets a clean, always-"not supported" `preview()` rather than a crash on first use.

**`extractors::ImageExtractor`** (`engine/include/flowforge/extractors/image_extractor.hpp`)
implements `engine::IInputExtractor` exactly like `CsvExtractor` (§3): `extract()` validates,
invokes the injected `IOcrProvider`, and turns the result into `domain::StructuredRecord`s. Its
job past that point is genuinely new: **table reconstruction**, a real (if heuristic) generic
layout algorithm, not business-domain aware --

1. **Group words into lines** using the OCR engine's own `block_num`/`par_num`/`line_num`
   hierarchy (preserving first-seen order, i.e. natural reading order -- never re-sorted by `top`
   alone, which would misorder a genuinely multi-column page layout).
2. **Split each line into columns** by sorting its words left-to-right and starting a new column
   whenever the horizontal gap between two consecutive words exceeds `2x` the image's mean
   recognized word height -- a threshold scaled to the text's own size (so it holds up across
   image resolutions) rather than a fixed pixel count. Validated against this phase's own fixture
   image (`engine/tests/fixtures/user_table.png`): real inter-word gaps there are ~10-15px, real
   column gaps are ~110-290px, and the computed threshold sits at ~50px -- comfortably separating
   the two. See `engine/src/extractors/image_extractor.cpp`'s `split_into_columns` for the exact
   logic, and §11 for this heuristic's acknowledged limits.
3. **The first line becomes the header** -- its (trimmed) cell text becomes each subsequent
   record's field names, verbatim, exactly like `CsvExtractor`'s header row (§3). No "name"/"email"
   semantics exist at this layer -- that is §15's job, one step downstream.
4. **Structural checks only, mirroring `CsvExtractor`**: fewer than 2 lines, or every line
   splitting into only 1 column, is "no table structure was detected" (`ErrorCode::Validation`) --
   there is nothing to extract records from. Zero OCR words at all is the more specific "no text
   was detected in the image". A data row whose column count doesn't match the header's is a
   per-row `RejectedRecord` (bounded the same way `CsvExtractor`'s are, §3), never silently dropped
   or force-fit. More than 200 detected data rows (`kMaxRecords`, deliberately smaller than CSV's
   1000 -- a real business-table screenshot is realistically dozens of rows, and OCR is far more
   compute-expensive per record than CSV tokenization) rejects the whole image, mirroring
   `CsvExtractor`'s whole-file row-count bound.

**Confidence and warnings** are generic `domain::ExtractionResult` fields (§2, extended this
phase) any extractor may optionally populate -- `CsvExtractor` leaves both unset; `ImageExtractor`
sets `average_confidence` to the mean of every OCR'd word's non-negative confidence score, and adds
a warning when that average falls below 70% ("extracted data may contain errors; please review
before confirming") -- surfaced, not hidden (§11).

## 15. User mapping and the Preview API

**`services::map_structured_records_to_users`** (`engine/include/flowforge/services/
user_mapping.hpp`) is the target-specific adapter step between generic extraction and the Users
target -- mirrors §5's already-documented mechanical pattern for exactly this purpose. It locates
each record's name/email/phone columns by a **case-insensitive, trimmed, small-alias-set** match
(`"Email Address"`, `"email"`, `"e-mail"` all map to email; similarly for name/phone) --
deliberately looser than `parse_user_import_csv`'s exact-lowercase CSV header match (§ of
`user-import.md`), because an image's header text was read by OCR, not typed as a machine-oriented
column name by whoever authored the CSV. A record missing a recognizable name/email column, or one
whose values fail the exact same `domain::validate_and_normalize_user_record` CSV import and
`handlers::UserProcessHandler` already enforce, is reported as a rejection -- never silently
dropped, never silently accepted.

**`POST /api/v1/process/preview`** (multipart, identical fields to `/api/v1/process`: `source`,
`target`, `file`) runs extraction (§14) and mapping (above) and returns the result --
**it creates nothing**: no `Workload` row, no `Job` row, no database write of any kind. Verified
directly (not just asserted): `apps/server/tests/process_routes_test.cpp`'s
`PreviewOfARealFixtureImageExtractsRecordsAndCreatesNoWorkload` uploads this phase's real fixture
image, asserts a real extraction result, and then asserts `GET /api/v1/workloads` is still empty.
`InputProcessingService::preview()` returns `ErrorCode::Validation` immediately for `csv`+any
target, any target other than `users`, or when no image extractor was wired in at startup (no
Tesseract found) -- `Image`/`Screenshot`+`Users` is the only combination `preview()` ever accepts.

**Why the reported rejection index is the *original* row position, not extraction's own
(shorter) index space.** `ImageExtractor::extract()` already filters out structurally-invalid rows
into its own `rejected_records`, so `extracted->records` (what mapping actually sees) is a
*shorter* list than the original table, with gaps where structural rejections were. If mapping
then rejects, say, the 2nd record in that shorter list, reporting "record 2" would be actively
misleading if row 1 of the *original* table had already been dropped for a structural reason --
the user's real row 3 would be reported as "2". `InputProcessingService::preview()` reconstructs
the true original position of each structurally-valid record (by walking `1..total_records` and
skipping whatever `extracted->rejected_records` already claimed) before merging extraction-level
and mapping-level rejections into one list, each entry pointing at its real row in the image --
see `engine/tests/services/input_processing_service_test.cpp`'s
`PreviewRenumbersMappingRejectionsPastStructuralRejections` for the test proving this holds even
with a structural rejection ahead of a mapping rejection in the same image.

## 16. Why the Tesseract CLI, not libtesseract -- and the resulting security model

Before adding any OCR dependency, this phase checked: repository/build conventions, supported
platforms, licensing, maintenance status, and whether the library performs OCR itself or only
image decoding (per this phase's own brief). **Tesseract** (Apache 2.0, actively maintained, the
de facto standard open-source OCR engine, genuinely performing OCR rather than just decoding) was
the clear choice -- the open question was *how* to integrate it.

**This development environment has no MSYS2/pacman and no working `pkg-config`-based C++ library
discovery for a toolchain-matched Tesseract build**, and a prebuilt Windows Tesseract distribution
ships MSVC-ABI libraries incompatible with this project's MinGW/Clang toolchain (the same class of
cross-compiler C++ ABI risk already documented for other native dependencies in this codebase).
Linking `libtesseract`'s C++ API directly would have meant either an unverified, likely-broken
Windows build, or a from-source Leptonica+Tesseract compile large and slow enough to risk leaving
the build in an unverified state within this phase -- exactly the situation this phase's own brief
says to stop at the provider boundary for.

**Shelling out to the `tesseract` CLI as a subprocess sidesteps all of this entirely**: Tesseract's
CLI is a stable, documented, platform-independent interface (the same binary distributed via
`apt-get install tesseract-ocr` on Linux CI and the UB-Mannheim/tesseract-ocr Windows installers)
that requires zero C++-level linking, so it is immune to compiler/ABI mismatches by construction.
This was verified, not assumed: this phase actually installed Tesseract (`winget install
UB-Mannheim.TesseractOCR`) on the development machine and ran the full
image -> OCR -> table-reconstruction -> mapping -> preview -> confirm -> real-PostgreSQL-persisted
workload pipeline against a real fixture image before writing this document (see §19).

This choice has a direct, positive security consequence: **`infra::run_subprocess`
(`engine/include/flowforge/infra/subprocess.hpp`) never invokes a shell.** `executable` and each
argument are passed directly to the OS's native process-creation API (`CreateProcessA` on Windows,
`posix_spawnp` on POSIX) as a literal argv -- there is no `/bin/sh -c` or `cmd.exe /c` anywhere in
this path, so shell metacharacters in any argument have no special meaning and command injection
is not a category of bug this code can have. `run_subprocess` also enforces a hard timeout
(`TesseractCliOcrProvider`'s default: 15s), forcibly terminating the child process
(`TerminateProcess`/`SIGKILL`) if it's exceeded -- bounded processing, never an unbounded hang on a
pathological image (§12 of the original phase brief).

**Security: image validation** -- covered in full in §13; the summary is that no unvalidated byte
ever reaches the OCR engine, and the accepted-format allowlist is checked against real file
signatures, never a caller-supplied claim.

**Security: temporary file handling** -- `TesseractCliOcrProvider::recognize()` writes the input
image to `<system temp dir>/flowforge_ocr_<random UUIDv4>.<ext>` and instructs Tesseract to write
its TSV output to a sibling `..._out.tsv`; both filenames are entirely server-generated (a random
UUID plus a format extension already validated against a fixed allowlist -- §13), never derived
from caller-supplied content, so there is no path-traversal surface. A local `TempFileGuard` (RAII)
deletes both files on every return path -- success, a validation failure, an OCR failure, or a
timeout -- so a burst of failed uploads cannot accumulate temp files. No uploaded image's raw
bytes, and no OCR-extracted text, is ever written to a log line -- only structural facts (word
count, average confidence, row/column counts) are logged, mirroring `user-import.md` §7's "never
log raw uploaded data" policy for CSV.

## 17. Confirmation and why `process()` never accepts `Image`/`Screenshot`

**`POST /api/v1/process/confirm`** (JSON body: `{"target": "users", "records": [{"name", "email",
"phone"?}, ...]}`) is the only step in this phase's image pipeline that creates anything --
`InputProcessingService::confirm()` re-validates every record via the same
`domain::validate_and_normalize_user_record` (**never trusting a client-echoed record as
already-valid**, even though in the intended flow it is exactly what `/preview` just returned,
unmodified -- a defensively-designed API must not assume its own prior response was never
tampered with in transit or by a modified client) and then calls the exact same
`WorkloadService::create_workload()` every other workload-creating path in this codebase uses.
`ConfirmRequest`/its response (`ProcessResult`, reused from §4/Phase 3C) are therefore not new
concepts -- `confirm()` is `process()`'s create-a-workload step, fed by already-extracted records
instead of a fresh CSV parse.

**Why this is stateless rather than a server-side "preview session".** `/preview`'s response
already contains everything `/confirm` needs (the normalized, ready-to-submit records) -- there is
no missing information a server-side session would supply that the client doesn't already have.
Building one would mean either an in-memory session store (which doesn't survive a server restart
or work across multiple server instances behind a load balancer) or a persisted one (which is
exactly the kind of "commit unconfirmed data to a database" this phase's brief explicitly forbids).
The stateless design -- the client holds the preview result and re-submits it verbatim to confirm
-- has neither problem, at the cost of a client needing to keep the preview response around between
the two calls, which every reasonable client (including `<ProcessingUploadPanel>`, §18) does
trivially via component state.

**`InputProcessingService::process()`'s `is_supported()` check (§4, unchanged by this phase) never
accepts `Image`/`Screenshot`, even now that a real `ImageExtractor` is wired in for `preview()`.**
This is deliberate, not an oversight: the product requirement is explicit that extracted image
records must never be persisted before a human confirms them (this document's opening note), and
`process()`'s entire contract is "validate then immediately create a workload" -- there is no
confirmation step in that path for any source. Allowing `image`+`users` through `process()` would
silently bypass the confirmation requirement the moment a caller used the "wrong" endpoint.
`engine/tests/services/input_processing_service_test.cpp`'s
`DirectProcessNeverAcceptsImageEvenWithAnExtractorWired` proves this holds even with a working
image extractor injected -- the guard is `is_supported()`'s source-type check, not merely "no
extractor configured".

## 18. Frontend: image upload, extraction preview, and confirmation

`<ProcessingUploadPanel>` (`apps/dashboard/src/components/processing/processing-upload-panel.tsx`)
gained a second, real flow alongside CSV's unchanged direct upload: selecting `Image` or
`Screenshot` as the source (both now `available: true`, alongside `Users` as the only available
target -- `Products`/`Categories` and `Text`/`Url` remain visibly present but disabled, "Coming
soon", never silently-do-nothing buttons) reveals a drag-and-drop upload area with client-side file
type/size hints (mirroring the server's own PNG/JPEG/WebP + 6 MiB limits -- client-side is UX sugar
only; the server independently, authoritatively validates every byte, §13) and a selected-image
thumbnail preview via an object URL (revoked on replacement/unmount to avoid leaking blobs).

Clicking **Extract Data** calls `apiClient.previewProcess` (`POST /api/v1/process/preview`) and
shows a non-fake loading state -- a spinner plus static "Analyzing image… / Extracting rows ·
Validating records" text, never an animated progress percentage, since the underlying request is
one atomic HTTP call with no granular progress to report honestly. On success, the panel renders
the extracted-records review table (valid/invalid counts, average OCR confidence, low-confidence
warnings, a Name/Email/Phone table for valid records, and a separate "Invalid rows" list with each
row's original position and rejection reason) with **Process Valid Records** and **Cancel**
buttons -- the panel never auto-submits after extraction; a human must explicitly click through.
**Process Valid Records** calls `apiClient.confirmProcess` (`POST /api/v1/process/confirm`) with
exactly the `records` the preview response returned, and on success renders the same
"Processing started" + `<WorkloadProgressPanel>` view CSV's flow already used (§10) -- the same
downstream experience regardless of which source produced the workload. An error during either
call (extraction or confirmation) is shown inline, never silently swallowed; an error during
confirmation specifically keeps the review table visible so the user can retry or cancel without
losing the extraction they already reviewed.

`/users`, `<UserImportWizard>`, and CSV's flow through this panel are all unchanged by this phase.

## 19. Testing: real vs. fake OCR

Every test in this phase's suite that asserts something about *text actually being read from an
image* runs real Tesseract OCR against a real, committed fixture image
(`engine/tests/fixtures/user_table.png` -- a rendered four-line "Name | Email | Phone" table,
generated once for this phase) -- never a mock OCR provider standing in for the real engine's
output, and never a network service. These integration tests (`engine/tests/providers/
tesseract_ocr_provider_test.cpp`'s `OcrIntegrationTest` suite, plus
`apps/server/tests/process_routes_test.cpp`'s `PreviewOfARealFixtureImageExtractsRecordsAndCreatesNoWorkload`)
call `TesseractCliOcrProvider::discover_executable()` in `SetUp()` and `GTEST_SKIP()` -- reported
honestly by GoogleTest/CTest as "SKIPPED", never silently treated as a pass -- when no usable
Tesseract binary is found, mirroring `engine/tests/persistence/postgres/
postgres_test_support.hpp`'s existing pattern for PostgreSQL integration tests exactly. CI installs
`tesseract-ocr` via `apt-get` in every job that builds/runs the engine or server test binaries
(`.github/workflows/ci.yml`) specifically so these tests run for real there too, not just locally.

Every other test of this phase's logic -- table reconstruction (`engine/tests/extractors/
image_extractor_test.cpp`), user mapping (`engine/tests/services/user_mapping_test.cpp`), and the
preview/confirm service layer (`engine/tests/services/input_processing_service_test.cpp`) -- uses a
small, deterministic `FakeOcrProvider` test double that returns caller-constructed `OcrWord` lists
instead of running OCR, so these tests are fast, deterministic, and independent of whether
Tesseract is installed on the machine running them. This split (real-OCR integration tests, gated
and skippable; fast deterministic unit tests for everything downstream of OCR) is deliberate: it
means the *business logic* (table reconstruction, mapping, preview/confirm semantics) is fully
tested on every machine and in every CI run, while the *real OCR accuracy* claim is verified
specifically and only where a real Tesseract binary is actually available -- exactly the
distinction this phase's brief asked for ("do not use external network services inside unit
tests"; "isolate [external-infrastructure-dependent tests] as an integration test and clearly
document how it runs").

---

# Phase 3D-2: 100+ record bulk acceptance hardening

Phase 3D-1 proved the image pipeline works end to end on a 3-row fixture. This phase proves the
same, unmodified architecture holds up at the scale the product actually targets -- a real
spreadsheet/table screenshot with 100+ rows -- and hardens the one real bug that scale exposed (a
test-infrastructure timeout, not a production defect; see below).

## 20. 100+ record fixture

`engine/tests/fixtures/user_table_bulk_100.png` -- a header row ("Name Email Phone") plus 100 data
rows, rendered the same way `user_table.png` (§19) was: GDI+ (`System.Drawing`), Consolas,
`AntiAliasGridFit` text rendering, deterministic synthetic data (a fixed 20-name x 20-surname pool
cycling by row index; `person{n}@example.com`; `0300-{n:D7}` phone numbers) -- no randomness, so
regenerating it produces byte-identical output. `1160x3292px`, `~488 KB`, comfortably under
`kMaxImageBytes` (6 MiB, §13).

Column x-positions (name/email/phone) were widened from the 3-row fixture's during this phase
after real OCR testing showed the original margins weren't safe at scale -- see §21's "table
reconstruction" finding for why, and why that was a *fixture* fix, not a code fix. The email
field deliberately avoids zero-padding (`person7@example.com`, not `person007@...`) -- see §21.

## 21. OCR/extraction quality at 100+ rows -- findings and what was (and wasn't) fixed

Running real Tesseract OCR (`--psm 6 tsv`) against the fixture and measuring the actual output
(`engine/tests/providers/tesseract_ocr_provider_test.cpp`'s
`ReconstructsAllHundredRowsFromTheBulkFixtureWithoutLosingAny`, and the full HTTP-level
`ProcessRoutesBulkPostgresTest.HundredRecordImageFlowReconcilesAgainstRealPostgres`):

| Metric | Result |
|---|---|
| OCR words detected | ~400 (4 per row: name may be 1-2 words merged into one column, email, phone, plus 3 header words) |
| Reconstructed table rows (`ExtractionResult::total_records`) | **100 -- exact, zero rows lost or merged** |
| Structured records (`ExtractionResult::records`) | 100 (0 structural/column-count rejections on the final fixture) |
| Valid records (post-mapping, `PreviewResult::records`) | 97 |
| Invalid records (`PreviewResult::rejected_records`) | 3, all `"'email' must be a valid email address"` |
| Extraction confidence (`average_confidence`) | ~77% |

**No row was ever silently discarded.** `total_records` is always exactly 100 -- every one of the
3 invalid rows is present in `rejected_records` with its real original row index and reason (§15's
renumbering logic, unit-tested independently, holds at this scale too), never merely absent from
the response.

**What caused the 3 rejections, and why nothing in the extraction/validation code was changed for
it.** Investigating the actual TSV output showed all 3 were a real Tesseract text-recognition
artifact: OCR occasionally reads two consecutive `0`-adjacent-to-`@`-type character sequences as a
doubled `@` (e.g. `person2@example.com` mis-OCR'd as containing an extra `@`), which
`domain::validate_and_normalize_user_record`'s "exactly one `@`" structural check correctly flags
as invalid -- exactly the human-review safety net this architecture is designed around (§15's
"why the product puts a mandatory preview/confirm step in front of every image-sourced record").
This is expected OCR imperfection, not an extraction bug: **the row itself was never lost** (it
shows up in `rejected_records`, correctly attributed), and forcibly "fixing" an OCR misread by
guessing the intended text would be exactly the kind of fake/corrected data this project's brief
prohibits. Nothing in `ImageExtractor`, `TesseractCliOcrProvider`, or `map_structured_records_to_users`
was changed to chase this number down further.

**What *was* found and fixed -- in the fixture, not the algorithm.** An earlier iteration of this
fixture (narrower name/email column margins, names with a digit glued directly onto the surname)
produced far worse results: OCR-rendered text from the `Name` column bled far enough right to
merge with the `Email` column's text for any sufficiently long surname (e.g. "Williams",
"Rodriguez"), which `ImageExtractor`'s gap-based column-split heuristic (§14) correctly
reported as a structural anomaly rather than silently guessing a split -- but the practical result
was a majority of rows rejected. This was root-caused to the *fixture's rendered column spacing*
being too narrow for the longest realistic name in the test data pool, confirmed by widening the
`Email`/`Phone` column start positions (§20) and re-running: the merge disappeared entirely (0
structural rejections in the final fixture, versus the earlier iteration's dozens). This is the
kind of fixture-quality issue this phase's brief anticipated ("If the current heuristic table
reconstruction loses rows or merges columns incorrectly, fix the underlying generic extraction
logic" -- it did not; a wider real-world screenshot's own natural column spacing would not have
hit this at all, and no synthetic test image should be narrower than a realistic one). The
table-reconstruction algorithm itself (`engine/src/extractors/image_extractor.cpp`) received no
changes this phase -- its existing deterministic unit tests (`ImageExtractorTest`, unchanged)
continue to pass.

## 22. 100+ record acceptance result

Verified directly against a real PostgreSQL database (`FLOWFORGE_TEST_DATABASE_URL`), through the
real HTTP API, with no shortcut (both manually, and as the automated
`ProcessRoutesBulkPostgresTest.HundredRecordImageFlowReconcilesAgainstRealPostgres`):

- **Preview**: `total_records=100`, `valid_records=97`, `invalid_records=3`. Database `workloads`/
  `jobs` row counts identical before and after -- preview creates nothing, confirmed by direct
  count comparison, not just by absence of an error.
- **Confirm**: exactly one `Workload` row created (`type="user.process"`), exactly 97 `Job` rows
  (one per valid record, none for the 3 rejected ones), all 97 `scheduled=true`.
- **Execution**: workload reached `status="succeeded"` with `completed_items=97`, `failed_items=0`,
  `queued_items=0`, `running_items=0` -- a genuine terminal state, reached by polling
  `GET /api/v1/workloads/{id}` (never assumed from the confirm response alone). Every job's
  `job_attempts` row has `outcome="succeeded"`, `attempt_number=1`, and a non-null `worker_id` --
  proof each one actually ran through `LocalWorkerPool` -> `JobExecutor` ->
  `handlers::UserProcessHandler`, not merely got created. Zero duplicate job IDs.
- **Reconciliation** (submitted -> accepted -> queued -> running -> succeeded -> failed ->
  retrying -> dead-lettered): `97 -> 97 -> 0 -> 0 -> 97 -> 0 -> 0 -> 0`. Every number accounted
  for; nothing unexplained.

## 23. Performance observations (not a benchmark)

Measured once, on the development machine, for context -- not a performance guarantee or SLA:

| Step | Duration |
|---|---|
| Raw `tesseract` CLI on the 100-row fixture | ~3.6s |
| `POST /api/v1/process/preview` round trip (validation + OCR + reconstruction + mapping) | ~4.1-4.5s |
| `POST /api/v1/process/confirm` (97 items: create workload + 97 jobs + dispatch) | ~0.8s |
| End-to-end job execution (97 jobs, `UserProcessHandler`, `LocalWorkerPool` default worker
  count) | Sub-second past dispatch -- the workload was already `succeeded` by the time the next
  poll fired |

OCR dominates the request time, as expected (Tesseract is the only genuinely CPU-bound step in
this pipeline). No O(n²) behavior or unbounded-memory growth was observed or is expected to exist:
`ImageExtractor`'s line-grouping is an `unordered_map`-backed single pass over the OCR word list
(§14), and the confirm path builds one `WorkloadItem` per record in a single linear pass -- both
already linear in record count. No performance change was made this phase; none was justified by
what the 100-row run showed.

## 24. Security re-verification at scale

Re-confirmed against the 100-row fixture and adjacent adversarial inputs (oversized, wrong-format,
corrupt-with-a-trusted-`Content-Type`): the 6 MiB size cap, magic-byte-only format detection
(never trusting client `Content-Type`), and generic rejection messages (§13) all behave identically
at this scale as at the 3-row scale -- these checks run before OCR, independent of image content
size within the accepted range. No leftover `flowforge_ocr_*` temporary files remained after the
full bulk run (`TempFileGuard`, §16, cleaned up every one). No image bytes, extracted text, or
per-record OCR output appeared in server logs -- only aggregate counts (`total_records=100
valid_records=97 rejected_records=3`) and structural error reasons, unchanged from §16's policy.
No subprocess-invocation code was added or modified this phase (`infra::run_subprocess`, §16, is
reused as-is), so its no-shell-invocation guarantee is unchanged.

## 25. Known limitations (Phase 3D-2 additions)

- The 3 rejected rows in the committed fixture's OCR output are a property of this specific
  fixture image and this specific installed Tesseract version -- a different OCR engine version,
  font renderer, or fixture would produce a different (not necessarily zero, not necessarily
  three) count. The acceptance criterion this phase verifies is "no row silently lost, and the
  valid/invalid split is accurately reported and reconciles", not "OCR reads every character
  perfectly" -- the latter is not a property any OCR system offers.
- §21's fixture-spacing finding is a reminder, not a new safeguard: a real-world screenshot with
  an unusually narrow column layout and very long field values could still hit the same
  structural-rejection path `ImageExtractor` already has (§14) -- it will report the affected rows
  as rejected, never merge them incorrectly and silently, but a human uploading such a screenshot
  would see more rejected rows than expected. No change was made to relax the column-gap
  heuristic's strictness in exchange for guessing splits in ambiguous cases; correctness over
  best-effort guessing remains the design choice (§14/§11).
