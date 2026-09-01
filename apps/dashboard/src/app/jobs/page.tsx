import Link from "next/link";

import { CreateJobForm } from "@/components/jobs/create-job-form";
import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

export default async function JobsPage() {
  let jobsResult: Awaited<ReturnType<typeof apiClient.listJobs>> | null = null;
  let loadError: string | null = null;
  try {
    jobsResult = await apiClient.listJobs(100, 0);
  } catch (error) {
    loadError = error instanceof ApiError ? error.message : "Failed to load jobs";
  }

  return (
    <div>
      <PageHeader
        title="Jobs"
        description="Live job records from the FlowForge API. A job with a job type is submitted to the real Scheduler, dispatched to a real WorkerPool, and actually executed by the matching handler -- open a job to see its real execution attempt history."
      />

      <div className="mb-6">
        <Card>
          <div className="mb-3 text-sm font-medium">Create a job</div>
          <CreateJobForm />
        </Card>
      </div>

      {loadError ? (
        <Card>
          <div className="text-sm text-red-400">{loadError}</div>
        </Card>
      ) : (
        <Card className="p-0">
          <table className="w-full text-left text-sm">
            <thead>
              <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                <th className="px-4 py-3 font-medium">ID</th>
                <th className="px-4 py-3 font-medium">Queue</th>
                <th className="px-4 py-3 font-medium">Type</th>
                <th className="px-4 py-3 font-medium">Priority</th>
                <th className="px-4 py-3 font-medium">Status</th>
                <th className="px-4 py-3 font-medium">Attempts</th>
                <th className="px-4 py-3 font-medium">Created</th>
              </tr>
            </thead>
            <tbody>
              {jobsResult?.jobs.length === 0 && (
                <tr>
                  <td colSpan={7} className="px-4 py-8 text-center text-[var(--muted)]">
                    No jobs yet. Create one above.
                  </td>
                </tr>
              )}
              {jobsResult?.jobs.map((job) => (
                <tr key={job.id} className="border-b border-[var(--border)] last:border-0 hover:bg-white/5">
                  <td className="px-4 py-3">
                    <Link href={`/jobs/${job.id}`} className="font-mono text-xs text-[var(--accent)]">
                      {job.id}
                    </Link>
                  </td>
                  <td className="px-4 py-3">{job.queue_name}</td>
                  <td className="px-4 py-3 text-[var(--muted)]">{job.job_type || "—"}</td>
                  <td className="px-4 py-3 text-[var(--muted)]">{job.priority}</td>
                  <td className="px-4 py-3">
                    <StatusBadge status={job.status} />
                  </td>
                  <td className="px-4 py-3 text-[var(--muted)]">
                    {job.attempt_count} / {job.max_attempts}
                  </td>
                  <td className="px-4 py-3 text-[var(--muted)]">{new Date(job.created_at).toLocaleString()}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </Card>
      )}
    </div>
  );
}
