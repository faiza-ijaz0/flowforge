import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";

export default function QueuesPage() {
  return (
    <div>
      <PageHeader
        title="Queues"
        description="Logical job queues defined in the database."
      />
      <NotYetImplemented
        feature="Queue management"
        note="There is no queue API yet. Queues are defined by database seed data (database/seeds/dev_seed.sql)."
      />
    </div>
  );
}
