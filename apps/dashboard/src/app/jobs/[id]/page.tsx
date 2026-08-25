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
    </div>
  );
}
