"use client";

import { useRouter } from "next/navigation";
import { useState } from "react";

import { apiClient, ApiError } from "@/lib/api-client";

export function CreateJobForm() {
  const router = useRouter();
  const [queueName, setQueueName] = useState("default");
  const [payload, setPayload] = useState('{\n  "example": true\n}');
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setError(null);

    let parsedPayload: unknown;
    try {
      parsedPayload = JSON.parse(payload);
    } catch {
      setError("Payload must be valid JSON");
      return;
    }

    setSubmitting(true);
    try {
      await apiClient.createJob({ queue_name: queueName, payload: parsedPayload });
      router.refresh();
    } catch (err) {
      setError(err instanceof ApiError ? err.message : "Failed to create job");
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <form onSubmit={handleSubmit} className="flex flex-col gap-3">
      <div>
        <label className="mb-1 block text-xs text-[var(--muted)]">Queue name</label>
        <input
          value={queueName}
          onChange={(e) => setQueueName(e.target.value)}
          className="w-full rounded-md border border-[var(--border)] bg-transparent px-3 py-1.5 text-sm outline-none focus:border-[var(--accent)]"
          required
        />
      </div>
      <div>
        <label className="mb-1 block text-xs text-[var(--muted)]">Payload (JSON)</label>
        <textarea
          value={payload}
          onChange={(e) => setPayload(e.target.value)}
          rows={4}
          className="w-full rounded-md border border-[var(--border)] bg-transparent px-3 py-1.5 font-mono text-sm outline-none focus:border-[var(--accent)]"
          required
        />
      </div>
      {error && <div className="text-sm text-red-400">{error}</div>}
      <button
        type="submit"
        disabled={submitting}
        className="self-start rounded-md bg-[var(--accent)] px-4 py-1.5 text-sm font-medium text-white disabled:opacity-50"
      >
        {submitting ? "Creating…" : "Create job"}
      </button>
    </form>
  );
}
