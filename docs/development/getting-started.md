# Getting started

This is the contributor-facing companion to the root [README.md](../../README.md) build/test
instructions — read that first for commands. This page covers day-to-day workflow notes that don't
belong in the README.

## Windows-specific: compiler choice

If you're on Windows using the [WinLibs](https://winlibs.com/) UCRT+LLVM distribution (GCC + clang
bundled together), **configure CMake with `g++`, not `clang++`**. clang targeting
`x86_64-w64-mingw32` against this libstdc++ build has a reproducible linker bug affecting any binary
that transitively uses `std::call_once` (which `std::future`consequently `ThreadPool::submit`
uses internally) — see `docs/architecture/overview.md` §11 for the full explanation and the exact
error signature. clang is still useful on Windows for `clang-format` and `clang-tidy`; just don't use
it as `CMAKE_CXX_COMPILER` there. On Linux/macOS, clang works fine end-to-end.

## Repository layout for contributors

- Adding a new engine source file? Add it to `engine/CMakeLists.txt`'s explicit source list (not a
  glob — see the comment there for why) and mirror the `include/flowforge/<area>/` /
  `src/<area>/` split.
- Adding a new engine unit test? Add the `.cpp` to `engine/tests/CMakeLists.txt`'s source list.
  `gtest_discover_tests` picks up new `TEST`/`TEST_F` cases automatically once the file is listed.
- New HTTP route? Add a `register_*_routes` function under `apps/server/src/http/routes/`, wire it
  into `App::register_routes` (`apps/server/src/http/app.cpp`), and add coverage in
  `apps/server/tests/http_server_test.cpp`.
- New database table? Add the next-numbered file to `database/migrations/`; never edit an already
  merged migration file — add a new one that alters the table instead (see any production migration
  workflow for why: applied migrations are effectively immutable history).

## Editor setup

`compile_commands.json` is generated at `build/compile_commands.json` on every configure
(`CMAKE_EXPORT_COMPILE_COMMANDS ON` in the root `CMakeLists.txt`) — point clangd/VS Code's C++
extension at it for accurate IntelliSense/diagnostics.
