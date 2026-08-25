/**
 * Client-visible configuration. Only NEXT_PUBLIC_-prefixed variables are
 * exposed to the browser bundle by Next.js -- this file is the single
 * place that reads them, so nothing else needs to know that constraint.
 */
export const apiBaseUrl: string = process.env.NEXT_PUBLIC_API_URL ?? "http://localhost:8080";
