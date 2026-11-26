cmake_minimum_required(VERSION 3.25)

# The version lives in VERSION, the one source for the core, the apps, the npm packages and the site
# (tools/check-version.mjs keeps the rest in step).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/VERSION" DIADRAW_VERSION LIMIT_COUNT 1)
if(NOT DIADRAW_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must hold MAJOR.MINOR.PATCH, not '${DIADRAW_VERSION}'")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/VERSION")
project(diadraw VERSION ${DIADRAW_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DIADRAW_BUILD_TESTS "Build unit tests" ON)
option(DIADRAW_BUILD_CLI "Build the diadraw command-line tool" ON)
option(DIADRAW_BUILD_MACOS "Build the macOS app (apps/macos)" ${APPLE})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(DIADRAW_GTK_DEFAULT ON)
else()
  set(DIADRAW_GTK_DEFAULT OFF)
endif()
option(DIADRAW_BUILD_GTK "Build the gtkmm app (apps/gtk); needs gtkmm-4.0" ${DIADRAW_GTK_DEFAULT})
option(DIADRAW_BUILD_WINUI "Build the WinUI 3 app (apps/winui); Windows and MSVC only" OFF)
option(DIADRAW_SANITIZE "Build with address and undefined-behaviour sanitizers" OFF)
option(DIADRAW_BUILD_PYTHON "Build the Python extension module (src/python); needs nanobind" OFF)
# The Node.js addon (src/node); configured by cmake-js through `npm run build -w @diadraw/node`.
option(DIADRAW_BUILD_NODE "Build the Node.js addon (src/node); needs cmake-js" OFF)
if(DIADRAW_BUILD_PYTHON OR DIADRAW_BUILD_NODE)
  # The static libraries end up inside a shared extension module or addon.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
# PNG and PDF export backends (native builds). Each is one source file behind the interfaces in
# src/export/include/diadraw/export/export.hpp, so it can be swapped for another implementation.
set(DIADRAW_PNG_BACKEND "lunasvg" CACHE STRING "PNG rasteriser: lunasvg or none")
set_property(CACHE DIADRAW_PNG_BACKEND PROPERTY STRINGS lunasvg none)
set(DIADRAW_PDF_BACKEND "builtin" CACHE STRING "PDF writer: builtin or none")
set_property(CACHE DIADRAW_PDF_BACKEND PROPERTY STRINGS builtin none)

if(EMSCRIPTEN)
  set(DIADRAW_BUILD_TESTS OFF)
  set(DIADRAW_BUILD_CLI OFF)
  set(DIADRAW_BUILD_MACOS OFF)
  # Native WebAssembly exceptions: the core reports malformed input (e.g. a regular expression
  # built from a PlantUML macro) by throwing, and the bindings catch it.
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
endif()

if(DIADRAW_BUILD_NODE)
  # cmake-js also links the static C runtime on Windows, as node.exe does, which the WinUI app
  # cannot use; the addon is built on its own.
  if(DIADRAW_BUILD_WINUI)
    message(FATAL_ERROR "DIADRAW_BUILD_NODE and DIADRAW_BUILD_WINUI cannot be on in one build")
  endif()
endif()

add_library(diadraw_warnings INTERFACE)
if(MSVC)
  # Sources are UTF-8; the generated tables make large objects.
  add_compile_options(/utf-8 /bigobj /Zc:__cplusplus)
  target_compile_options(diadraw_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(diadraw_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
                                                    -Wno-sign-conversion)
endif()

if(DIADRAW_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake")

# ── Core library ────────────────────────────────────────────────────────────────────────────────
add_library(diadraw STATIC)
add_library(diadraw::diadraw ALIAS diadraw)
target_include_directories(diadraw PUBLIC $<INSTALL_INTERFACE:include>)
target_compile_definitions(diadraw PRIVATE DIADRAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(diadraw PRIVATE diadraw_warnings miniz_inflate)

# ── WASM module ─────────────────────────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
  add_executable(diadraw_wasm src/wasm/src/bindings.cpp)
  set_target_properties(diadraw_wasm PROPERTIES OUTPUT_NAME diadraw SUFFIX ".mjs")
  target_link_libraries(diadraw_wasm PRIVATE diadraw diadraw_warnings)
  target_link_options(diadraw_wasm PRIVATE
    -lembind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createDiaDraw
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=8MB
    -sFILESYSTEM=0
    --emit-tsd=diadraw.d.ts)
endif()

# ── PNG and PDF export (native) ─────────────────────────────────────────────────────────────────
if(NOT EMSCRIPTEN)
  add_library(diadraw_export STATIC src/export/src/export.cpp
                                    src/export/src/png_${DIADRAW_PNG_BACKEND}.cpp
                                    src/export/src/pdf_${DIADRAW_PDF_BACKEND}.cpp)
  target_link_libraries(diadraw_export PUBLIC diadraw PRIVATE diadraw_warnings miniz_inflate)
  if(DIADRAW_PNG_BACKEND STREQUAL "lunasvg")
    target_link_libraries(diadraw_export PRIVATE lunasvg::lunasvg)
  endif()
endif()

# ── CLI ─────────────────────────────────────────────────────────────────────────────────────────
if(DIADRAW_BUILD_CLI)
  add_executable(diadraw_cli src/apps/cli/src/main.cpp src/apps/cli/src/lsp.cpp src/apps/cli/src/mcp.cpp)
  set_target_properties(diadraw_cli PROPERTIES OUTPUT_NAME diadraw)
  if(WIN32)
    # Arguments and paths arrive as UTF-8, as on every other platform (host/files.hpp).
    target_sources(diadraw_cli PRIVATE src/apps/cli/cli.manifest)
  endif()
  target_link_libraries(diadraw_cli PRIVATE diadraw diadraw_host diadraw_export diadraw_warnings)
  # The CLI goes into the Linux and Windows packages (the macOS DMG holds the app only); on
  # Windows it sits beside DiaDraw.exe.
  if(WIN32)
    install(TARGETS diadraw_cli RUNTIME DESTINATION . COMPONENT cli)
  elseif(NOT APPLE)
    install(TARGETS diadraw_cli RUNTIME DESTINATION bin COMPONENT cli)
  endif()
endif()

# ── Tests ───────────────────────────────────────────────────────────────────────────────────────
if(DIADRAW_BUILD_TESTS)
  enable_testing()
  add_executable(diadraw_tests
    src/base/tests/json_test.cpp
    src/base/tests/js_regex_test.cpp
    src/base/tests/text_test.cpp
    src/base/tests/ordered_map_test.cpp
    src/dsl/tests/highlight_test.cpp
    src/canvas/tests/canvas_test.cpp
    src/library/tests/library_test.cpp
    src/export/tests/export_test.cpp
    src/model/tests/operations_test.cpp
    src/model/tests/history_test.cpp
    src/model/tests/views_test.cpp
    src/capi/tests/capi_test.cpp
    src/capi/tests/capi_diagram_test.cpp
    src/dsl/tests/parse_test.cpp
    src/dsl/tests/patch_test.cpp
    src/layout/tests/layout_test.cpp
    src/layout/tests/engines_test.cpp
    src/layout/tests/json_test.cpp
    src/editor/tests/editor_test.cpp
    src/io/tests/io_test.cpp
    src/lsp/tests/lsp_test.cpp
    src/library/tests/kinds_test.cpp
    src/dsl/tests/patch_property_test.cpp
    src/dsl/tests/style_test.cpp
    src/dsl/tests/decorations_test.cpp
    src/dsl/tests/element_update_test.cpp
    src/dsl/tests/style_update_test.cpp
    src/dsl/tests/relationship_update_test.cpp
    src/dsl/tests/reconnect_test.cpp
    src/dsl/tests/rename_element_test.cpp
  )
  target_link_libraries(diadraw_tests PRIVATE diadraw diadraw_capi diadraw_export GTest::gtest_main diadraw_warnings
                        miniz_inflate)
  target_compile_definitions(diadraw_tests PRIVATE DIADRAW_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
  target_include_directories(diadraw_tests PRIVATE src/tests)
  include(GoogleTest)
  gtest_discover_tests(diadraw_tests)
endif()

add_subdirectory(src)

# ── Packages (CPack) ─────────────────────────────────────────────────────────────────────────────
if(NOT EMSCRIPTEN AND (DIADRAW_BUILD_MACOS OR DIADRAW_BUILD_GTK OR DIADRAW_BUILD_WINUI))
  include("${CMAKE_CURRENT_LIST_DIR}/cmake/Packaging.cmake")
endif()
