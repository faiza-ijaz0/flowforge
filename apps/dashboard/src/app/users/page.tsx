import { PageHeader } from "@/components/ui/page-header";
import { UserImportWizard } from "@/components/users/user-import-wizard";

export default function UsersPage() {
  return (
    <div>
      <PageHeader
        title="Users"
        description="Import and process users through FlowForge. Each upload creates one workload; every row becomes a real user.process job, dispatched through the existing PriorityScheduler/WorkerPool and persisted in PostgreSQL -- see docs/architecture/user-import.md."
      />
      <UserImportWizard />
    </div>
  );
}
