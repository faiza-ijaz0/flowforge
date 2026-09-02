"use client";

import { useId, useRef, useState } from "react";

import type { InputSourceType, ProcessingTarget, ProcessResponse } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { WorkloadProgressPanel } from "@/components/workloads/workload-progress-panel";
import { apiClient, ApiError } from "@/lib/api-client";

type Stage = "idle" | "submitting" | "created" | "error";

const TARGETS: { value: ProcessingTarget; label: string; available: boolean }[] = [
  { value: "users", label: "Users", available: true },
  { value: "products", label: "Products", available: false },
  { value: "categories", label: "Categories", available: false },
];

const SOURCES: { value: InputSourceType; label: string; available: boolean }[] = [
  { value: "csv", label: "CSV", available: true },
  { value: "image", label: "Image", available: false },
  { value: "screenshot", label: "Screenshot", available: false },
  { value: "text", label: "Text", available: false },
  { value: "url", label: "URL", available: false },
];

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

/**
 * The Processing Center's upload panel: a target selector (Users/Products/
 * Categories) crossed with a source selector (CSV/Image/Screenshot/Text/
 * URL). Only the target/source combinations the server actually implements
 * are clickable -- every other option is visibly present but disabled and
 * labeled "Coming soon" rather than a silently-do-nothing button. See
 * docs/architecture/input-processing.md, "Processing Center".
 *
 * This is intentionally a thinner panel than <UserImportWizard>: it has no
 * client-side CSV preview, because it exists to prove out the *generic*
 * source x target flow for targets that don't have -- and shouldn't need --
 * a bespoke wizard. /users keeps its richer wizard; this panel is what every
 * future target gets by default once its extractor ships.
 */
export function ProcessingUploadPanel() {
  const [target, setTarget] = useState<ProcessingTarget>("users");
  const [source, setSource] = useState<InputSourceType>("csv");
  const [file, setFile] = useState<File | null>(null);
  const [stage, setStage] = useState<Stage>("idle");
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<ProcessResponse | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const inputId = useId();

  const isSupportedCombination = target === "users" && source === "csv";

  function reset() {
    setFile(null);
    setResult(null);
    setError(null);
    setStage("idle");
    if (fileInputRef.current) fileInputRef.current.value = "";
  }

  async function handleSubmit() {
    if (!file) return;
    setStage("submitting");
    setError(null);
    try {
      const processed = await apiClient.process(source, target, file);
      setResult(processed);
      setStage("created");
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to process input");
      setStage("error");
    }
  }

  if (stage === "created" && result) {
    return (
      <div className="flex flex-col gap-4">
        <Card className="border-emerald-500/30">
          <div className="flex items-start justify-between gap-4">
            <div>
              <div className="text-sm font-medium text-emerald-300">Processing started</div>
              <p className="mt-1 text-sm text-[var(--muted)]">
                {result.total_records} records &middot; {result.valid_records} valid &middot;{" "}
                {result.invalid_records} invalid
              </p>
            </div>
            <button
              type="button"
              onClick={reset}
              className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
            >
              New submission
            </button>
          </div>
          {result.rejected_records.length > 0 && (
            <div className="mt-4">
              <div className="mb-2 text-xs uppercase tracking-wide text-[var(--muted)]">
                Rejected records
                {result.rejected_records_truncated ? ` (showing first ${result.rejected_records.length})` : ""}
              </div>
              <ul className="max-h-40 overflow-y-auto rounded-md border border-[var(--border)] text-xs">
                {result.rejected_records.map((record) => (
                  <li key={record.index} className="border-b border-[var(--border)] px-3 py-1.5 last:border-0">
                    <span className="font-mono text-[var(--muted)]">Record {record.index}:</span> {record.reason}
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
        <div className="mb-3 text-sm font-medium">Processing target</div>
        <div className="flex flex-wrap gap-2">
          {TARGETS.map((option) => (
            <SelectorButton
              key={option.value}
              label={option.label}
              available={option.available}
              selected={target === option.value}
              onClick={() => {
                setTarget(option.value);
                reset();
              }}
            />
          ))}
        </div>
      </Card>

      <Card>
        <div className="mb-3 text-sm font-medium">Input source</div>
        <div className="flex flex-wrap gap-2">
          {SOURCES.map((option) => (
            <SelectorButton
              key={option.value}
              label={option.label}
              available={option.available}
              selected={source === option.value}
              onClick={() => {
                setSource(option.value);
                reset();
              }}
            />
          ))}
        </div>
      </Card>

      {!isSupportedCombination && (
        <Card className="border-dashed">
          <div className="text-sm text-[var(--muted)]">
            {TARGETS.find((t) => t.value === target)?.label} via {SOURCES.find((s) => s.value === source)?.label} is
            not implemented yet. Select <strong className="text-[var(--foreground)]">Users</strong> +{" "}
            <strong className="text-[var(--foreground)]">CSV</strong> to process input today.
          </div>
        </Card>
      )}

      {isSupportedCombination && (
        <Card>
          <div className="mb-3 text-sm font-medium">Upload CSV</div>
          <label
            htmlFor={inputId}
            className="flex cursor-pointer flex-col items-center justify-center rounded-lg border-2 border-dashed border-[var(--border)] p-8 text-center transition-colors hover:bg-white/5"
          >
            <span className="text-sm font-medium">Click to choose a CSV file</span>
            <span className="mt-1 text-xs text-[var(--muted)]">UTF-8 CSV, up to 2 MB, up to 1000 records</span>
            <input
              ref={fileInputRef}
              id={inputId}
              type="file"
              accept=".csv,text/csv"
              className="sr-only"
              onChange={(e) => {
                setFile(e.target.files?.[0] ?? null);
                setError(null);
              }}
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
      )}

      {stage === "error" && error && (
        <Card className="border-red-500/30">
          <div className="text-sm text-red-400">{error}</div>
        </Card>
      )}

      {isSupportedCombination && (
        <div>
          <button
            type="button"
            onClick={handleSubmit}
            disabled={!file || stage === "submitting"}
            className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white disabled:cursor-not-allowed disabled:opacity-50"
          >
            {stage === "submitting" ? "Processing…" : "Start Processing"}
          </button>
        </div>
      )}
    </div>
  );
}

function SelectorButton({
  label,
  available,
  selected,
  onClick,
}: {
  label: string;
  available: boolean;
  selected: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      disabled={!available}
      onClick={onClick}
      className={`flex items-center gap-2 rounded-md border px-3 py-1.5 text-sm font-medium transition-colors ${
        selected
          ? "border-[var(--accent)] bg-[var(--accent)]/15 text-[var(--foreground)]"
          : "border-[var(--border)] text-[var(--muted)] hover:bg-white/5"
      } ${!available ? "cursor-not-allowed opacity-50" : ""}`}
    >
      {label}
      {!available && (
        <span className="rounded-full bg-white/10 px-1.5 py-0.5 text-[10px] uppercase tracking-wide">
          Coming soon
        </span>
      )}
    </button>
  );
}
