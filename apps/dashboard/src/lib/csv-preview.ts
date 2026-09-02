/**
 * Client-side, non-authoritative CSV preview -- gives the user instant
 * feedback (row count, a bounded preview table, obvious per-row problems)
 * before they click "Start Import". The C++ server
 * (flowforge::services::parse_user_import_csv) is the single source of
 * truth for what actually gets imported: it re-parses and re-validates
 * the same file independently, using the same column/limit rules, and its
 * response is what the UI trusts after upload -- see
 * docs/architecture/user-import.md, "CSV contract". Any discrepancy
 * between this preview and the server's real result is expected and
 * harmless (this preview may under- or over-reject a truly borderline
 * row); it exists only for responsiveness, not enforcement.
 */

const REQUIRED_COLUMNS = ["name", "email"] as const;
const OPTIONAL_COLUMNS = ["phone"] as const;
const MAX_PREVIEW_ROWS = 20;
/** Above this, skip client-side parsing entirely -- the server's own,
 *  authoritative 2 MiB bound will reject an oversized file anyway; this
 *  just avoids a janky UI hang parsing a huge file for a preview nobody
 *  will read in full. */
const MAX_PREVIEWABLE_BYTES = 5 * 1024 * 1024;

export interface CsvPreviewRow {
  rowNumber: number;
  name: string;
  email: string;
  phone: string;
  valid: boolean;
  reason?: string;
}

export interface CsvPreview {
  totalRows: number;
  validRows: number;
  invalidRows: number;
  /** Bounded to MAX_PREVIEW_ROWS -- see the table in UserImportWizard. */
  previewRows: CsvPreviewRow[];
  /** Set when the file/header itself is unusable -- row counts are meaningless then. */
  fileError?: string;
}

/** Too large to preview client-side; the upload can still proceed. */
export interface CsvPreviewSkipped {
  skipped: true;
  reason: string;
}

function tokenizeCsv(content: string): string[][] {
  const rows: string[][] = [];
  let field = "";
  let row: string[] = [];
  let inQuotes = false;
  let i = 0;
  const n = content.length;

  const endField = () => {
    row.push(field);
    field = "";
  };
  const endRow = () => {
    endField();
    if (!(row.length === 1 && row[0] === "")) {
      rows.push(row);
    }
    row = [];
  };

  while (i < n) {
    const c = content[i];
    if (inQuotes) {
      if (c === '"') {
        if (content[i + 1] === '"') {
          field += '"';
          i += 2;
          continue;
        }
        inQuotes = false;
        i += 1;
        continue;
      }
      field += c;
      i += 1;
      continue;
    }
    if (c === '"' && field === "") {
      inQuotes = true;
      i += 1;
      continue;
    }
    if (c === ",") {
      endField();
      i += 1;
      continue;
    }
    if (c === "\r") {
      if (content[i + 1] === "\n") {
        endRow();
        i += 2;
        continue;
      }
      endRow();
      i += 1;
      continue;
    }
    if (c === "\n") {
      endRow();
      i += 1;
      continue;
    }
    field += c;
    i += 1;
  }
  if (field !== "" || row.length > 0) {
    endRow();
  }
  return rows;
}

const EMAIL_PATTERN = /^[^@\s]+@[^@\s]+\.[^@\s]+$/;

export function previewCsv(content: string): CsvPreview {
  const rows = tokenizeCsv(content);
  const headerRow = rows[0];
  if (rows.length === 0 || !headerRow) {
    return { totalRows: 0, validRows: 0, invalidRows: 0, previewRows: [], fileError: "File is empty." };
  }

  const header = headerRow.map((column) => column.trim());
  const nameIndex = header.indexOf("name");
  const emailIndex = header.indexOf("email");
  const phoneIndex = header.indexOf("phone");
  const knownColumns = new Set<string>([...REQUIRED_COLUMNS, ...OPTIONAL_COLUMNS]);
  const unknownColumn = header.find((column) => !knownColumns.has(column));

  if (nameIndex === -1 || emailIndex === -1) {
    return {
      totalRows: 0,
      validRows: 0,
      invalidRows: 0,
      previewRows: [],
      fileError: "CSV header must include 'name' and 'email' columns.",
    };
  }
  if (unknownColumn) {
    return {
      totalRows: 0,
      validRows: 0,
      invalidRows: 0,
      previewRows: [],
      fileError: `Unexpected column '${unknownColumn}' -- only name, email, phone are supported.`,
    };
  }

  const dataRows = rows.slice(1);
  const previewRows: CsvPreviewRow[] = [];
  const seenEmails = new Set<string>();
  let validRows = 0;

  dataRows.forEach((fields, index) => {
    const rowNumber = index + 1;
    const name = (fields[nameIndex] ?? "").trim();
    const email = (fields[emailIndex] ?? "").trim().toLowerCase();
    const phone = phoneIndex >= 0 ? (fields[phoneIndex] ?? "").trim() : "";

    let reason: string | undefined;
    if (fields.length !== header.length) {
      reason = `expected ${header.length} fields, found ${fields.length}`;
    } else if (!name) {
      reason = "name is required";
    } else if (!email || !EMAIL_PATTERN.test(email)) {
      reason = "email is invalid";
    } else if (seenEmails.has(email)) {
      reason = "duplicate email";
    }

    const valid = reason === undefined;
    if (valid) {
      validRows += 1;
      seenEmails.add(email);
    }
    if (previewRows.length < MAX_PREVIEW_ROWS) {
      previewRows.push({ rowNumber, name, email, phone, valid, reason });
    }
  });

  return {
    totalRows: dataRows.length,
    validRows,
    invalidRows: dataRows.length - validRows,
    previewRows,
  };
}

export function previewCsvFile(file: File): Promise<CsvPreview | CsvPreviewSkipped> {
  if (file.size > MAX_PREVIEWABLE_BYTES) {
    return Promise.resolve({
      skipped: true,
      reason: "File is large -- skipping the local preview. The server will validate it on import.",
    });
  }
  return file.text().then(previewCsv);
}
