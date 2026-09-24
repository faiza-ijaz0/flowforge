# FlowForge Category Domain (Phase 3F)

This document describes the Category domain: the third workload target FlowForge processes bulk
records into, after Users ([`user-import.md`](user-import.md)) and Products
([`product-processing.md`](product-processing.md)). It complements
[`input-processing.md`](input-processing.md) (the generic, source-/target-agnostic pipeline every
target shares) rather than replacing it -- read that document, and product-processing.md, first.

Categories is the third proof of the same architectural claim Products established: a new
workload target is added by writing that target's own domain model, mapping adapter, and job
handler, plus a small, explicit dispatch inside `InputProcessingService`'s `.cpp` -- never by
duplicating `WorkloadService`, `InputProcessingService`, `CsvExtractor`, or `ImageExtractor`. This
phase adds no new architectural mechanism; every section below names the existing mechanism it
reuses.

## 1. Category domain model

`domain::NormalizedCategoryRecord` (`engine/include/flowforge/domain/category_record.hpp`):

| Field | Required? | Notes |
|---|---|---|
| `name` | required | Trimmed, <= 200 chars. |
| `slug` | derived if absent | See §2. Always present in the *output*, even though it's not a required *input* field. |
| `description` | optional | Trimmed, <= 2000 chars, absent if blank. |
| `parent_slug` | optional | See §4. A bare reference to another category's `slug`, normalized the same way. |

**Why only `name` is required.** `sku`/`price` have no sensible default for Products, but a
category's machine identifier (`slug`) *does* have one: derive it from the name. Requiring a
separate `slug` column for every row would reject perfectly good "just a name" imports (a
one-column CSV, a simple OCR'd list) for no safety benefit -- the same reasoning
product-processing.md gives for defaulting `currency`/`stock_quantity`, applied here to an even
more central field.

**Why no category tree beyond one `parent_slug` reference.** The brief is explicit: "reliable
bulk category ingestion, not a full CMS." No child-list, materialized path, depth counter, or
`ltree`-style structure -- a single optional parent reference is the minimum shape that supports
hierarchy at all, and §4 shows it's enough to make cycles impossible by construction plus one
cheap runtime check.

## 2. Slug semantics

`domain::slugify(std::string_view) -> std::string` (`category_record.{hpp,cpp}`) is the single,
deterministic function that turns arbitrary text into a slug: lowercase every ASCII letter,
collapse any run of characters that are neither letters nor digits into a single `-`, and never
emit a leading or trailing `-`.

```
"  Electronics & Gadgets  "  ->  "electronics-gadgets"
"Home   ---   Kitchen"       ->  "home-kitchen"
"already-a-slug"             ->  "already-a-slug"   (idempotent)
"---"                         ->  ""                 (no letters/digits -- invalid)
```

**One function, two call sites, same rules.** If a record's `slug` field is present and
non-blank, `validate_and_normalize_category_record()` calls `slugify()` on *that* value; otherwise
it calls `slugify()` on the (already-validated, trimmed) `name`. An explicitly-provided slug is
not held to a stricter "reject on odd characters" rule than a derived one -- both go through the
same normalization, so `"Home Electronics!"` in a `slug` column becomes `home-electronics` rather
than being rejected for containing punctuation. This is one code path, not two sets of rules to
keep in sync.

**When slugification fails.** `slugify()` returns an empty string only when its input has no
letters or digits at all (e.g. `"---"`, `"###"`, whitespace-only). `validate_and_normalize_category_record()`
treats that as `ErrorCode::Validation` -- "could not derive a valid slug" -- never silently
accepting an empty identifier. This is also why the 100+ fixture's deliberately-invalid rows use a
punctuation-only name (§9) rather than a blank one: it exercises this exact rejection path, not
the separate "name must not be blank" one.

## 3. Duplicate semantics

Identical to Products' philosophy (product-processing.md, "Why upsert, not insert-or-conflict"):
`ICategoryRepository::upsert()` is `INSERT ... ON CONFLICT (slug) DO UPDATE` -- a re-imported slug
(re-running the same import, or deliberately updating a category's name/description/parent) is
expected, handled behavior, never surfaced as `ErrorCode::Conflict`. `id`/`created_at` are
preserved across an update; every other column, plus `job_id`/`updated_at`, reflect the most
recent write. Verified directly: `CategoryProcessHandlerTest.ReimportingTheSameSlugUpdatesTheExistingRow`,
`InMemoryCategoryRepositoryTest.ReimportingTheSameSlugUpdatesInPlace`,
`PostgresCategoryRepositoryTest.ReimportingTheSameSlugUpdatesInPlaceRatherThanConflicting`.

## 4. Parent semantics

A category's `parent_slug` is a bare string reference to another category's `slug` -- not a
resolved pointer, not a foreign key at the database level (see §6 for why). Three rules apply, in
increasing order of where they're enforced:

**Self-parent (pure, checked at preview/confirm time).** `validate_and_normalize_category_record()`
rejects a record whose (normalized) `parent_slug` equals its own (normalized) `slug` --
`ErrorCode::Validation`, "a category cannot be its own parent". This needs no database access, so
it's visible immediately in the Processing Center's preview table, not just at job execution.

**Missing parent (impure, checked at job-execution time only).** Whether `parent_slug` refers to
a category that actually *exists* requires a database read -- `validate_and_normalize_category_record`
is pure (callable from `preview()`, which never touches the database; see input-processing.md,
"Preview"), so it cannot know. `handlers::CategoryProcessHandler` -- which, unlike
`InputProcessingService`, owns a repository -- performs this check itself, right before writing:
it calls `ICategoryRepository::find_by_slug(parent_slug)`, and if nothing comes back, returns
`ErrorCode::Validation` ("parent category '...' does not exist -- import it first, then re-import
this record"), non-retryable.

**Why this is checked at execution time, not preview/confirm time.** `preview()`/`confirm()` have
no database access beyond `WorkloadService` (input-processing.md's whole point), and jobs within
one workload have no execution-order guarantee across `PriorityScheduler`'s worker threads -- a
CSV containing both a parent row and a child row referencing it in the *same* import has no
guarantee the parent's job runs first.

**Same-submission parents (Phase 3H follow-up).** Originally the rule was "a parent must already be
persisted", and a child whose parent was in the *same* submission failed permanently whenever its
job happened to run first -- reproduced in the browser as 1 succeeded / 3 failed for a
root/child/grandchild CSV. Now `InputProcessingService::confirm()` marks a category's payload
`"parent_in_submission":"true"` when its `parent_slug` is another accepted record of the same
submission, and the handler treats *that* missing immediate parent as **retryable**: the existing
retry engine re-runs the child after backoff, by which time the parent has committed. Record order
in the file does not matter (`MultiLevelHierarchyInOneSubmissionSucceedsRegardlessOfRecordOrder`
submits child-first). Bounded by the job's retry policy (default 3 attempts): a parent that never
appears -- e.g. its own job failed -- leaves the child in `dead_letter`, and a very deep chain could
exhaust the budget with worst-case timing. For any parent **not** in the submission the original
rule stands: **it must already be a persisted row before a job referencing it can succeed.** A record with a missing parent is structurally valid (passes preview and
confirm, becomes a real job) and only fails when *that job* executes -- a deliberate, documented
tradeoff, not an oversight. See `ProcessRoutesTest.ConfirmCategoryWithMissingParentIsAcceptedAtConfirmButFailsAsAJob`
for the exact, verified behavior this implies: `confirm()` returns 201 with `valid_records: 1`,
and the workload later resolves to `status: "failed"` with the job's `last_error` naming the
missing parent.

**Importing a tree.** A tree can be imported in one submission (see above). Importing level by level
-- parents first, wait for that workload to succeed, then children referencing them by slug -- is
still the most predictable choice for very deep trees. This mirrors how a
real catalog is normally built (categories before subcategories) and needs no new mechanism --
just two ordinary imports through the same pipeline.

**Cyclic parent chains (impure, checked at job-execution time).** A cycle can only arise from an
*update*: category A exists with no parent; category B is created with `parent_slug: "a"`;
re-importing A with `parent_slug: "b"` would create `A -> B -> A`. `CategoryProcessHandler` walks
the chain starting at the record's own `parent_slug`, following `find_by_slug` up through each
ancestor's own `parent_slug`, and rejects (non-retryable) if the record's own slug reappears
anywhere in that chain -- catching cycles of any depth, not only the direct self-parent case §
already rules out earlier. The walk is bounded (64 hops) as a defensive guard against a
pathological chain, not because deep, legitimate hierarchies are expected.

**Why no database-level foreign key on `parent_slug`.** `database/migrations/0015_create_categories.sql`
leaves `parent_slug` a bare `TEXT` column, not `REFERENCES categories(slug)`. Two reasons: (1)
`InMemoryCategoryRepository` (used by every non-Postgres-gated test) has no equivalent of a
foreign-key constraint, so a real FK would only be enforced on one of the two backends this
codebase runs tests against; (2) a missing parent must be a non-retryable `ErrorCode::Validation`
failure, not a `Database`/`Infrastructure` one (see the header's "Retryability" note) --
implementing the check once, in application code, keeps that classification correct on both
backends identically, instead of correct on one and needing SQLSTATE-specific `foreign_key_violation`
translation on the other.

**Tests.** `CategoryProcessHandlerTest`: `ValidParentIsAccepted`, `MissingParentIsRejectedNonRetryably`,
`SelfParentIsRejected`, `DeeperCyclicParentChainIsRejected`, `MultiLevelParentChainIsAccepted`.
`ProcessRoutesTest`: `ConfirmCategoryWithValidPreexistingParentSucceeds`,
`ConfirmCategoryWithMissingParentIsAcceptedAtConfirmButFailsAsAJob`.

## 5. `category.process`: `CategoryProcessHandler`

`handlers::CategoryProcessHandler` registers under job type `"category.process"` into the existing
`HandlerRegistry`/`JobExecutor`/retry model -- no second executor, no second scheduler. It mirrors
`ProductProcessHandler` exactly in every respect §4 didn't already cover:

- Parses the same hand-rolled, JSON-library-free flat object (`infra::extract_json_string_field`,
  shared with `ProductProcessHandler`/`UserProcessHandler`).
- Re-validates via `domain::validate_and_normalize_category_record` -- the same function the
  import path (§7) runs at preview/confirm time, so "what makes a valid category" is defined
  exactly once.
- Gets its `ICategoryRepository` via constructor injection, resolved once at application
  composition (`apps/server/src/http/app.cpp`), registered separately from
  `register_builtin_handlers` -- never reached through `engine::ExecutionContext`, which stays
  exactly as narrow as every other phase left it. See product-processing.md, "Why
  ProductProcessHandler writes to PostgreSQL directly" for the fuller rationale, which applies
  here unchanged.
- **Retryability**: a validation failure -- malformed/missing fields, self-parent, missing
  parent, or a cyclic chain -- is always non-retryable (`std::unexpected`, never
  `ExecutionResult::failure(..., retryable=true, ...)`): none of those can be fixed by simply
  retrying the same job. A `categories` table write failure, or a database error encountered
  while walking the parent chain, is retryable=true -- a transient connection issue may succeed
  on a later attempt.
- Cancellation: cooperative, checked once before any work, non-retryable -- identical to
  `ProductProcessHandler`.

## 6. PostgreSQL persistence

`database/migrations/0015_create_categories.sql`:

```sql
CREATE TABLE categories (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name           TEXT NOT NULL,
    slug           TEXT NOT NULL,
    description    TEXT,
    parent_slug    TEXT,
    job_id         UUID REFERENCES jobs (id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT categories_slug_unique UNIQUE (slug)
);
CREATE INDEX idx_categories_created_at ON categories (created_at, id);  -- pagination
CREATE INDEX idx_categories_job_id ON categories (job_id);              -- provenance lookups
CREATE INDEX idx_categories_parent_slug ON categories (parent_slug);    -- parent-chain walks
```

`job_id`, `created_at`/`updated_at`, and the pagination/provenance indexes all mirror migration
0014's `products` table exactly -- see product-processing.md §4 for the shared rationale
(`job_id` not `workload_id`, `ON DELETE SET NULL`). The one schema difference from `products` is
`parent_slug`'s deliberate lack of a foreign key -- see §4's "Why no database-level foreign key".

`persistence::ICategoryRepository`/`InMemoryCategoryRepository`/`PostgresCategoryRepository`
mirror the equivalent Product types' shape and conventions exactly -- `pqxx::*` types never leave
`postgres_category_repository.cpp`, and every query is parameterized (`pqxx::params{...}`), never
string-built from imported data.

## 7. Category import mapping, CSV flow, and image/screenshot flow

`services::map_structured_records_to_categories` (`services/category_mapping.{hpp,cpp}`) mirrors
`map_structured_records_to_products` exactly: case-insensitive, alias-tolerant column matching
(`name`/`"Category Name"`/`"category_name"`/`"Title"` all resolve to `name`; similarly for
`slug`/`description`/`parent_slug`) via `domain::StructuredRecord::field_by_aliases()`, the same
shared helper Users' and Products' mapping already use. Only `name` triggers a "no column found"
rejection -- every other field is optional, per §1.

The CSV and image/screenshot flows are structurally identical to Products' (product-processing.md
§7-8), with `map_structured_records_to_categories`/`domain::validate_and_normalize_category_record`
in place of the Products equivalents:

```
CSV bytes -> CsvExtractor (generic, unchanged)                    Image/Screenshot -> ImageExtractor (generic, unchanged)
          -> map_structured_records_to_categories (§ above)                       -> map_structured_records_to_categories
          -> PreviewResult (zero DB writes)                                       -> PreviewResult (zero DB writes)
          -> [human reviews, clicks "Process Valid Records"]
          -> ConfirmRequest -> InputProcessingService::confirm() -> WorkloadService::create_workload()
          -> one Job per valid record (job_type "category.process")
          -> PriorityScheduler -> LocalWorkerPool -> JobExecutor -> CategoryProcessHandler -> PostgreSQL
```

`CsvExtractor`/`ImageExtractor` gained zero Categories-specific code -- the same instances already
serving Users and Products serve Categories too, through this mapping adapter alone. Like
CSV+Products, CSV+Categories has no direct `POST /api/v1/process` path (`is_supported()` remains
exactly `csv`+`users`) -- both go through `preview()`/`confirm()` only, so a human always reviews
a category import before it's committed.

## 8. `InputProcessingService`'s Categories dispatch

Exactly the same small, contained, anonymous-namespace dispatch `input_processing_service.cpp`
already has for Products (see product-processing.md §6) grew one more branch each, in
`map_for_target()`, `validate_record_for_target()`, and `preview()`'s extractor selection, plus one
new private conversion function, `category_record_to_structured()`. `PreviewResult`/`ConfirmRequest`
needed no shape change at all -- they were already generalized to `domain::StructuredRecord` in
Phase 3E specifically so a third target would need exactly this: no new type, no touched public
API, no widened `InputProcessingService` interface. `confirm()`'s target check
(`Users`/`Products`/`Categories` are the only three, matching the complete
`domain::ProcessingTarget` enum) now accepts all three declared targets -- there is no longer an
"implemented but not really" target for this codebase's own tests to exercise (see
`InputProcessingServiceTest.ConfirmSupportsEveryDeclaredProcessingTarget`).

## 9. Categories API and dashboard

`GET /api/v1/categories?limit=&offset=` (`apps/server/src/http/routes/category_routes.cpp`) --
same thin, read-only, `limit`-capped-at-200 pagination convention as `GET /api/v1/products`.
Deliberately no `POST /api/v1/categories`, same reasoning as Products: every write is "confirm an
import," never a direct create bypassing validation/provenance. No search/filter/sort, for the
same "not justified yet" reason product-processing.md §9 gives.

`/categories` (`apps/dashboard/src/app/categories/page.tsx`) mirrors `/products/page.tsx`'s
structure precisely (loading/empty/error states, pagination, "Open Processing Center" entry
point), reading real `GET /api/v1/categories` data. Its one addition beyond the Products page: a
"Parent" column showing either the parent's slug or a "Top-level" badge, giving an at-a-glance
view of each category's place in the (single-level-reference) hierarchy without rendering a full
tree widget -- consistent with §1's "not a full CMS" scope. The Processing Center
(`processing-upload-panel.tsx`) enables `Categories` as a target for both CSV and Image/Screenshot
sources, with its own preview-table column set (`Name`/`Slug`/`Parent`).

## 10. 100+ category fixture

`engine/tests/fixtures/categories_bulk_100.csv`: 100 data rows, header
`name,slug,description,parent_slug`. Every row leaves `slug` and `parent_slug` blank (slug is
derived from name; the fixture is deliberately flat -- see below) except 5 deliberately-invalid
rows (every 17th, matching the index pattern product-processing.md's fixture uses) whose `name` is
`"###"` -- punctuation only, so `slugify()` produces an empty string and the record is rejected
with "could not derive a valid slug" (§2), exercising that exact code path at scale.

**Why the bulk fixture has no `parent_slug` values.** Parent semantics (§4) already have their own
precise, deterministic, small tests (`CategoryProcessHandlerTest`'s parent-chain tests,
`ProcessRoutesTest`'s two `ConfirmCategoryWith*Parent*` tests) that assert exact behavior a
100-row fixture could only test loosely. Mixing parent references into the scale-and-reconciliation
fixture would risk making a bulk-acceptance failure ambiguous (an OCR misread vs. a genuine parent
bug) for no added coverage.

`engine/tests/fixtures/categories_bulk_100.png`: a 2-column ("Name", "Slug") table image, reusing
the exact font/rendering parameters (Consolas, `AntiAliasGridFit`, the column-geometry approach)
that produced reliable OCR for the Users/Products bulk fixtures. The same `"###"` marker is used
for the 5 deliberately-invalid rows; an earlier attempt using `"---"` was discarded after real
Tesseract testing showed thin hyphen glyphs have an unusual vertical baseline that the row-
clustering heuristic sometimes merges into a neighboring row (5 rows silently disappeared from
`total_records` rather than being correctly counted and rejected) -- `"###"`'s taller, more
letter-like strokes reconstruct reliably (verified: `total_records` == 100 exactly, no row loss).

**100+ record acceptance, verified for real (not mocked):**

- `ProcessRoutesBulkPostgresTest.HundredCategoryCsvFlowReconcilesAgainstRealPostgres` -- CSV ->
  preview (100 total, 95 valid) -> confirm (one workload, 95 jobs) -> real PostgreSQL execution ->
  full reconciliation (95/95 succeeded, 0 failed/queued/running, no duplicate job ids, every
  accepted record has a persisted `categories` row).
- `ProcessRoutesBulkPostgresTest.HundredCategoryImageFlowReconcilesAgainstRealPostgres` -- same
  flow via real Tesseract OCR, `min_valid_records` bounded at 80 (a generous floor for OCR noise,
  matching Products' identical convention) rather than an exact count.
- `OcrIntegrationTest.ReconstructsAllHundredRowsFromTheBulkCategoryFixtureWithoutLosingAny` --
  extraction-layer-only verification: `total_records == 100`, every extracted record has both
  `Name` and `Slug` fields.

## 11. A pre-existing bug this phase found and fixed

While verifying "a job created via `confirm()` can genuinely fail, and the workload must reconcile
to a `failed` terminal status" (§4's missing-parent path is the first case in this codebase where
a record passes `confirm()`-time validation but then fails at job-execution time -- Users/Products
validation failures are always caught *before* a job is ever created), the workload was observed
stuck at `status: "running"` forever, with the failed job's own status correctly showing
`"failed"` via `GET /api/v1/workloads/{id}/items`.

**Root cause**: `domain::classify_job_status_for_workload()` (`engine/src/domain/workload.cpp`)
mapped `JobStatus::Failed` to `WorkloadItemOutcome::Queued`, not `Failed`. Its doc comment's
rationale -- "a failed attempt is not yet a workload-level failure; `RetryDispatcher` may still
retry it" -- was accurate when written (Phase 2B-1, before the retry engine existed) but became
stale once `RetryDispatcher` was built (Phase 2B-4): it polls exclusively for
`JobStatus::Retrying` (`retry_dispatcher.cpp`), never `JobStatus::Failed`. A job that reaches
`Failed` via `Job::record_execution_failure()` (a handler's non-retryable error) is, in the actual
running system, permanently stuck there -- nothing ever moves it further -- so classifying it as
"still queued, might still change" caused any workload containing such a job to never reach a
terminal `Failed` status.

**Why this was never caught before.** `classify_job_status_for_workload` has exactly one caller
(`WorkloadService::with_progress`), and every prior test that polls a workload to a terminal state
only ever asserted `"succeeded"` -- Users/Products validation failures are always rejected at
`confirm()` time (never becoming a job at all), so no job in this codebase's test history had
previously reached `JobStatus::Failed` as part of a still-open workload. Categories' missing-parent
check is the first path that can only be validated with database access unavailable at
`confirm()` time (§4), making it the first to exercise this combination.

**Fix**: `classify_job_status_for_workload` now maps `Failed` (alongside the existing
`Cancelled`/`DeadLetter`) to `WorkloadItemOutcome::Failed`. Deliberately narrow: `domain::is_terminal(JobStatus)`
(used by `JobExecutor`/`PriorityScheduler`/`JobService` to guard against acting on an already-
terminal job) was left untouched -- broadening it would have a larger blast radius across
components this phase's brief did not ask to change, whereas `classify_job_status_for_workload`'s
one call site makes this fix fully contained. Regression coverage:
`ClassifyJobStatusForWorkloadTest.CancelledDeadLetterAndFailedAreAllFailed` (updated from the
stale assertion) plus the full existing `JobExecutorTest`/`PrioritySchedulerTest`/
`RetryDispatcherTest`/`RetryEngineAcceptanceTest` suites (all still passing, confirming `is_terminal`
being left alone kept every other component's behavior unchanged).

## 12. Known limitations

- Slug/parent-slug validation is structural (letters, digits, `-`), not tied to any external
  catalog/taxonomy standard.
- No `DELETE`/`PATCH` category endpoint -- a category's lifecycle today is "created or updated by
  an import job," matching this phase's bulk-ingestion scope.
- The parent-chain existence/cycle check (§4) has a small TOCTOU window under concurrent imports
  of the same category tree (check-then-upsert is not one atomic operation) -- acceptable for the
  bulk-ingestion use case this phase targets, not for a scenario demanding strict concurrent-write
  safety.
- No recursive "list all descendants of category X" query -- only a single-level `parent_slug`
  reference and the chain-walk `CategoryProcessHandler` performs internally at write time; reading
  a full subtree would require the caller to walk `GET /api/v1/categories` results itself.
