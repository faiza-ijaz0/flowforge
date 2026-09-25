import { Card } from "@/components/ui/card";
import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";
import { StatusBadge } from "@/components/ui/status-badge";
import { apiClient, ApiError } from "@/lib/api-client";

export default async function WorkflowsPage() {
  let workflows: Awaited<ReturnType<typeof apiClient.listWorkflows>> | null = null;
  let loadError: string | null = null;
  try {
    workflows = await apiClient.listWorkflows();
  } catch (error) {
    loadError = error instanceof ApiError ? error.message : "Failed to load workflows";
  }

  return (
    <div>
      <PageHeader
        title="Workflows"
        description="Workflows stored in the database. Creating and running workflows is not implemented yet."
      />

      {loadError ? (
        <Card>
          <div className="text-sm text-red-400">{loadError}</div>
        </Card>
      ) : workflows && workflows.workflows.length > 0 ? (
        <Card className="p-0">
          <table className="w-full text-left text-sm">
            <thead>
              <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                <th className="px-4 py-3 font-medium">Name</th>
                <th className="px-4 py-3 font-medium">Status</th>
                <th className="px-4 py-3 font-medium">Steps</th>
              </tr>
            </thead>
            <tbody>
              {workflows.workflows.map((wf) => (
                <tr key={wf.id} className="border-b border-[var(--border)] last:border-0">
                  <td className="px-4 py-3">{wf.name}</td>
                  <td className="px-4 py-3">
                    <StatusBadge status={wf.status} />
                  </td>
                  <td className="px-4 py-3 text-[var(--muted)]">{wf.steps.length}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </Card>
      ) : (
        <NotYetImplemented
          feature="Workflow creation and execution"
          note="There is no API to create or run workflows yet, so this list stays empty."
        />
      )}
    </div>
  );
}
