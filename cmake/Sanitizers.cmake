# Opt-in sanitizer instrumentation for first-party targets.
#
# Usage: cmake -B build -DFLOWFORGE_ENABLE_ASAN=ON -DFLOWFORGE_ENABLE_UBSAN=ON
# ASan/UBSan may be combined; TSan is mutually exclusive with both (it
# instruments differently and existing races would otherwise double-report).

function(flowforge_enable_sanitizers target)
  if(NOT (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
    if(FLOWFORGE_ENABLE_ASAN OR FLOWFORGE_ENABLE_UBSAN OR FLOWFORGE_ENABLE_TSAN)
      message(WARNING "Sanitizers requested but compiler '${CMAKE_CXX_COMPILER_ID}' is not Clang/GNU; ignoring.")
    endif()
    return()
  endif()

  if(FLOWFORGE_ENABLE_TSAN AND (FLOWFORGE_ENABLE_ASAN OR FLOWFORGE_ENABLE_UBSAN))
    message(FATAL_ERROR "FLOWFORGE_ENABLE_TSAN cannot be combined with ASan/UBSan.")
  endif()

  set(sanitizers "")
  if(FLOWFORGE_ENABLE_ASAN)
    list(APPEND sanitizers "address")
  endif()
  if(FLOWFORGE_ENABLE_UBSAN)
    list(APPEND sanitizers "undefined")
  endif()
  if(FLOWFORGE_ENABLE_TSAN)
    list(APPEND sanitizers "thread")
  endif()

  if(sanitizers)
    list(JOIN sanitizers "," sanitizer_list)
    target_compile_options(${target} PRIVATE -fsanitize=${sanitizer_list} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${sanitizer_list})
  endif()
endfunction()
