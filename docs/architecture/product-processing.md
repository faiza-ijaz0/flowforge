# FlowForge Product Domain (Phase 3E)

This document describes the Product domain: the second real workload target FlowForge processes
records into, after Users (`user-import.md`). It complements
[`input-processing.md`](input-processing.md) (the generic, source-/target-agnostic pipeline both
targets share) and [`workload-model.md`](workload-model.md) (the generic Workload aggregate)
rather than replacing either -- read those first for the parts of this pipeline Products does not
change.

## 1. Why Products proves the architecture is genuinely domain-agnostic

Phase 3C-3D's `InputProcessingService`/`WorkloadService`/`ImageExtractor`/`CsvExtractor` were built
"for Users" in the sense that Users was the only concrete target exercising them, but were
explicitly designed not to *know* about Users (see each type's own class comment). This phase is
the first real test of that claim: Products was added by

- writing Products' own domain model, validation, mapping adapter, and job handler (§2-5), and
- adding a small, explicit, reviewed dispatch to *existing* generic services (§6) --

**without** duplicating `WorkloadService`, `InputProcessingService`, `CsvExtractor`, or
`ImageExtractor`, and without either of the two extractors gaining a single line of Products-aware
code. `extractors::CsvExtractor` -- built in Phase 3C as untested foundation, never wired into any
live endpoint -- is, as of this phase, genuinely in production use for the first time, serving
Products' CSV path unchanged.

## 2. Product domain model

`domain::NormalizedProductRecord` (`engine/include/flowforge/domain/product_record.hpp`), mirroring
`domain::NormalizedUserRecord`'s shape/rationale exactly:

| Field | Required? | Notes |
|---|---|---|
| `sku` | required | Trimmed, uppercased, `[A-Z0-9_-]+`, <= 64 chars. A machine-oriented code, not free text -- no ISO/EAN checksum (see §11). |
| `name` | required | Trimmed, <= 200 chars. |
| `price` | required | Parsed as a non-negative decimal, <= 2 decimal places, <= 10,000,000. |
| `currency` | optional, defaults to `"USD"` | Trimmed, uppercased, structural 3-letter check -- not a real ISO 4217 registry lookup (see §11). |
| `category` | optional | Trimmed, <= 100 chars, absent if blank. |
| `description` | optional | Trimmed, <= 2000 chars, absent if blank. |
| `stock_quantity` | optional, defaults to `0` | Parsed as a non-negative integer, <= 10,000,000. |

**Field choice.** `sku`/`name`/`price` are required because a product record is meaningless
without them. `currency`/`stock_quantity` default rather than reject-if-absent: a real import
source (a supplier's screenshot, a simple CSV) very often omits either when there's one obvious
default, and rejecting an otherwise-good row over a missing "USD" or "0" would be a false-negative
with no safety benefit -- unlike `email` for Users, which has no safe default. `category`/
`description` are optional free text, mirroring `phone`. No `weight`, `dimensions`, `tax_class`,
`vendor`, `barcode`, etc.: this codebase's own convention (`NormalizedUserRecord`) is to add a
field only once a concrete need exists for it, and the phase brief explicitly warned against
padding the model.

`domain::validate_and_normalize_product_record()` is the single function both the import path
(§4) and the job handler (§5) call -- "what makes a valid product" is defined exactly once, the
same guarantee `user-import.md` establishes for Users.

`domain::Product` (`product.hpp`) is the separate, persisted-row read model (id, timestamps,
`job_id` provenance) -- mirrors the `Job`/`CreateJobRequest` and `Workload`/`CreateWorkloadRequest`
split already established elsewhere in this codebase; `NormalizedProductRecord` is the
input/validation-side type, `Product` is what `GET /api/v1/products` (§8) reads back.

## 3. `product.process`: `ProductProcessHandler`

`handlers::ProductProcessHandler` registers under job type `"product.process"` into the existing
`HandlerRegistry`/`JobExecutor`/retry model -- no second executor, no second scheduler (the phase
brief's explicit constraint). It parses the same hand-rolled, JSON-library-free flat object
`UserProcessHandler` does (`sku`/`name`/`price`/... as JSON strings -- see
`infra::extract_json_string_field`, a helper this phase extracted out of `UserProcessHandler` to
share rather than duplicate a third time), re-validates via §2's function, and:

**Validation failures are always `Result` errors (non-retryable)** -- an unfixable payload can
never succeed on retry, identical to `UserProcessHandler`.

**Why `ProductProcessHandler` writes to PostgreSQL directly, unlike `UserProcessHandler`.** This
phase's brief calls for real Product persistence (a `products` table, a read API) --
`UserProcessHandler` has no equivalent requirement. `engine::ExecutionContext` remains exactly as
narrow as `IJobHandler`'s contract requires: no database connection, no repository reachable
through it (see `execution_context.hpp`'s class comment, unchanged this phase). The
`persistence::IProductRepository` `ProductProcessHandler` writes through is *this handler
instance's own* constructor-injected dependency, resolved once at application composition
(`apps/server/src/http/app.cpp`, registered alongside but separately from
`register_builtin_handlers` since it -- unlike every built-in handler -- isn't
default-constructible) -- never reached around `ExecutionContext`. A repository is a safe
dependency for a handler to hold: it's already designed for concurrent access from multiple
threads (every other service in this codebase already shares one across threads), and
`IJobHandler`'s only real constraint is "no data member mutated without synchronization", which a
`shared_ptr<IProductRepository>` set once at construction satisfies trivially.

**A database write failure is retryable=true** (`ErrorCode::Database`/`Infrastructure`), unlike a
validation failure -- a transient connection/pool issue may succeed on a later attempt, mirroring
this codebase's general retry philosophy for infra-classed errors.

**Cancellation** follows `UserProcessHandler`'s exact convention: cooperative, checked once before
any real work, reported as a non-retryable `ExecutionResult::failure`.

## 4. Product persistence

`database/migrations/0014_create_products.sql`:

```sql
CREATE TABLE products (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    sku            TEXT NOT NULL,
    name           TEXT NOT NULL,
    price          NUMERIC(12, 2) NOT NULL CHECK (price >= 0),
    currency       TEXT NOT NULL,
    category       TEXT,
    description    TEXT,
    stock_quantity INTEGER NOT NULL DEFAULT 0 CHECK (stock_quantity >= 0),
    job_id         UUID REFERENCES jobs (id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT now(),
    CONSTRAINT products_sku_unique UNIQUE (sku)
);
CREATE INDEX idx_products_created_at ON products (created_at, id);  -- pagination
CREATE INDEX idx_products_job_id ON products (job_id);              -- provenance lookups
```

**`job_id`, not `workload_id`.** A product row's import provenance -- "which batch created or last
touched this row" -- is recoverable via `job_id -> jobs.workload_id` through a join, without
duplicating the workload foreign key onto this table too; `jobs` is already the source of truth
for a job's own workload association (`workload-model.md`). `ON DELETE SET NULL` mirrors migration
0013's `jobs.workload_id` choice: a product is real business data, not grouping metadata, and must
survive its originating job being deleted (nothing in this codebase deletes a job today; this is a
conservative, forward-looking choice, not a behavior this phase exercises).

**Why upsert, not insert-or-conflict.** `IProductRepository::upsert()` is `INSERT ... ON CONFLICT
(sku) DO UPDATE` -- a re-imported SKU (a supplier re-sending their catalog with updated
prices/stock) is expected, handled behavior, never surfaced as `ErrorCode::Conflict`. `id` and
`created_at` are preserved across an update; every other column, plus `job_id` and `updated_at`,
reflect the most recent write. Verified directly: `ProductProcessHandlerTest.
ReimportingTheSameSkuUpdatesTheExistingRow`, `InMemoryProductRepositoryTest.
ReimportingTheSameSkuUpdatesInPlace`, `PostgresProductRepositoryTest.
ReimportingTheSameSkuUpdatesInPlaceRatherThanConflicting`.

`persistence::IProductRepository`/`InMemoryProductRepository`/`PostgresProductRepository` mirror
`IWorkloadRepository`'s shape and conventions exactly (`repository_factory.cpp` wires the concrete
choice the same config-driven way as every other repository) -- `pqxx::*` types never leave
`postgres_product_repository.cpp`.

## 5. Product import adapter

`services::map_structured_records_to_products` (`services/product_mapping.hpp`) mirrors
`map_structured_records_to_users` exactly: case-insensitive, alias-tolerant column matching (`sku`/
`"Product Code"`/`"product_sku"` all resolve to `sku`; similarly for the other six fields) via
`domain::StructuredRecord::field_by_aliases()` -- a small helper this phase extracted out of
`user_mapping.cpp` (which had its own private copy) into `StructuredRecord` itself, since Products
needed the identical logic; `user_mapping.cpp` was refactored to use it too, with no behavior
change (its own regression tests are unchanged and still pass).

This adapter -- like `map_structured_records_to_users` -- lives outside `InputProcessingService`,
`WorkloadService`, `CsvExtractor`, and `ImageExtractor` entirely, per the phase's explicit
architectural rule.

## 6. Why `InputProcessingService` is not duplicated for Products

`InputProcessingService::preview()`/`confirm()` (Phase 3D-1) originally held
`vector<domain::NormalizedUserRecord>` directly in `PreviewResult`/`ConfirmRequest` -- correct for
one target, but a dead end for a second. This phase widened both to generic
`vector<domain::StructuredRecord>` (a flat field-name -> string map, the exact shape
`extractors::CsvExtractor`/`ImageExtractor` already produce) instead of adding a parallel
`ProductPreviewResult`/duplicating the class. Concretely:

- `preview()` selects an extractor by `(source_type, target)` (Csv -> the now-live
  `CsvExtractor`; Image/Screenshot -> the existing injected `ImageExtractor`, unchanged) and a
  mapping function by `target` (`map_structured_records_to_users` or
  `_products`), converts whichever target-specific `NormalizedXRecord` the mapper returns into a
  generic `StructuredRecord` (two small, private, one-purpose conversion functions in
  `input_processing_service.cpp` -- the *only* place this class contains the words "user" or
  "product"), and returns it. The renumbering logic that maps a mapping-level rejection back to
  its true original row position (`input-processing.md` §15) is completely unchanged -- it already
  operated on the target-agnostic `{index, reason}` shape.
- `confirm()` re-validates each generic record by calling the matching target's
  `domain::validate_and_normalize_*_record` and serializes its own job payload -- again, a small
  `switch`-shaped dispatch in the `.cpp`, never in the public API (`ConfirmRequest`'s shape,
  `preview()`/`confirm()`'s signatures, mention no domain at all).

Adding a future `Categories` target means adding one more branch to that same small dispatch plus
that target's own adapter module -- never touching `WorkloadService`, never duplicating
`InputProcessingService`.

**Why CSV+Products has no direct `process()` path.** `POST /api/v1/process`'s `is_supported()`
remains exactly `csv`+`users` (unchanged from Phase 3C) -- CSV+Products, like every
Image/Screenshot combination, only goes through `preview()`/`confirm()`. This is a deliberate
product decision, not an oversight: Users' direct CSV path predates the preview/confirm pattern
and was left as-is to avoid changing proven, shipped behavior (`input-processing.md` §17), but
every target added *after* preview/confirm existed goes through it uniformly, so a human always
reviews a product import before it is committed -- exactly the same safety property Image/
Screenshot already had for Users.

## 7. CSV product flow

```
CSV bytes -> CsvExtractor (generic, header-row-keyed StructuredRecords, unchanged from Phase 3C)
          -> map_structured_records_to_products (case-insensitive column matching, §5)
          -> PreviewResult (zero DB writes)
          -> [human clicks "Process Valid Records"]
          -> ConfirmRequest -> InputProcessingService::confirm() -> WorkloadService::create_workload()
          -> one Job per valid record (job_type "product.process")
          -> PriorityScheduler -> LocalWorkerPool -> JobExecutor -> ProductProcessHandler -> PostgreSQL
```

`POST /api/v1/process/preview` with `source=csv&target=products`; `POST /api/v1/process/confirm`
with `{"target":"products","records":[...]}` -- both existing Phase 3D-1 endpoints, unchanged in
shape, now also accepting `target=products`.

## 8. Image/screenshot product flow

Identical to §7 except the extractor is `ImageExtractor` (unchanged, see `input-processing.md`
§14) instead of `CsvExtractor` -- real Tesseract OCR, the same table-reconstruction heuristic, the
same generic `StructuredRecord` output. `ImageExtractor` has no product-specific code whatsoever;
only `map_structured_records_to_products` (§5), downstream of it, understands `sku`/`price`/etc.

## 9. Products API

`GET /api/v1/products?limit=&offset=` (`apps/server/src/http/routes/product_routes.cpp`) -- a
thin, read-only HTTP <-> `IProductRepository` translation mirroring `GET /api/v1/workloads`'s
pagination convention exactly (`limit` capped at 200, same as that endpoint's cap on `jobs`).
Deliberately no `POST /api/v1/products`: every product write is "confirm an import" (§6-8), never
a direct create through this router -- creating one outside the import pipeline would bypass
`validate_and_normalize_product_record` and the real-job-execution provenance (`job_id`) every
other row has.

No search/filter/sort: the phase brief asked to implement these "only where justified", and no
existing FlowForge list endpoint (`workloads`, `jobs`) has them either -- there is no established
convention to extend, and a 25-row dashboard table doesn't yet need one. `docs/development/
getting-started.md`-style guidance: add one when a concrete need (not speculative completeness)
arises.

## 10. Dashboard

`/products` (`apps/dashboard/src/app/products/page.tsx`) -- a client-rendered, paginated product
table (loading/empty/error states, "Open Processing Center" entry point) reading real
`GET /api/v1/products` data, no hardcoded rows. `<ProcessingUploadPanel>` (`/processing`) enables
`Products` as a target (CSV/Image/Screenshot, matching Users' available sources) with a
target-keyed preview-table column map (`SKU`/`Name`/`Price`/`Currency`/`Category`/`Stock` for
Products vs. `Name`/`Email`/`Phone` for Users) -- the same generic `preview -> review table ->
explicit confirm` flow Users' image path already used, since `PreviewResponse.records` is now a
generic field map (§6) the panel renders based on `target`, not a Users-specific type.

## 11. Known limitations

- SKU validation is structural (uppercase alphanumeric + `-`/`_`), not a real product-catalog
  standard -- no EAN/UPC checksum, no vendor-prefix convention.
- Currency validation is a 3-letter structural check, not a real ISO 4217 registry lookup --
  mirrors `user-import.md`'s email validation being structural, not RFC-5322-complete.
- `price` is a C++ `double` end to end (bounded to 2 decimal places on input, formatted with
  `%.2f` on output, stored as `NUMERIC(12,2)`) rather than a dedicated fixed-point/decimal type --
  a deliberate, documented simplification (mirrors this codebase's existing "no unnecessary
  dependencies" bar) rather than an oversight; a domain requiring cent-exact arithmetic across
  many currencies would need a real money type.
- No `DELETE`/`PATCH` product endpoint -- every product row's lifecycle today is "created or
  updated by an import job", matching the phase's scope (bulk processing, not full CRUD).

## 12. Deferred to a future phase

- A `Categories` target, following the exact mechanical pattern this document and
  `input-processing.md` §5 both lay out (its own domain/validation module, its own mapping
  adapter, one more branch in `InputProcessingService`'s small dispatch).
- Image/screenshot extraction for Categories, once Categories itself exists -- the
  `ImageExtractor`/`CsvExtractor` pipeline already supports any target's mapping adapter without
  change.
- Product search/filter/sort on `GET /api/v1/products`, if a concrete dashboard need emerges (§9).
