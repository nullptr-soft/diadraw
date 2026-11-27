# DiaDraw for Windows: a WinUI 3 shell (C++/WinRT, code only, no XAML files) over the C++ core.
# Built with the `windows` preset (DIADRAW_BUILD_WINUI) in a Visual Studio developer
# prompt: diadraw-build/windows/apps/winui/DiaDraw.exe. See README.md here.
#
# The NuGet packages it needs are downloaded at configure time, and the C++/WinRT projection
# headers are generated from their metadata, so no Visual Studio project or NuGet client is used.

set(DIADRAW_CPPWINRT_VERSION "2.0.240405.15" CACHE STRING "Microsoft.Windows.CppWinRT")
set(DIADRAW_WINDOWSAPPSDK_VERSION "1.6.241114003" CACHE STRING "Microsoft.WindowsAppSDK")
set(DIADRAW_WIN2D_VERSION "1.3.0" CACHE STRING "Microsoft.Graphics.Win2D")
set(DIADRAW_WEBVIEW2_VERSION "1.0.2651.64" CACHE STRING "Microsoft.Web.WebView2 (referenced by WinUI)")

set(NUGET_DIR "${CMAKE_BINARY_DIR}/nuget")

# Downloads and unpacks a NuGet package; sets <var> to its folder.
function(diadraw_nuget var id version)
  set(dir "${NUGET_DIR}/${id}.${version}")
  if(NOT EXISTS "${dir}/.unpacked")
    set(archive "${NUGET_DIR}/${id}.${version}.zip")
    message(STATUS "Downloading NuGet package ${id} ${version}")
    file(DOWNLOAD "https://www.nuget.org/api/v2/package/${id}/${version}" "${archive}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
      message(FATAL_ERROR "Could not download ${id} ${version}: ${status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
    file(TOUCH "${dir}/.unpacked")
  endif()
  set(${var} "${dir}" PARENT_SCOPE)
endfunction()

diadraw_nuget(CPPWINRT Microsoft.Windows.CppWinRT ${DIADRAW_CPPWINRT_VERSION})
diadraw_nuget(WASDK Microsoft.WindowsAppSDK ${DIADRAW_WINDOWSAPPSDK_VERSION})
diadraw_nuget(WIN2D Microsoft.Graphics.Win2D ${DIADRAW_WIN2D_VERSION})
diadraw_nuget(WEBVIEW2 Microsoft.Web.WebView2 ${DIADRAW_WEBVIEW2_VERSION})

if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64")
  set(ARCH arm64)
else()
  set(ARCH x64)
endif()

# Metadata to project: the Windows SDK, the Windows App SDK (WinUI), Win2D, and WebView2 (WinUI's
# controls header includes its projection).
file(GLOB WASDK_WINMDS "${WASDK}/lib/uap10.0*/*.winmd")
file(GLOB WIN2D_WINMDS "${WIN2D}/lib/uap10.0*/*.winmd")
file(GLOB_RECURSE WEBVIEW2_WINMDS "${WEBVIEW2}/lib/*.winmd")
if(NOT WASDK_WINMDS OR NOT WIN2D_WINMDS OR NOT WEBVIEW2_WINMDS)
  message(FATAL_ERROR "Package metadata not found; check the package layouts under ${NUGET_DIR}")
endif()
list(GET WEBVIEW2_WINMDS 0 WEBVIEW2_WINMD)

set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CPPWINRT_EXE "${CPPWINRT}/bin/cppwinrt.exe")
set(cppwinrt_inputs)
foreach(winmd IN LISTS WASDK_WINMDS WIN2D_WINMDS WEBVIEW2_WINMD)
  list(APPEND cppwinrt_inputs -input "${winmd}")
endforeach()
add_custom_command(
  OUTPUT "${GENERATED}/winrt/base.h"
  # -base: write winrt/base.h too, so the SDK's older copy is not picked up.
  COMMAND "${CPPWINRT_EXE}" -input sdk ${cppwinrt_inputs} -base -output "${GENERATED}"
  DEPENDS ${WASDK_WINMDS} ${WIN2D_WINMDS} ${WEBVIEW2_WINMD}
  COMMENT "Generating C++/WinRT headers"
  VERBATIM)
add_custom_target(diadraw_winui_projection DEPENDS "${GENERATED}/winrt/base.h")

add_executable(diadraw_winui WIN32
  src/main.cpp
  src/window.cpp
  src/canvas_view.cpp
  src/painter.cpp
  src/files.cpp
  src/cloud_window.cpp
  src/text_folds.cpp
  app.manifest
  app.rc)
set_target_properties(diadraw_winui PROPERTIES OUTPUT_NAME DiaDraw)
add_dependencies(diadraw_winui diadraw_winui_projection)
target_include_directories(diadraw_winui PRIVATE "${GENERATED}" "${WASDK}/include" "${WIN2D}/include")
target_compile_options(diadraw_winui PRIVATE /EHsc /permissive- /Zc:preprocessor)
target_compile_definitions(diadraw_winui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(diadraw_winui PRIVATE diadraw diadraw_host diadraw_cloud diadraw_export
  WindowsApp.lib
  "${WASDK}/lib/win10-${ARCH}/Microsoft.WindowsAppRuntime.Bootstrap.lib"
  ole32 shell32 comdlg32)

# Next to the executable: the bootstrapper, Win2D, and draw.io's stencil sets.
add_custom_command(TARGET diadraw_winui POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${WASDK}/runtimes/win-${ARCH}/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
          "${WIN2D}/runtimes/win-${ARCH}/native/Microsoft.Graphics.Canvas.dll"
          "$<TARGET_FILE_DIR:diadraw_winui>")
set(DIADRAW_DRAWIO_SETS "${PROJECT_SOURCE_DIR}/assets/drawio")
if(EXISTS "${DIADRAW_DRAWIO_SETS}/index.json.gz")
  add_custom_command(TARGET diadraw_winui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${DIADRAW_DRAWIO_SETS}" "$<TARGET_FILE_DIR:diadraw_winui>/drawio")
endif()

# Installed layout (the portable ZIP, and the MSIX's contents): everything in one folder.
install(TARGETS diadraw_winui RUNTIME DESTINATION . COMPONENT app)
install(FILES
  "${WASDK}/runtimes/win-${ARCH}/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
  "${WIN2D}/runtimes/win-${ARCH}/native/Microsoft.Graphics.Canvas.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/../icons/DiaDraw.ico"
  "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/docs/credits.md"
  DESTINATION . COMPONENT app)
if(EXISTS "${DIADRAW_DRAWIO_SETS}/index.json.gz")
  install(DIRECTORY "${DIADRAW_DRAWIO_SETS}/" DESTINATION drawio COMPONENT app)
  install(FILES "${PROJECT_SOURCE_DIR}/assets/NOTICE" DESTINATION drawio COMPONENT app)
endif()

# The MSIX manifest, with the Windows App Runtime framework this build needs.
set(DIADRAW_MSIX_PUBLISHER "CN=DiaDraw" CACHE STRING "MSIX publisher: the subject of the signing certificate")
set(DIADRAW_MSIX_VERSION "${PROJECT_VERSION}.0")
file(READ "${WASDK}/include/WindowsAppSDK-VersionInfo.h" version_info)
string(REGEX MATCH "WINDOWSAPPSDK_RELEASE_MAJOR[ \t]+([0-9]+)" _ "${version_info}")
set(wasdk_major "${CMAKE_MATCH_1}")
string(REGEX MATCH "WINDOWSAPPSDK_RELEASE_MINOR[ \t]+([0-9]+)" _ "${version_info}")
set(wasdk_minor "${CMAKE_MATCH_1}")
string(REGEX MATCH "WINDOWSAPPSDK_RUNTIME_VERSION_DOTQUADSTRING[ \t]+\"([0-9.]+)\"" _ "${version_info}")
set(WASDK_RUNTIME_VERSION "${CMAKE_MATCH_1}")
if(NOT wasdk_major OR NOT WASDK_RUNTIME_VERSION)
  message(WARNING "Could not read the Windows App Runtime version from WindowsAppSDK-VersionInfo.h; "
                  "fill in AppxManifest.xml's PackageDependency by hand")
endif()
set(WASDK_FRAMEWORK_NAME "Microsoft.WindowsAppRuntime.${wasdk_major}.${wasdk_minor}")
configure_file(msix/AppxManifest.xml.in "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml" @ONLY)

# The shared gesture scripts (label "gui": they open a window).
if(DIADRAW_BUILD_TESTS)
  foreach(name gestures undo reconnect grid fit views rename clear-canvas format
               folding)
    add_test(NAME winui_${name}
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                     "${CMAKE_CURRENT_SOURCE_DIR}/../shell-tests/run.ps1" "$<TARGET_FILE:diadraw_winui>" ${name})
    set_tests_properties(winui_${name} PROPERTIES LABELS gui TIMEOUT 120)
  endforeach()
endif()
