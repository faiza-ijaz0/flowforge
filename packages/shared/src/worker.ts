/** Mirrors flowforge::domain::WorkerStatus (engine/include/flowforge/domain/worker.hpp). */
export type WorkerStatus = "idle" | "busy" | "offline";

export interface Worker {
  id: string;
  hostname: string;
  status: WorkerStatus;
  registered_at: string;
  last_heartbeat: string;
}

export interface ListWorkersResponse {
  workers: Worker[];
}
