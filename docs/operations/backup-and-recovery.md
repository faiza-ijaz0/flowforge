# Backup and recovery

FlowForge's only durable state is PostgreSQL. This document covers what to back up, how, and what
FlowForge does **not** provide today — read the last section before assuming more than is actually
implemented.

## What needs backing up

Everything that matters is in PostgreSQL:

| Table | Contents | Loss impact if not backed up |
|---|---|---|
| `jobs`, `job_attempts` | Every job ever created, its status, and its full execution attempt history | Complete loss of job/execution history; in-flight work is lost |
| `workloads` | Workload metadata (type, total item count, timestamps) | Loss of the grouping between a batch import and its jobs |
| `products`, `categories`, `users` | Persisted domain records (Phase 3H added `users`) | Loss of imported business data |
| `workers` | Registered local worker rows | Cosmetic — re-created on next server start |
| `queues`, `workflows`, `workflow_steps`, `workflow_step_dependencies`, `audit_logs` | Schema exists; workflows are read-only in this phase (no create path) | Low impact today |
| `schema_migrations` | Which migrations have been applied | Critical for `scripts/db-migrate.sh`/`.ps1` to behave correctly — never edit by hand |

Nothing else is durable. Uploaded CSV/image files are processed in memory and never written to
disk — see "Uploaded files" below.

## Database backup

Standard PostgreSQL tooling — FlowForge adds nothing custom on top:

```bash
# Full logical backup (recommended for FlowForge's data size)
pg_dump "postgres://flowforge:flowforge@localhost:5432/flowforge" \
  --format=custom --file=flowforge-$(date +%Y%m%d-%H%M%S).dump

# Or plain SQL, if you want something diffable/greppable
pg_dump "postgres://flowforge:flowforge@localhost:5432/flowforge" > flowforge-backup.sql
```

For a managed PostgreSQL provider (RDS, Cloud SQL, etc.), prefer the provider's native
snapshot/point-in-time-recovery feature over `pg_dump` for anything beyond local development — this
document does not assume or configure any specific provider.

**Recommended cadence**: this is an operator decision based on your actual write volume and
tolerance for data loss — FlowForge does not prescribe one. A daily `pg_dump` plus your database
provider's transaction-log-based point-in-time recovery (if available) covers most deployments
without custom tooling.

## Database restore

```bash
# From a custom-format dump
pg_restore --clean --if-exists -d "postgres://flowforge:flowforge@localhost:5432/flowforge" flowforge-20260101-120000.dump

# From a plain SQL dump
psql "postgres://flowforge:flowforge@localhost:5432/flowforge" < flowforge-backup.sql
```

After restoring, verify `schema_migrations` matches what `database/migrations/` expects — run
`scripts/db-migrate.sh` (or `.ps1` on Windows) again; it is idempotent and will apply anything
missing, skip anything already present, and now (Phase 3H) correctly fails loudly rather than
silently if a migration step errors (see `docs/architecture/phase-3h-production-readiness.md` §9
for the bug this fixed).

## Migration strategy

- Migrations are plain, numbered SQL files in `database/migrations/`, applied in order by
  `scripts/db-migrate.sh` (Linux/macOS/CI) or `scripts/db-migrate.ps1` (Windows) — both apply only
  not-yet-applied migrations, tracked in `schema_migrations`.
- **Never edit an already-applied migration file.** If a schema change is needed, add a new
  numbered migration — this is how every schema change in this project's history has been made
  (0001 through 0016 as of Phase 3H), and it is what keeps `schema_migrations` a trustworthy record
  of what a given database has actually had applied to it.
- Verified this phase (Phase 3H): a fresh database (migrations 0001–0016 applied from zero) and an
  upgrade path (a database pre-migrated through 0015, then brought to 0016) both produce the exact
  expected schema — see the production-readiness report §9 for the exact verification steps.

### Rollback limitations

**FlowForge has no down-migrations.** There is no `scripts/db-rollback.sh` and no tooling to
reverse a migration. If a migration needs to be undone:

1. If nothing has written data depending on the new schema yet, write a new migration that reverses
   the change (e.g. `DROP TABLE`, `DROP COLUMN`) — never edit history.
2. If data already depends on the new schema, restoring from a pre-migration backup is the only
   built-in recovery path. Plan backups accordingly before applying a migration to a database with
   data you cannot afford to lose.

## Persistent workload/job data

Workload and job state lives entirely in PostgreSQL (`workloads`, `jobs`, `job_attempts`) — there
is no separate queue technology (no Redis, no message broker) holding in-flight state that a
database backup would miss. A database backup captures the complete state of every job, including
ones mid-retry (`status = 'retrying'`) — on restore, `RetryDispatcher` will pick them back up
according to their stored backoff schedule once the server restarts.

**What a backup does *not* capture**: jobs that were `queued`/`running` in the in-process
`PriorityScheduler`/`LocalWorkerPool` dispatch queues at the moment of a crash, but had not yet had
their status persisted back to PostgreSQL. This is a narrow window (typically milliseconds) inherent
to any at-least-once, non-transactional in-memory dispatch queue — FlowForge does not currently
persist queue state itself, only job *status*. A job caught in this window on a crash will remain
in its last-persisted status (`queued`, most likely) and will not automatically resume — an operator
would need to notice and re-submit it. This is a known, documented limitation, not a silent one.

## Uploaded temporary files and OCR processing

CSV and image uploads (`POST /api/v1/process`, `/preview`, `/confirm`,
`/api/v1/workloads/user-imports`) are received into memory (bounded by the payload/upload-size caps
— see `docs/architecture/phase-3h-production-readiness.md` §13) and processed there.
**Nothing is written to disk** except a transient temp file some OCR providers require internally
(see `providers::TesseractCliOcrProvider` — it invokes the `tesseract` CLI binary, which requires a
file path, not a byte stream); that temp file is created and deleted within the single
preview/confirm request's lifetime and is never a durable artifact. There is therefore nothing to
back up related to uploads — the only durable output of a successful import is the rows it wrote to
`products`/`categories`/`users` and the `jobs`/`job_attempts` rows recording how it got there.

## What FlowForge does NOT currently provide

Stated explicitly, per this phase's brief:

- No automated backup scheduling or retention policy.
- No cloud backup integration (S3, GCS, etc.) of any kind.
- No point-in-time recovery tooling beyond what PostgreSQL/your provider already offers natively.
- No down-migrations / schema rollback tooling.
- No backup of in-flight (not-yet-persisted) scheduler/worker-pool queue state.
- No encryption-at-rest configuration beyond whatever your PostgreSQL deployment provides itself.

Operators are expected to supply their own backup scheduling (cron + `pg_dump`, or their managed
database provider's snapshot feature) — FlowForge intentionally does not reimplement PostgreSQL
operational tooling.
