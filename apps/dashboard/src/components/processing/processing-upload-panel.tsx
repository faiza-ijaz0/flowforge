"use client";

import { useEffect, useId, useRef, useState } from "react";

import type { InputSourceType, PreviewResponse, ProcessingTarget, ProcessResponse } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { WorkloadProgressPanel } from "@/components/workloads/workload-progress-panel";
import { apiClient, ApiError } from "@/lib/api-client";

type Stage = "idle" | "extracting" | "preview-ready" | "confirming" | "submitting" | "created" | "error";

const TARGETS: { value: ProcessingTarget; label: string; available: boolean }[] = [
  { value: "users", label: "Users", available: true },
  { value: "products", label: "Products", available: true },
  { value: "categories", label: "Categories", available: true },
];

const SOURCES: { value: InputSourceType; label: string; available: boolean }[] = [
  { value: "csv", label: "CSV", available: true },
  { value: "image", label: "Image", available: true },
  { value: "screenshot", label: "Screenshot", available: true },
  { value: "text", label: "Text", available: false },
  { value: "url", label: "URL", available: false },
];

/// Column definitions for the extraction-preview table, keyed by target
/// (Phase 3E; Categories added Phase 3F -- see
/// docs/architecture/category-processing.md). `preview` records are a
/// generic field-name -> string map (`@flowforge/shared`'s
/// `StructuredRecord`); this is the one place the dashboard knows which
/// fields each target's records carry.
const PREVIEW_COLUMNS: Record<ProcessingTarget, { key: string; label: string }[]> = {
  users: [
    { key: "name", label: "Name" },
    { key: "email", label: "Email" },
    { key: "phone", label: "Phone" },
  ],
  products: [
    { key: "sku", label: "SKU" },
    { key: "name", label: "Name" },
    { key: "price", label: "Price" },
    { key: "currency", label: "Currency" },
    { key: "category", label: "Category" },
    { key: "stock_quantity", label: "Stock" },
  ],
  categories: [
    { key: "name", label: "Name" },
    { key: "slug", label: "Slug" },
    { key: "parent_slug", label: "Parent" },
  ],
};

const TARGET_LABELS: Record<ProcessingTarget, string> = {
  users: "Users",
  products: "Products",
  categories: "Categories",
};

// Client-side hints only -- the server independently validates every
// upload by inspecting its actual file-signature bytes, never trusting
// `File.type` or extension (see docs/architecture/input-processing.md,
// "Image validation"). Mirrors the server's own infra::kMaxImageBytes.
const MAX_IMAGE_BYTES = 6 * 1024 * 1024;
const ACCEPTED_IMAGE_TYPES = ["image/png", "image/jpeg", "image/webp"];

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
 * Phase 3D-1 adds a second, real flow alongside CSV's direct
 * upload -> workload: Image/Screenshot + Users goes upload -> **extract**
 * (`apiClient.previewProcess`, creates nothing server-side) -> a review
 * table the user must explicitly confirm -> **confirm**
 * (`apiClient.confirmProcess`, the only one of the two that creates a
 * workload) -- see docs/architecture/input-processing.md, "Preview" /
 * "Confirmation". CSV's flow is unchanged: it has no preview step,
 * exactly as before.
 */
export function ProcessingUploadPanel() {
  const [target, setTarget] = useState<ProcessingTarget>("users");
  const [source, setSource] = useState<InputSourceType>("csv");
  const [file, setFile] = useState<File | null>(null);
  const [imagePreviewUrl, setImagePreviewUrl] = useState<string | null>(null);
  const [dragActive, setDragActive] = useState(false);
  const [stage, setStage] = useState<Stage>("idle");
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<ProcessResponse | null>(null);
  const [preview, setPreview] = useState<PreviewResponse | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const inputId = useId();

  const isImageFlow = source === "image" || source === "screenshot";
  // Users+CSV is the one combination with no preview step (direct
  // process() -- see docs/architecture/input-processing.md, "Why CSV has
  // no preview step"). Every other implemented combination -- Users'
  // image/screenshot, and both of Products' sources -- goes through
  // preview() -> confirm() instead (see docs/architecture/
  // product-processing.md, "Why CSV+Products has no direct process()
  // path").
  const isDirectSubmit = target === "users" && source === "csv";
  const isSupportedCombination =
    (target === "users" || target === "products" || target === "categories") &&
    (source === "csv" || isImageFlow);
  const usesPreviewFlow = isSupportedCombination && !isDirectSubmit;

  // Object URLs must be revoked when replaced or on unmount -- otherwise
  // each selected image leaks its blob for the life of the tab.
  useEffect(() => {
    return () => {
      if (imagePreviewUrl) URL.revokeObjectURL(imagePreviewUrl);
    };
  }, [imagePreviewUrl]);

  function reset() {
    setFile(null);
    if (imagePreviewUrl) URL.revokeObjectURL(imagePreviewUrl);
    setImagePreviewUrl(null);
    setResult(null);
    setPreview(null);
    setError(null);
    setStage("idle");
    if (fileInputRef.current) fileInputRef.current.value = "";
  }

  function handleFile(selected: File | null) {
    setError(null);
    setResult(null);
    setPreview(null);
    if (imagePreviewUrl) {
      URL.revokeObjectURL(imagePreviewUrl);
      setImagePreviewUrl(null);
    }
    setFile(selected);
    setStage("idle");
    if (selected && isImageFlow) {
      setImagePreviewUrl(URL.createObjectURL(selected));
    }
  }

  const imageFileProblem =
    file && isImageFlow
      ? file.size > MAX_IMAGE_BYTES
        ? `File is ${formatBytes(file.size)}, which exceeds the ${formatBytes(MAX_IMAGE_BYTES)} limit.`
        : file.type && !ACCEPTED_IMAGE_TYPES.includes(file.type)
          ? "Unsupported file type -- please choose a PNG, JPEG, or WebP image."
          : null
      : null;

  async function handleExtract() {
    if (!file) return;
    setStage("extracting");
    setError(null);
    try {
      const previewed = await apiClient.previewProcess(source, target, file);
      setPreview(previewed);
      setStage("preview-ready");
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to extract data from the image");
      setStage("error");
    }
  }

  async function handleConfirm() {
    if (!preview || preview.records.length === 0) return;
    setStage("confirming");
    setError(null);
    try {
      const processed = await apiClient.confirmProcess(target, preview.records);
      setResult(processed);
      setStage("created");
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to submit the extracted records");
      setStage("error");
    }
  }

  async function handleSubmitCsv() {
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

  // Covers "preview-ready", "confirming", and an "error" that occurred
  // while confirming (confirmProcess failing must not discard the
  // preview table the user was looking at -- they should be able to see
  // the error alongside it and retry or cancel, not be dropped back to
  // the upload selectors).
  if (preview && stage !== "created") {
    return (
      <div className="flex flex-col gap-4">
        <Card>
          <div className="flex items-start justify-between gap-4">
            <div>
              <div className="text-sm font-medium">Extracted {TARGET_LABELS[preview.target]}</div>
              <p className="mt-1 flex items-center gap-3 text-sm">
                <span className="text-emerald-300">{preview.valid_records} valid</span>
                <span className="text-red-300">{preview.invalid_records} invalid</span>
                {preview.average_confidence !== null && (
                  <span className="text-[var(--muted)]">
                    ~{Math.round(preview.average_confidence)}% OCR confidence
                  </span>
                )}
              </p>
            </div>
            <button
              type="button"
              onClick={reset}
              className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
            >
              Cancel
            </button>
          </div>

          {preview.warnings.length > 0 && (
            <div className="mt-3 rounded-md border border-amber-500/30 bg-amber-500/5 p-3 text-xs text-amber-300">
              {preview.warnings.map((warning) => (
                <div key={warning}>{warning}</div>
              ))}
            </div>
          )}

          {preview.records.length > 0 && (
            <div className="mt-4 max-h-72 overflow-y-auto overflow-x-auto rounded-md border border-[var(--border)]">
              <table className="w-full text-left text-xs">
                <thead className="sticky top-0 bg-[var(--surface)]">
                  <tr className="border-b border-[var(--border)] uppercase tracking-wide text-[var(--muted)]">
                    {PREVIEW_COLUMNS[preview.target].map((column) => (
                      <th key={column.key} className="px-3 py-2 font-medium">
                        {column.label}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {preview.records.map((record, i) => (
                    <tr key={i} className="border-b border-[var(--border)] last:border-0">
                      {PREVIEW_COLUMNS[preview.target].map((column) => (
                        <td key={column.key} className="px-3 py-1.5">
                          {record[column.key] ?? "—"}
                        </td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}

          {preview.rejected_records.length > 0 && (
            <div className="mt-4">
              <div className="mb-2 text-xs uppercase tracking-wide text-[var(--muted)]">
                Invalid rows{preview.rejected_records_truncated ? " (showing first " + preview.rejected_records.length + ")" : ""}
              </div>
              <ul className="max-h-40 overflow-y-auto rounded-md border border-red-500/20 text-xs">
                {preview.rejected_records.map((record) => (
                  <li key={record.index} className="border-b border-red-500/10 px-3 py-1.5 text-red-300 last:border-0">
                    <span className="font-mono text-[var(--muted)]">Row {record.index}:</span> {record.reason}
                  </li>
                ))}
              </ul>
            </div>
          )}
        </Card>

        {stage === "error" && error && (
          <Card className="border-red-500/30">
            <div className="text-sm text-red-400">{error}</div>
          </Card>
        )}

        <div className="flex gap-2">
          <button
            type="button"
            onClick={handleConfirm}
            disabled={preview.records.length === 0 || stage === "confirming"}
            className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white disabled:cursor-not-allowed disabled:opacity-50"
          >
            {stage === "confirming" ? "Processing…" : "Process Valid Records"}
          </button>
          <button
            type="button"
            onClick={reset}
            className="rounded-md border border-[var(--border)] px-4 py-2 text-sm font-medium hover:bg-white/5"
          >
            Cancel
          </button>
        </div>
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
            not implemented yet. Select <strong className="text-[var(--foreground)]">Users</strong>,{" "}
            <strong className="text-[var(--foreground)]">Products</strong>, or{" "}
            <strong className="text-[var(--foreground)]">Categories</strong> with{" "}
            <strong className="text-[var(--foreground)]">CSV</strong>,{" "}
            <strong className="text-[var(--foreground)]">Image</strong>, or{" "}
            <strong className="text-[var(--foreground)]">Screenshot</strong> to process input today.
          </div>
        </Card>
      )}

      {isSupportedCombination && source === "csv" && (
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
              onChange={(e) => handleFile(e.target.files?.[0] ?? null)}
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

      {isSupportedCombination && isImageFlow && (
        <Card>
          <div className="mb-3 text-sm font-medium">
            Upload {source === "screenshot" ? "screenshot" : "image"}
          </div>
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
              handleFile(e.dataTransfer.files[0] ?? null);
            }}
            className={`flex cursor-pointer flex-col items-center justify-center rounded-lg border-2 border-dashed p-8 text-center transition-colors ${
              dragActive ? "border-[var(--accent)] bg-[var(--accent)]/5" : "border-[var(--border)] hover:bg-white/5"
            }`}
          >
            <span className="text-sm font-medium">Drag & drop an image here, or click to choose a file</span>
            <span className="mt-1 text-xs text-[var(--muted)]">
              PNG, JPEG, or WebP &middot; up to {formatBytes(MAX_IMAGE_BYTES)}
            </span>
            <input
              ref={fileInputRef}
              id={inputId}
              type="file"
              accept="image/png,image/jpeg,image/webp"
              className="sr-only"
              onChange={(e) => handleFile(e.target.files?.[0] ?? null)}
            />
          </label>

          {file && (
            <div className="mt-4 flex flex-col gap-3 sm:flex-row sm:items-start">
              {imagePreviewUrl && (
                // eslint-disable-next-line @next/next/no-img-element -- locally-selected blob: URL, not an optimizable remote image
                <img
                  src={imagePreviewUrl}
                  alt="Selected upload preview"
                  className="h-32 w-32 shrink-0 rounded-md border border-[var(--border)] object-cover"
                />
              )}
              <div className="flex flex-wrap items-center gap-x-6 gap-y-1 text-sm">
                <div>
                  <span className="text-[var(--muted)]">File: </span>
                  {file.name}
                </div>
                <div>
                  <span className="text-[var(--muted)]">Size: </span>
                  {formatBytes(file.size)}
                </div>
              </div>
            </div>
          )}

          {imageFileProblem && (
            <div className="mt-3 rounded-md border border-red-500/30 bg-red-500/5 p-3 text-xs text-red-400">
              {imageFileProblem}
            </div>
          )}
        </Card>
      )}

      {stage === "extracting" && (
        <Card>
          <div className="flex items-center gap-3">
            <span className="h-4 w-4 shrink-0 animate-spin rounded-full border-2 border-[var(--border)] border-t-[var(--accent)]" />
            <div className="text-sm">
              <div className="font-medium">{isImageFlow ? "Analyzing image…" : "Analyzing file…"}</div>
              <div className="mt-0.5 text-xs text-[var(--muted)]">Extracting rows · Validating records</div>
            </div>
          </div>
        </Card>
      )}

      {stage === "error" && error && (
        <Card className="border-red-500/30">
          <div className="text-sm text-red-400">{error}</div>
        </Card>
      )}

      {isDirectSubmit && (
        <div>
          <button
            type="button"
            onClick={handleSubmitCsv}
            disabled={!file || stage === "submitting"}
            className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white disabled:cursor-not-allowed disabled:opacity-50"
          >
            {stage === "submitting" ? "Processing…" : "Start Processing"}
          </button>
        </div>
      )}

      {usesPreviewFlow && (
        <div>
          <button
            type="button"
            onClick={handleExtract}
            disabled={!file || Boolean(imageFileProblem) || stage === "extracting"}
            className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white disabled:cursor-not-allowed disabled:opacity-50"
          >
            {stage === "extracting" ? "Extracting…" : "Extract Data"}
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
