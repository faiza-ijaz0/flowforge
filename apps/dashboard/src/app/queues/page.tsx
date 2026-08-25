import { NotYetImplemented } from "@/components/ui/not-yet-implemented";
import { PageHeader } from "@/components/ui/page-header";

export default function QueuesPage() {
  return (
    <div>
      <PageHeader
        title="Queues"
        description="Logical queue configuration (see database/migrations/0003_create_queues.sql and domain::QueueConfig)."
      />
      <NotYetImplemented
        feature="Queue management"
        note="There is no /api/v1/queues endpoint yet -- queues currently only exist as rows seeded via database/seeds/dev_seed.sql."
      />
    </div>
  );
}
