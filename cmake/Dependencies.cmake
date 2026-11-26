# Third-party code, pinned. Everything comes through FetchContent so a fresh clone needs no
# system packages, and the same versions build natively and under Emscripten.
include(FetchContent)

# miniz (MIT): its inflate and deflate code, for compressed draw.io pages, PNG text chunks,
# gzip and packed libraries (.ddlib).
# Its CMake build generates an export header, so we compile the sources ourselves with a stub.
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/archive/refs/tags/3.0.2.tar.gz
  URL_HASH SHA256=c4b4c25a4eb81883448ff8924e6dba95c800094a198dc9ce66a292ac2ef8e018
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(miniz)
file(WRITE ${CMAKE_BINARY_DIR}/miniz-export/miniz_export.h "#pragma once\n#define MINIZ_EXPORT\n")
add_library(miniz_inflate STATIC ${miniz_SOURCE_DIR}/miniz.c ${miniz_SOURCE_DIR}/miniz_tinfl.c
            ${miniz_SOURCE_DIR}/miniz_tdef.c)
target_include_directories(miniz_inflate SYSTEM PUBLIC ${miniz_SOURCE_DIR} ${CMAKE_BINARY_DIR}/miniz-export)
target_compile_definitions(miniz_inflate PUBLIC MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_APIS)

# lunasvg (MIT, with its bundled plutovg): the PNG backend of native builds. The backend is a
# build option, so another rasteriser can replace it (see include/diadraw/export/export.hpp).
if(DIADRAW_PNG_BACKEND STREQUAL "lunasvg" AND NOT EMSCRIPTEN)
  FetchContent_Declare(lunasvg
    URL https://github.com/sammycage/lunasvg/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=1abf1472ee6c4d19797916e8cc3c2e4b628e0d81178ffac60bdb0d457e32c690
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)  # linked statically; its install rules stay out of DiaDraw's packages
  set(BUILD_SHARED_LIBS OFF)
  set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(lunasvg)
endif()

if(NOT DIADRAW_BUILD_TESTS)
  return()
endif()

FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
