import Link from "next/link";
import { notFound } from "next/navigation";

import { CancelJobButton } from "@/components/jobs/cancel-job-button";
import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

const TERMINAL_STATUSES = new Set(["succeeded", "cancelled", "dead_letter"]);

export default async function JobDetailPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;

  let job: Awaited<ReturnType<typeof apiClient.getJob>> | null = null;
  try {
    job = await apiClient.getJob(id);
  } catch (error) {
    if (error instanceof ApiError && error.status === 404) {
      notFound();
    }
    throw error;
  }

  // Additive (Phase 2B-3): real execution attempt history. Best-effort --
  // an older server without this endpoint (or a transient error) should
  // not break the rest of the page.
  let attempts: Awaited<ReturnType<typeof apiClient.getJobAttempts>>["attempts"] = [];
  try {
    attempts = (await apiClient.getJobAttempts(id)).attempts;
  } catch {
    attempts = [];
  }

  return (
    <div>
      <Link href="/jobs" className="text-sm text-[var(--accent)]">
        &larr; Back to jobs
      </Link>

      <PageHeader
        title={job.queue_name}
        description={job.id}
        action={<CancelJobButton jobId={job.id} disabled={TERMINAL_STATUSES.has(job.status)} />}
      />

      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2">
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Status</div>
          <div className="mt-2">
            <StatusBadge status={job.status} />
          </div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Job type</div>
          <div className="mt-2 text-lg font-semibold">{job.job_type || "—"}</div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Workload</div>
          <div className="mt-2 text-lg font-semibold">
            {job.workload_id ? (
              <Link href={`/workloads/${job.workload_id}`} className="font-mono text-sm text-[var(--accent)]">
                {job.workload_id.slice(0, 8)}…
              </Link>
            ) : (
              <span className="text-[var(--muted)]">—</span>
            )}
          </div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Attempts</div>
          <div className="mt-2 text-lg font-semibold">
            {job.attempt_count} / {job.max_attempts}
          </div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Created</div>
          <div className="mt-2 text-sm">{new Date(job.created_at).toLocaleString()}</div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Updated</div>
          <div className="mt-2 text-sm">{new Date(job.updated_at).toLocaleString()}</div>
        </Card>
      </div>

      {job.last_error && (
        <Card className="mt-4 border-red-500/30">
          <div className="text-xs uppercase tracking-wide text-red-400">Last error</div>
          <div className="mt-2 text-sm">{job.last_error}</div>
        </Card>
      )}

      <Card className="mt-4">
        <div className="mb-2 text-xs uppercase tracking-wide text-[var(--muted)]">Payload</div>
        <pre className="overflow-x-auto rounded-md bg-black/30 p-3 text-xs">
          {JSON.stringify(job.payload, null, 2)}
        </pre>
      </Card>

      <Card className="mt-4 p-0">
        <div className="p-4 pb-0 text-sm font-medium">
          Execution attempts{" "}
          <span className="text-xs font-normal text-[var(--muted)]">
            (one row per execution attempt)
          </span>
        </div>
        <div className="overflow-x-auto">
          <table className="w-full text-left text-sm">
            <thead>
              <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                <th className="px-4 py-3 font-medium">#</th>
                <th className="px-4 py-3 font-medium">Outcome</th>
                <th className="px-4 py-3 font-medium">Worker</th>
                <th className="px-4 py-3 font-medium">Started</th>
                <th className="px-4 py-3 font-medium">Duration</th>
                <th className="px-4 py-3 font-medium">Error</th>
              </tr>
            </thead>
            <tbody>
              {attempts.length === 0 && (
                <tr>
                  <td colSpan={6} className="px-4 py-8 text-center text-[var(--muted)]">
                    No execution attempts yet.
                  </td>
                </tr>
              )}
              {attempts.map((attempt) => {
                const durationMs = attempt.finished_at
                  ? new Date(attempt.finished_at).getTime() - new Date(attempt.started_at).getTime()
                  : null;
                return (
                  <tr key={attempt.id} className="border-b border-[var(--border)] last:border-0">
                    <td className="px-4 py-3">{attempt.attempt_number}</td>
                    <td className="px-4 py-3">
                      <StatusBadge status={attempt.outcome} />
                    </td>
                    <td className="px-4 py-3 font-mono text-xs text-[var(--muted)]">
                      {attempt.worker_id ?? "—"}
                    </td>
                    <td className="px-4 py-3 text-[var(--muted)]">
                      {new Date(attempt.started_at).toLocaleString()}
                    </td>
                    <td className="px-4 py-3 text-[var(--muted)]">
                      {durationMs !== null ? `${durationMs}ms` : "—"}
                    </td>
                    <td className="px-4 py-3 text-red-400">{attempt.error_message ?? "—"}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </Card>
    </div>
  );
}
