/**
 * A persisted user record -- mirrors flowforge::domain::User as serialized
 * by apps/server/src/json/user_json.cpp. Written only by
 * handlers::UserProcessHandler at job-execution time (Phase 3H); see
 * docs/architecture/phase-3h-production-readiness.md for why Users gained
 * the same dedicated persistence Products/Categories already had.
 */
export interface User {
  id: string;
  name: string;
  email: string;
  phone: string | null;
  job_id: string | null;
  created_at: string;
  updated_at: string;
}

export interface ListUsersResponse {
  users: User[];
  total: number;
  limit: number;
  offset: number;
}
