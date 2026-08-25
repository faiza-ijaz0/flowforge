# Centralized compiler-warning configuration.
#
# Rationale: every first-party target should build with a strict, consistent
# warning set. Warnings are NOT escalated to hard errors globally (-Werror)
# because that makes the build brittle across compiler versions and
# third-party FetchContent code; instead CI treats new warnings on
# first-party code as review signal. Individual targets may opt into
# -Werror via FLOWFORGE_WARNINGS_AS_ERRORS.

function(flowforge_set_project_warnings target)
  set(MSVC_WARNINGS
    /W4
    /permissive-
    /w14242 /w14254 /w14263 /w14265 /w14287 /we4289
    /w14296 /w14311 /w14545 /w14546 /w14547 /w14549
    /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
  )

  set(CLANG_GCC_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
  )

  if(FLOWFORGE_WARNINGS_AS_ERRORS)
    list(APPEND MSVC_WARNINGS /WX)
    list(APPEND CLANG_GCC_WARNINGS -Werror)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE ${CLANG_GCC_WARNINGS})
  endif()
endfunction()
