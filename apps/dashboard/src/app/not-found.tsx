import Link from "next/link";

export default function NotFound() {
  return (
    <div className="flex h-full flex-col items-center justify-center gap-2 py-24 text-center">
      <div className="text-lg font-semibold">Not found</div>
      <p className="text-sm text-[var(--muted)]">The resource you&apos;re looking for doesn&apos;t exist.</p>
      <Link href="/" className="mt-2 text-sm text-[var(--accent)] underline underline-offset-2">
        Back to Overview
      </Link>
    </div>
  );
}
