import Link from "next/link";
import { notFound } from "next/navigation";

import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { WorkloadItemsTable } from "@/components/workloads/workload-items-table";
import { WorkloadProgressPanel } from "@/components/workloads/workload-progress-panel";
import { apiClient, ApiError } from "@/lib/api-client";

export default async function WorkloadDetailPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;

  let workload: Awaited<ReturnType<typeof apiClient.getWorkload>> | null = null;
  try {
    workload = await apiClient.getWorkload(id);
  } catch (error) {
    if (error instanceof ApiError && error.status === 404) {
      notFound();
    }
    throw error;
  }

  return (
    <div>
      <Link href="/users" className="text-sm text-[var(--accent)]">
        &larr; Back to users
      </Link>

      <PageHeader title="Workload" description={workload.id} />

      <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 lg:grid-cols-4">
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Type</div>
          <div className="mt-2 text-lg font-semibold">{workload.type}</div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Total items</div>
          <div className="mt-2 text-lg font-semibold">{workload.total_items}</div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Created</div>
          <div className="mt-2 text-sm">{new Date(workload.created_at).toLocaleString()}</div>
        </Card>
        <Card>
          <div className="text-xs uppercase tracking-wide text-[var(--muted)]">Updated</div>
          <div className="mt-2 text-sm">{new Date(workload.updated_at).toLocaleString()}</div>
        </Card>
      </div>

      <div className="mt-4">
        <WorkloadProgressPanel workloadId={workload.id} initialWorkload={workload} />
      </div>

      <div className="mt-4">
        <WorkloadItemsTable workloadId={workload.id} />
      </div>
    </div>
  );
}
