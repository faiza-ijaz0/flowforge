"use client";

import Link from "next/link";
import { useId, useRef, useState } from "react";

import type { UserImportResponse } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { WorkloadProgressPanel } from "@/components/workloads/workload-progress-panel";
import { apiClient, ApiError } from "@/lib/api-client";
import { previewCsvFile, type CsvPreview, type CsvPreviewSkipped } from "@/lib/csv-preview";

type Stage = "idle" | "previewing" | "ready" | "submitting" | "created" | "error";

function isSkipped(preview: CsvPreview | CsvPreviewSkipped | null): preview is CsvPreviewSkipped {
  return preview !== null && "skipped" in preview;
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function UserImportWizard() {
  const [stage, setStage] = useState<Stage>("idle");
  const [file, setFile] = useState<File | null>(null);
  const [preview, setPreview] = useState<CsvPreview | CsvPreviewSkipped | null>(null);
  const [dragActive, setDragActive] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<UserImportResponse | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const inputId = useId();

  async function handleFile(selected: File | null) {
    setError(null);
    setResult(null);
    if (!selected) {
      setFile(null);
      setPreview(null);
      setStage("idle");
      return;
    }
    setFile(selected);
    setStage("previewing");
    try {
      const computed = await previewCsvFile(selected);
      setPreview(computed);
      setStage("ready");
    } catch {
      // Client-side preview is best-effort UX sugar (see csv-preview.ts) --
      // a failure to read/preview the file locally must not block the
      // user from still attempting the real, authoritative upload.
      setPreview(null);
      setStage("ready");
    }
  }

  async function handleStartImport() {
    if (!file) return;
    setStage("submitting");
    setError(null);
    try {
      const imported = await apiClient.createUserImportWorkload(file);
      setResult(imported);
      setStage("created");
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to import users");
      setStage("error");
    }
  }

  function reset() {
    setFile(null);
    setPreview(null);
    setResult(null);
    setError(null);
    setStage("idle");
    if (fileInputRef.current) fileInputRef.current.value = "";
  }

  if (stage === "created" && result) {
    return (
      <div className="flex flex-col gap-4">
        <Card className="border-emerald-500/30">
          <div className="flex items-start justify-between gap-4">
            <div>
              <div className="text-sm font-medium text-emerald-300">Import started</div>
              <p className="mt-1 text-sm text-[var(--muted)]">
                {result.total_rows} rows in file &middot; {result.valid_rows} valid &middot;{" "}
                {result.invalid_rows} invalid
              </p>
            </div>
            <div className="flex shrink-0 items-center gap-2">
              <Link
                href={`/workloads/${result.id}`}
                className="rounded-md bg-[var(--accent)] px-3 py-1.5 text-sm font-medium text-white hover:opacity-90"
              >
                View Processing Workload
              </Link>
              <button
                type="button"
                onClick={reset}
                className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
              >
                New import
              </button>
            </div>
          </div>
          {result.rejected_rows.length > 0 && (
            <div className="mt-4">
              <div className="mb-2 text-xs uppercase tracking-wide text-[var(--muted)]">
                Rejected rows{result.rejected_rows_truncated ? " (showing first " + result.rejected_rows.length + ")" : ""}
              </div>
              <ul className="max-h-40 overflow-y-auto rounded-md border border-[var(--border)] text-xs">
                {result.rejected_rows.map((row) => (
                  <li key={row.row_number} className="border-b border-[var(--border)] px-3 py-1.5 last:border-0">
                    <span className="font-mono text-[var(--muted)]">Row {row.row_number}:</span> {row.reason}
                  </li>
                ))}
              </ul>
            </div>
          )}
        </Card>

        <WorkloadProgressPanel workloadId={result.id} initialWorkload={result} />
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-4">
      <Card>
        <div className="mb-3 text-sm font-medium">Upload CSV</div>
        <p className="mb-3 text-xs text-[var(--muted)]">
          Supported columns: <code className="text-[var(--foreground)]">name</code>,{" "}
          <code className="text-[var(--foreground)]">email</code> (required),{" "}
          <code className="text-[var(--foreground)]">phone</code> (optional).
        </p>

        <label
          htmlFor={inputId}
          onDragOver={(e) => {
            e.preventDefault();
            setDragActive(true);
          }}
          onDragLeave={() => setDragActive(false)}
          onDrop={(e) => {
            e.preventDefault();
            setDragActive(false);
            void handleFile(e.dataTransfer.files[0] ?? null);
          }}
          className={`flex cursor-pointer flex-col items-center justify-center rounded-lg border-2 border-dashed p-8 text-center transition-colors ${
            dragActive ? "border-[var(--accent)] bg-[var(--accent)]/5" : "border-[var(--border)] hover:bg-white/5"
          }`}
        >
          <span className="text-sm font-medium">Drag & drop a CSV file here, or click to choose a file</span>
          <span className="mt-1 text-xs text-[var(--muted)]">UTF-8 CSV, up to 2 MB, up to 1000 users</span>
          <input
            ref={fileInputRef}
            id={inputId}
            type="file"
            accept=".csv,text/csv"
            className="sr-only"
            onChange={(e) => void handleFile(e.target.files?.[0] ?? null)}
          />
        </label>

        {file && (
          <div className="mt-4 flex flex-wrap items-center gap-x-6 gap-y-1 text-sm">
            <div>
              <span className="text-[var(--muted)]">File: </span>
              {file.name}
            </div>
            <div>
              <span className="text-[var(--muted)]">Size: </span>
              {formatBytes(file.size)}
            </div>
          </div>
        )}
      </Card>

      {stage === "previewing" && (
        <Card>
          <div className="text-sm text-[var(--muted)]">Reading file…</div>
        </Card>
      )}

      {stage === "ready" && preview && !isSkipped(preview) && preview.fileError && (
        <Card className="border-red-500/30">
          <div className="text-sm text-red-400">{preview.fileError}</div>
        </Card>
      )}

      {stage === "ready" && preview && !isSkipped(preview) && !preview.fileError && (
        <Card>
          <div className="flex items-center gap-4 text-sm">
            <span className="font-medium">{preview.totalRows} rows</span>
            <span className="text-emerald-300">{preview.validRows} valid</span>
            <span className="text-red-300">{preview.invalidRows} invalid</span>
          </div>
          <p className="mt-1 text-xs text-[var(--muted)]">
            Local preview only -- the server independently validates every row on import.
          </p>

          {preview.previewRows.length > 0 && (
            <div className="mt-3 max-h-64 overflow-y-auto overflow-x-auto rounded-md border border-[var(--border)]">
              <table className="w-full text-left text-xs">
                <thead className="sticky top-0 bg-[var(--surface)]">
                  <tr className="border-b border-[var(--border)] uppercase tracking-wide text-[var(--muted)]">
                    <th className="px-3 py-2 font-medium">Row</th>
                    <th className="px-3 py-2 font-medium">Name</th>
                    <th className="px-3 py-2 font-medium">Email</th>
                    <th className="px-3 py-2 font-medium">Status</th>
                  </tr>
                </thead>
                <tbody>
                  {preview.previewRows.map((row) => (
                    <tr key={row.rowNumber} className="border-b border-[var(--border)] last:border-0">
                      <td className="px-3 py-1.5 text-[var(--muted)]">{row.rowNumber}</td>
                      <td className="px-3 py-1.5">{row.name || "—"}</td>
                      <td className="px-3 py-1.5">{row.email || "—"}</td>
                      <td className={`px-3 py-1.5 ${row.valid ? "text-emerald-300" : "text-red-300"}`}>
                        {row.valid ? "valid" : row.reason}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
          {preview.totalRows > preview.previewRows.length && (
            <p className="mt-2 text-xs text-[var(--muted)]">
              Showing first {preview.previewRows.length} of {preview.totalRows} rows.
            </p>
          )}
        </Card>
      )}

      {stage === "ready" && preview && isSkipped(preview) && (
        <Card>
          <div className="text-sm text-[var(--muted)]">{preview.reason}</div>
        </Card>
      )}

      {stage === "error" && error && (
        <Card className="border-red-500/30">
          <div className="text-sm text-red-400">{error}</div>
        </Card>
      )}

      <div>
        <button
          type="button"
          onClick={handleStartImport}
          disabled={!file || stage === "submitting" || stage === "previewing" || Boolean(preview && !isSkipped(preview) && preview.fileError)}
          className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white disabled:cursor-not-allowed disabled:opacity-50"
        >
          {stage === "submitting" ? "Starting import…" : "Start Import"}
        </button>
      </div>
    </div>
  );
}
