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

$appliedCount = 0
Get-ChildItem -Path $MigrationsDir -Filter "*.sql" | Sort-Object Name | ForEach-Object {
    $version = $_.BaseName
    $alreadyApplied = (psql $DatabaseUrl -t -A -c "SELECT 1 FROM schema_migrations WHERE version = '$version';").Trim()

    if ($alreadyApplied -eq "1") {
        Write-Host "skip    $version (already applied)"
        return
    }

    Write-Host "apply   $version"
    psql $DatabaseUrl -v ON_ERROR_STOP=1 -1 -c "\i $($_.FullName)" -c "INSERT INTO schema_migrations (version) VALUES ('$version');" | Out-Null
    $script:appliedCount++
}

Write-Host "done: $appliedCount migration(s) applied"
