"use client";

import { useRouter } from "next/navigation";
import { useState } from "react";

import { apiClient, ApiError } from "@/lib/api-client";

const JOB_TYPES = ["", "echo", "delay", "transform"] as const;

export function CreateJobForm({ onCreated }: { onCreated?: () => void }) {
  const router = useRouter();
  const [queueName, setQueueName] = useState("default");
  const [jobType, setJobType] = useState<string>("");
  const [payload, setPayload] = useState('{\n  "example": true\n}');
  const [error, setError] = useState<string | null>(null);
  const [info, setInfo] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setError(null);
    setInfo(null);

    let parsedPayload: unknown;
    try {
      parsedPayload = JSON.parse(payload);
    } catch {
      setError("Payload must be valid JSON");
      return;
    }

    setSubmitting(true);
    try {
      const created = await apiClient.createJob({
        queue_name: queueName,
        payload: parsedPayload,
        ...(jobType ? { job_type: jobType } : {}),
      });
      if (jobType && !created.scheduling.scheduled) {
        setInfo(`Job created but not scheduled: ${created.scheduling.reason ?? "unknown reason"}`);
      }
      router.refresh();
      onCreated?.();
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
        <label className="mb-1 block text-xs text-[var(--muted)]">
          Job type (optional -- submits to the Scheduler when set)
        </label>
        <select
          value={jobType}
          onChange={(e) => setJobType(e.target.value)}
          className="w-full rounded-md border border-[var(--border)] bg-transparent px-3 py-1.5 text-sm outline-none focus:border-[var(--accent)]"
        >
          {JOB_TYPES.map((type) => (
            <option key={type} value={type} className="bg-[var(--background)]">
              {type === "" ? "(none -- create only)" : type}
            </option>
          ))}
        </select>
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
      {info && <div className="text-sm text-yellow-400">{info}</div>}
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
