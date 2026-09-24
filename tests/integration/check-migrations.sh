#!/usr/bin/env bash
# Validates the migration runner and the migrations themselves against a
# disposable, EMPTY PostgreSQL database:
#
#   1. fresh apply  -- every database/migrations/*.sql file is applied and
#                      recorded in schema_migrations, in filename order;
#   2. schema       -- expected tables, unique constraints, foreign-key
#                      ON DELETE rules and indexes actually exist;
#   3. idempotency  -- a re-run applies nothing;
#   4. failure path -- a deliberately broken migration makes the runner exit
#                      non-zero, is NOT recorded, and leaves no partial DDL
#                      behind (single-transaction apply).
#
# Step 4 guards the exact class of bug Phase 3H found in db-migrate.ps1: a
# runner that printed "done: 16 migration(s) applied" while applying none.
#
# Usage:
#   FLOWFORGE_CHECK_DATABASE_URL=postgres://.../empty_db tests/integration/check-migrations.sh
#
# All lookups follow current_schema(), so without CREATEDB privilege the
# check can also target an empty scratch schema of an existing database:
#   ...?options=-csearch_path%3Dff_migcheck
# (CI uses a genuinely fresh database; see .github/workflows/ci.yml.)
#
# MIGRATE_CMD selects the runner under test (default: the bash runner). On
# Windows the PowerShell runner can be checked the same way:
#   MIGRATE_CMD="powershell -NoProfile -File ${REPO_DIR}/scripts/db-migrate.ps1" ...
set -euo pipefail

DATABASE_URL="${FLOWFORGE_CHECK_DATABASE_URL:-}"
if [[ -z "${DATABASE_URL}" ]]; then
  echo "error: set FLOWFORGE_CHECK_DATABASE_URL to a disposable, empty database" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MIGRATE_CMD="${MIGRATE_CMD:-bash ${REPO_DIR}/scripts/db-migrate.sh}"

q() { psql "${DATABASE_URL}" -v ON_ERROR_STOP=1 -t -A -c "$1" | tr -d '\r'; }
fail() {
  echo "FAIL: $*" >&2
  exit 1
}
pass() { echo "ok      $*"; }

# Safety: never run against a database that already has FlowForge state.
if [[ "$(q "SELECT to_regclass('schema_migrations') IS NOT NULL;")" == "t" ]]; then
  fail "target database already has schema_migrations -- point this at an empty, disposable database"
fi

run_migrate() {
  # shellcheck disable=SC2086  # MIGRATE_CMD is intentionally word-split
  FLOWFORGE_DATABASE_URL="${DATABASE_URL}" ${MIGRATE_CMD}
}

mapfile -t expected_versions < <(cd "${REPO_DIR}/database/migrations" && ls -1 *.sql | sed 's/\.sql$//' | sort)
expected_count="${#expected_versions[@]}"

# --- 1. fresh apply ---------------------------------------------------------
output="$(run_migrate | tr -d '\r')"
echo "${output}" | grep -qx "done: ${expected_count} migration(s) applied" ||
  fail "fresh apply did not report ${expected_count} applied migrations; got: $(echo "${output}" | tail -1)"
mapfile -t recorded_versions < <(q "SELECT version FROM schema_migrations ORDER BY version;")
[[ "${recorded_versions[*]}" == "${expected_versions[*]}" ]] ||
  fail "schema_migrations (${recorded_versions[*]}) does not match migration files (${expected_versions[*]})"
pass "fresh apply: ${expected_count} migrations applied and recorded"

# --- 2. schema --------------------------------------------------------------
for table in queues workers jobs job_attempts workflows workflow_steps workflow_step_dependencies \
  audit_logs workloads products categories users; do
  [[ "$(q "SELECT to_regclass('${table}') IS NOT NULL;")" == "t" ]] || fail "missing table ${table}"
done
pass "all 12 domain tables exist"

for constraint in products_sku_unique categories_slug_unique users_email_unique; do
  [[ "$(q "SELECT count(*) FROM pg_constraint WHERE conname = '${constraint}' AND contype = 'u'
                  AND connamespace = (SELECT oid FROM pg_namespace WHERE nspname = current_schema());")" == "1" ]] ||
    fail "missing unique constraint ${constraint}"
done
pass "natural-key unique constraints exist (sku, slug, email)"

# table.column -> expected ON DELETE rule
declare -A fk_rules=(
  [jobs.workload_id]="SET NULL"
  [job_attempts.job_id]="CASCADE"
  [job_attempts.worker_id]="SET NULL"
  [workflow_steps.workflow_id]="CASCADE"
  [workflow_steps.job_id]="CASCADE"
  [products.job_id]="SET NULL"
  [categories.job_id]="SET NULL"
  [users.job_id]="SET NULL"
)
for key in "${!fk_rules[@]}"; do
  table="${key%%.*}"
  column="${key#*.}"
  rule="$(q "SELECT rc.delete_rule FROM information_schema.referential_constraints rc
             JOIN information_schema.key_column_usage k
               ON k.constraint_name = rc.constraint_name AND k.constraint_schema = rc.constraint_schema
             WHERE k.table_schema = current_schema() AND k.table_name = '${table}' AND k.column_name = '${column}';")"
  [[ "${rule}" == "${fk_rules[${key}]}" ]] || fail "${key}: expected ON DELETE ${fk_rules[${key}]}, got '${rule}'"
done
pass "foreign-key ON DELETE rules match (${#fk_rules[@]} checked)"

for index in idx_jobs_queue_status_priority idx_jobs_status idx_jobs_workload_id idx_job_attempts_job_id \
  idx_products_created_at idx_products_job_id idx_categories_created_at idx_categories_job_id \
  idx_categories_parent_slug idx_users_created_at idx_users_job_id; do
  [[ "$(q "SELECT to_regclass('${index}') IS NOT NULL;")" == "t" ]] || fail "missing index ${index}"
done
pass "expected indexes exist"

# --- 3. idempotency ---------------------------------------------------------
output="$(run_migrate | tr -d '\r')"
echo "${output}" | grep -qx "done: 0 migration(s) applied" ||
  fail "re-run was not a no-op; got: $(echo "${output}" | tail -1)"
pass "re-run applies 0 migrations"

# --- 4. failure path ----------------------------------------------------------
broken_dir="$(mktemp -d)"
trap 'rm -rf "${broken_dir}"' EXIT
cp "${REPO_DIR}"/database/migrations/*.sql "${broken_dir}/"
cat >"${broken_dir}/9999_check_broken.sql" <<'SQL'
CREATE TABLE ff_check_partial (id INTEGER);
SELECT this_is_not_valid_sql FROM;
SQL

broken_dir_arg="${broken_dir}"
if command -v cygpath >/dev/null 2>&1; then
  broken_dir_arg="$(cygpath -m "${broken_dir}")"  # C:/... form, readable by psql and PowerShell alike
fi

set +e
FLOWFORGE_MIGRATIONS_DIR="${broken_dir_arg}" run_migrate >/dev/null 2>&1
status=$?
set -e
[[ ${status} -ne 0 ]] || fail "runner exited 0 despite a broken migration"
[[ "$(q "SELECT count(*) FROM schema_migrations WHERE version = '9999_check_broken';")" == "0" ]] ||
  fail "broken migration was recorded in schema_migrations"
[[ "$(q "SELECT to_regclass('ff_check_partial') IS NULL;")" == "t" ]] ||
  fail "broken migration left partial DDL behind (not applied atomically)"
pass "broken migration: non-zero exit (${status}), not recorded, no partial DDL"

echo "done: migration checks passed"
