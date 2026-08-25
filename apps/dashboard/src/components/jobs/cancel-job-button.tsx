"use client";

import { useRouter } from "next/navigation";
import { useState } from "react";

import { apiClient, ApiError } from "@/lib/api-client";

export function CancelJobButton({ jobId, disabled }: { jobId: string; disabled: boolean }) {
  const router = useRouter();
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function handleClick() {
    setPending(true);
    setError(null);
    try {
      await apiClient.cancelJob(jobId);
      router.refresh();
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to cancel job");
    } finally {
      setPending(false);
    }
  }

  return (
    <div className="flex flex-col items-end gap-1">
      <button
        onClick={handleClick}
        disabled={disabled || pending}
        className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium text-[var(--foreground)] hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-40"
      >
        {pending ? "Cancelling…" : "Cancel job"}
      </button>
      {error && <span className="text-xs text-red-400">{error}</span>}
    </div>
  );
}
