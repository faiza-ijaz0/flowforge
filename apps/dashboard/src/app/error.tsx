"use client";

import Link from "next/link";
import { useEffect } from "react";

export default function RouteError({ error, reset }: { error: Error & { digest?: string }; reset: () => void }) {
  useEffect(() => {
    // Client-side-only diagnostic -- never sent anywhere, just visible in
    // the browser devtools console for whoever is debugging this session.
    console.error(error);
  }, [error]);

  return (
    <div className="flex h-full flex-col items-center justify-center gap-3 py-24 text-center">
      <div className="text-lg font-semibold">Something went wrong</div>
      <p className="max-w-md text-sm text-[var(--muted)]">
        {error.message || "An unexpected error occurred while loading this page."}
      </p>
      <div className="mt-2 flex flex-wrap items-center justify-center gap-2">
        <button
          type="button"
          onClick={reset}
          className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-medium text-white"
        >
          Retry
        </button>
        <Link
          href="/"
          className="rounded-md border border-[var(--border)] px-4 py-2 text-sm font-medium hover:bg-white/5"
        >
          Back to Overview
        </Link>
        <Link
          href="/processing"
          className="rounded-md border border-[var(--border)] px-4 py-2 text-sm font-medium hover:bg-white/5"
        >
          Return to Processing Center
        </Link>
      </div>
    </div>
  );
}
