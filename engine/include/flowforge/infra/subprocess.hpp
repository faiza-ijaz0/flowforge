#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "flowforge/result.hpp"

namespace flowforge::infra {

/// Outcome of one bounded, non-shell child-process run (see
/// `run_subprocess`). `exit_code` is meaningless when `timed_out` is
/// true -- the process was killed before it could exit normally.
struct SubprocessResult {
  int exit_code = -1;
  bool timed_out = false;
};

/// Runs `executable` with `args` as a child process and waits up to
/// `timeout` for it to exit, killing it if the deadline passes. Used by
/// `providers::TesseractCliOcrProvider` (Phase 3D-1 -- see
/// docs/architecture/input-processing.md, "Image extraction") to invoke
/// the Tesseract OCR binary as an external tool rather than linking
/// against it, but is itself provider-agnostic -- any future extraction
/// provider that shells out to a CLI tool reuses this.
///
/// Deliberately never goes through a shell (no `/bin/sh -c`, no
/// `cmd.exe /c`): `executable` and each element of `args` are passed
/// directly to the OS's process-creation API as a literal argv, so shell
/// metacharacters in either have no special meaning and command
/// injection is not a category of bug this function can have -- see
/// docs/architecture/input-processing.md, "Security: subprocess
/// execution".
///
/// Returns an `ErrorCode::Infrastructure` `Result` error only when the
/// process could not be *started* at all (executable missing, not
/// executable, OS resource exhaustion) -- a process that starts and exits
/// non-zero is a normal, successful call whose failure is reported via
/// `SubprocessResult::exit_code`, not a `Result` error, so a caller can
/// distinguish "the OCR engine itself is unavailable" from "the OCR
/// engine ran and reported a problem with this particular image".
[[nodiscard]] Result<SubprocessResult> run_subprocess(const std::string& executable,
                                                      const std::vector<std::string>& args,
                                                      std::chrono::milliseconds timeout);

}  // namespace flowforge::infra
