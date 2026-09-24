# Applies every not-yet-applied SQL file in database/migrations/, in
# filename order, tracking progress in the schema_migrations table.
# PowerShell equivalent of db-migrate.sh -- see that file for the
# rationale behind a hand-rolled runner instead of a migration framework.
$ErrorActionPreference = "Stop"

$DatabaseUrl = $env:FLOWFORGE_DATABASE_URL
if (-not $DatabaseUrl) { $DatabaseUrl = $env:DATABASE_URL }
if (-not $DatabaseUrl) {
    Write-Error "Set FLOWFORGE_DATABASE_URL (or DATABASE_URL) to a postgres:// connection string"
    exit 1
}

if (-not (Get-Command psql -ErrorAction SilentlyContinue)) {
    Write-Error "psql not found on PATH"
    exit 1
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MigrationsDir = Join-Path $ScriptDir "..\database\migrations"

psql $DatabaseUrl -v ON_ERROR_STOP=1 -c `
    "CREATE TABLE IF NOT EXISTS schema_migrations (version TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now());" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Error "failed to create/verify schema_migrations (psql exited with code $LASTEXITCODE)"
    exit 1
}

$appliedCount = 0
Get-ChildItem -Path $MigrationsDir -Filter "*.sql" | Sort-Object Name | ForEach-Object {
    $version = $_.BaseName
    # psql -t -A prints nothing (not even a blank line) when the query
    # matches zero rows -- which is the normal, expected case for every
    # not-yet-applied migration against a schema_migrations table that
    # was just created. PowerShell then captures that as $null rather
    # than an empty string, so .Trim() on the raw psql output would throw
    # "cannot call a method on a null-valued expression" the very first
    # time this script runs against a genuinely fresh database (verified:
    # a plain [string] cast does not reliably coerce $null to "" here --
    # piping through Out-String does, since Out-String always returns a
    # real System.String, never $null, regardless of its input).
    $alreadyApplied = (psql $DatabaseUrl -t -A -c "SELECT 1 FROM schema_migrations WHERE version = '$version';" | Out-String).Trim()

    if ($alreadyApplied -eq "1") {
        Write-Host "skip    $version (already applied)"
        return
    }

    Write-Host "apply   $version"
    # psql's `\i` meta-command mis-parses a Windows backslash path (it
    # treats backslashes inside its own argument specially), which
    # produces a bizarre "C:: Permission denied" failure that only
    # touches the drive letter -- forward slashes are accepted by both
    # psql and the Windows filesystem APIs, so this converts before
    # interpolating. Verified as a real, previously-silent bug (Phase
    # 3H): psql exited non-zero on every migration, but this script kept
    # incrementing $appliedCount and printing "done: N migration(s)
    # applied" regardless, because native-command exit codes don't
    # automatically become PowerShell terminating errors -- $LASTEXITCODE
    # must be checked explicitly, which the code below now does.
    $forwardSlashPath = $_.FullName -replace '\\', '/'
    psql $DatabaseUrl -v ON_ERROR_STOP=1 -1 -c "\i $forwardSlashPath" -c "INSERT INTO schema_migrations (version) VALUES ('$version');" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Error "migration ${version}: psql exited with code $LASTEXITCODE -- aborting, schema_migrations was NOT updated for this version"
        exit 1
    }
    $script:appliedCount++
}

Write-Host "done: $appliedCount migration(s) applied"
