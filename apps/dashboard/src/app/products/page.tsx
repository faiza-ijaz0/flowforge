"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

import type { Product } from "@flowforge/shared";

import { Card } from "@/components/ui/card";
import { PageHeader } from "@/components/ui/page-header";
import { apiClient, ApiError } from "@/lib/api-client";

const PAGE_SIZE = 25;

function formatPrice(price: number, currency: string): string {
  try {
    return new Intl.NumberFormat(undefined, { style: "currency", currency }).format(price);
  } catch {
    // An unrecognized currency code (shouldn't happen -- the backend
    // validates it -- but Intl.NumberFormat throws on a truly malformed
    // one) falls back to a plain, still-honest rendering rather than
    // crashing the page.
    return `${price.toFixed(2)} ${currency}`;
  }
}

export default function ProductsPage() {
  const [products, setProducts] = useState<Product[]>([]);
  const [total, setTotal] = useState(0);
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    apiClient
      .listProducts(PAGE_SIZE, offset)
      .then((result) => {
        if (cancelled) return;
        setProducts(result.products);
        setTotal(result.total);
      })
      .catch((err: unknown) => {
        if (cancelled) return;
        setError(err instanceof ApiError ? err.message : "Failed to load products");
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });
    return () => {
      cancelled = true;
    };
  }, [offset]);

  const rangeStart = total === 0 ? 0 : offset + 1;
  const rangeEnd = Math.min(offset + PAGE_SIZE, total);

  return (
    <div>
      <PageHeader
        title="Products"
        description="Real, persisted product records. Every row here was written by handlers::ProductProcessHandler when its product.process job executed -- see docs/architecture/product-processing.md. Import more via the Processing Center."
        action={
          <Link
            href="/processing"
            className="shrink-0 rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5"
          >
            Open Processing Center
          </Link>
        }
      />

      {error && (
        <Card className="mb-4 border-red-500/30">
          <div className="text-sm text-red-400">{error}</div>
        </Card>
      )}

      {loading && (
        <Card>
          <div className="flex items-center gap-3 text-sm text-[var(--muted)]">
            <span className="h-4 w-4 shrink-0 animate-spin rounded-full border-2 border-[var(--border)] border-t-[var(--accent)]" />
            Loading products…
          </div>
        </Card>
      )}

      {!loading && !error && products.length === 0 && (
        <Card>
          <div className="text-sm text-[var(--muted)]">
            No products yet.{" "}
            <Link href="/processing" className="text-[var(--accent)] hover:underline">
              Import a CSV or image
            </Link>{" "}
            to get started.
          </div>
        </Card>
      )}

      {!loading && !error && products.length > 0 && (
        <>
          <Card className="p-0">
            <div className="overflow-x-auto">
              <table className="w-full text-left text-sm">
                <thead>
                  <tr className="border-b border-[var(--border)] text-xs uppercase tracking-wide text-[var(--muted)]">
                    <th className="px-4 py-3 font-medium">SKU</th>
                    <th className="px-4 py-3 font-medium">Name</th>
                    <th className="px-4 py-3 font-medium">Price</th>
                    <th className="px-4 py-3 font-medium">Category</th>
                    <th className="px-4 py-3 font-medium">Stock</th>
                    <th className="px-4 py-3 font-medium">Workload</th>
                    <th className="px-4 py-3 font-medium">Updated</th>
                  </tr>
                </thead>
                <tbody>
                  {products.map((product) => (
                    <tr key={product.id} className="border-b border-[var(--border)] last:border-0 hover:bg-white/5">
                      <td className="px-4 py-3 font-mono text-xs">{product.sku}</td>
                      <td className="px-4 py-3">{product.name}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">
                        {formatPrice(product.price, product.currency)}
                      </td>
                      <td className="px-4 py-3 text-[var(--muted)]">{product.category ?? "—"}</td>
                      <td className="px-4 py-3 text-[var(--muted)]">{product.stock_quantity}</td>
                      <td className="px-4 py-3">
                        {product.job_id ? (
                          <span className="font-mono text-xs text-[var(--muted)]" title={product.job_id}>
                            {product.job_id.slice(0, 8)}…
                          </span>
                        ) : (
                          <span className="text-[var(--muted)]">—</span>
                        )}
                      </td>
                      <td className="px-4 py-3 text-[var(--muted)]">
                        {new Date(product.updated_at).toLocaleString()}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </Card>

          <div className="mt-4 flex items-center justify-between text-sm text-[var(--muted)]">
            <span>
              Showing {rangeStart}-{rangeEnd} of {total}
            </span>
            <div className="flex gap-2">
              <button
                type="button"
                onClick={() => setOffset(Math.max(0, offset - PAGE_SIZE))}
                disabled={offset === 0}
                className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
              >
                Previous
              </button>
              <button
                type="button"
                onClick={() => setOffset(offset + PAGE_SIZE)}
                disabled={rangeEnd >= total}
                className="rounded-md border border-[var(--border)] px-3 py-1.5 text-sm font-medium hover:bg-white/5 disabled:cursor-not-allowed disabled:opacity-50"
              >
                Next
              </button>
            </div>
          </div>
        </>
      )}
    </div>
  );
}
