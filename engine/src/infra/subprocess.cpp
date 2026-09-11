#include "flowforge/infra/subprocess.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <thread>

extern char** environ;
#endif

namespace flowforge::infra {

namespace {

#ifdef _WIN32

/// Quotes one argument per the documented Win32 `CommandLineToArgvW`
/// convention (the same algorithm every conforming Windows argv consumer,
/// including CreateProcess's own child-side parsing, expects) -- doubling
/// backslashes that immediately precede a quote (or the end of the
/// string, since the whole argument is itself wrapped in a trailing
/// quote), and escaping embedded quotes. Without this, an argument
/// containing a space or quote would be mis-split by the child process.
std::string quote_windows_arg(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
    return arg;
  }
  std::string quoted = "\"";
  std::size_t backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    if (c == '"') {
      quoted.append(backslashes * 2 + 1, '\\');
      backslashes = 0;
      quoted += '"';
      continue;
    }
    quoted.append(backslashes, '\\');
    backslashes = 0;
    quoted += c;
  }
  quoted.append(backslashes * 2, '\\');
  quoted += '"';
  return quoted;
}

Result<SubprocessResult> run_subprocess_impl(const std::string& executable,
                                             const std::vector<std::string>& args,
                                             std::chrono::milliseconds timeout) {
  std::string command_line = quote_windows_arg(executable);
  for (const auto& arg : args) {
    command_line += ' ';
    command_line += quote_windows_arg(arg);
  }

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};

  // lpCommandLine must be a mutable buffer -- CreateProcess is permitted
  // to write into it (e.g. to split arguments in place).
  std::vector<char> mutable_command_line(command_line.begin(), command_line.end());
  mutable_command_line.push_back('\0');

  const BOOL created = CreateProcessA(
      /*lpApplicationName=*/nullptr, mutable_command_line.data(), /*lpProcessAttributes=*/nullptr,
      /*lpThreadAttributes=*/nullptr, /*bInheritHandles=*/FALSE, /*dwCreationFlags=*/CREATE_NO_WINDOW,
      /*lpEnvironment=*/nullptr, /*lpCurrentDirectory=*/nullptr, &startup_info, &process_info);
  if (!created) {
    return std::unexpected(
        make_error(ErrorCode::Infrastructure, "failed to start process '" + executable + "'"));
  }
  CloseHandle(process_info.hThread);

  const DWORD wait_result = WaitForSingleObject(process_info.hProcess, static_cast<DWORD>(timeout.count()));
  SubprocessResult result;
  if (wait_result == WAIT_TIMEOUT) {
    TerminateProcess(process_info.hProcess, 1);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    result.timed_out = true;
  } else {
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);
  }
  CloseHandle(process_info.hProcess);
  return result;
}

#else

Result<SubprocessResult> run_subprocess_impl(const std::string& executable,
                                             const std::vector<std::string>& args,
                                             std::chrono::milliseconds timeout) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(executable.c_str()));
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid = 0;
  const int spawn_status = posix_spawnp(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
  if (spawn_status != 0) {
    return std::unexpected(
        make_error(ErrorCode::Infrastructure, "failed to start process '" + executable + "'"));
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  SubprocessResult result;
  for (;;) {
    int status = 0;
    const pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      result.timed_out = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return result;
}

#endif

}  // namespace

Result<SubprocessResult> run_subprocess(const std::string& executable, const std::vector<std::string>& args,
                                        std::chrono::milliseconds timeout) {
  return run_subprocess_impl(executable, args, timeout);
}

}  // namespace flowforge::infra
