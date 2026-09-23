import Link from "next/link";

import { PageHeader } from "@/components/ui/page-header";
import { UserImportWizard } from "@/components/users/user-import-wizard";

export default function UsersPage() {
  return (
    <div>
      <PageHeader
        title="Users"
        description="Import and process users through FlowForge. Each upload creates one workload; every row becomes a real user.process job, dispatched through the existing PriorityScheduler/WorkerPool. Unlike Products/Categories, there is no dedicated users table yet -- each job validates and normalizes its row, and that outcome is persisted as the job's own record (see docs/architecture/user-import.md). Track results via the workload link below or the Jobs page."
        action={
          <Link
            href="/processing"
            className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
          >
            Open Processing Center
          </Link>
        }
      />
      <UserImportWizard />
    </div>
  );
}
