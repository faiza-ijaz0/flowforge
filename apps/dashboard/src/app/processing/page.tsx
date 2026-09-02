import { PageHeader } from "@/components/ui/page-header";
import { ProcessingUploadPanel } from "@/components/processing/processing-upload-panel";

export default function ProcessingPage() {
  return (
    <div>
      <PageHeader
        title="Processing Center"
        description="Submit input from any source to any processing target. Every submission becomes a real workload, dispatched through the same PriorityScheduler/WorkerPool pipeline as every other job -- see docs/architecture/input-processing.md. Today only Users + CSV is implemented; every other combination is disabled and marked accordingly."
      />
      <ProcessingUploadPanel />
    </div>
  );
}
