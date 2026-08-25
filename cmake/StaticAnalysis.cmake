# Opt-in clang-tidy integration.
#
# Usage: cmake -B build -DFLOWFORGE_ENABLE_CLANG_TIDY=ON
# Requires clang-tidy on PATH. Off by default so a missing tool never
# breaks a plain configure/build.

if(FLOWFORGE_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" CACHE STRING "" FORCE)
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
  else()
    message(WARNING "FLOWFORGE_ENABLE_CLANG_TIDY=ON but clang-tidy was not found on PATH.")
  endif()
else()
  # CMAKE_CXX_CLANG_TIDY is a CACHE variable: once set to ON in a given
  # build directory, it persists in CMakeCache.txt across later
  # reconfigures even after FLOWFORGE_ENABLE_CLANG_TIDY is toggled back
  # OFF, silently re-enabling clang-tidy (including over third-party
  # FetchContent targets) for every subsequent build. Explicitly clear it
  # here so OFF actually means off.
  set(CMAKE_CXX_CLANG_TIDY "" CACHE STRING "" FORCE)
endif()
