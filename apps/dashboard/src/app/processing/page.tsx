import { PageHeader } from "@/components/ui/page-header";
import { ProcessingUploadPanel } from "@/components/processing/processing-upload-panel";

export default function ProcessingPage() {
  return (
    <div>
      <PageHeader
        title="Processing Center"
        description="Upload a CSV file or an image of a table and turn it into a workload of processing jobs. Users, Products, and Categories accept CSV, image, and screenshot input. Every combination except a Users CSV shows the extracted records for review first, and nothing is created until you confirm."
      />
      <ProcessingUploadPanel />
    </div>
  );
}
