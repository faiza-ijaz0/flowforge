/**
 * Mirrors the `checks` object apps/server/src/http/routes/health_routes.cpp's
 * GET /ready returns. Each check is a cheap in-memory/non-blocking read on
 * the server -- never a live database query -- so this is safe to poll
 * frequently. "unavailable" on any one check makes the whole response come
 * back as HTTP 503 (still a well-formed body, not an error page).
 */
export interface ReadinessChecks {
  database: "ok" | "unavailable";
  scheduler: "ok" | "unavailable";
  worker_pool: "ok" | "unavailable";
  retry_dispatcher: "ok" | "unavailable";
}

export interface ReadyResponse {
  status: "ok" | "unavailable";
  environment: string;
  uptime_seconds: number;
  checks: ReadinessChecks;
}
