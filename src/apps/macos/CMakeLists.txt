# DiaDraw for macOS: an AppKit shell (Objective-C++) over the C++ core. Built with
# DIADRAW_BUILD_MACOS (on by default on macOS): diadraw-build/<preset>/apps/macos/DiaDraw.app.

enable_language(OBJCXX)

set(DIADRAW_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/../icons/DiaDraw.icns")
set_source_files_properties("${DIADRAW_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

if(NOT TARGET diadraw_cloud)
  message(FATAL_ERROR "DiaDraw for macOS needs libcurl (part of the macOS SDK) for Google Drive and OneDrive")
endif()

add_executable(diadraw_macos MACOSX_BUNDLE
  "${DIADRAW_ICNS}"
  src/main.mm
  src/DDDocument.mm
  src/DDCanvasView.mm
  src/DDInspector.mm
  src/DDStylePanel.mm
  src/DDStyleEditor.mm
  src/DDPropertiesTable.mm
  src/DDThemeEditors.mm
  src/DDDrawing.mm
  src/DDSyntax.mm
  src/DDCloudPanel.mm)
set_target_properties(diadraw_macos PROPERTIES
  OUTPUT_NAME DiaDraw
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
# The copyright line of the About panel: host/about.hpp's kCopyright (the host tests check it).
set(DIADRAW_COPYRIGHT "Copyright © 2026 Andriy Mykulyak")
configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
target_compile_options(diadraw_macos PRIVATE -fobjc-arc)
target_link_libraries(diadraw_macos PRIVATE diadraw diadraw_host diadraw_cloud diadraw_export
  "-framework AppKit" "-framework CoreText" "-framework UniformTypeIdentifiers")

# The licence and credits go into the bundle's Resources, and so do draw.io's stencil sets.
add_custom_command(TARGET diadraw_macos POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:diadraw_macos>/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/docs/credits.md"
          "$<TARGET_BUNDLE_CONTENT_DIR:diadraw_macos>/Resources/"
  COMMENT "Copying the licence and credits into DiaDraw.app"
  VERBATIM)
set(DIADRAW_DRAWIO_SETS "${PROJECT_SOURCE_DIR}/assets/drawio")
if(EXISTS "${DIADRAW_DRAWIO_SETS}/index.json.gz")
  add_custom_command(TARGET diadraw_macos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${DIADRAW_DRAWIO_SETS}"
            "$<TARGET_BUNDLE_CONTENT_DIR:diadraw_macos>/Resources/drawio"
    COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/assets/NOTICE"
            "$<TARGET_BUNDLE_CONTENT_DIR:diadraw_macos>/Resources/drawio/NOTICE"
    COMMENT "Copying draw.io stencil sets into DiaDraw.app")
endif()

# Signing: copying resources in invalidates the linker's ad-hoc signature, so the bundle is signed
# again last. "-" signs ad hoc (runs on this Mac); a "Developer ID Application: …" identity also
# turns on the hardened runtime, as notarization requires (see src/apps/macos/README.md).
set(DIADRAW_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for DiaDraw.app ('-' = ad hoc)")
if(DIADRAW_CODESIGN_IDENTITY STREQUAL "-")
  set(codesign_options "")
else()
  set(codesign_options --options runtime --timestamp)
endif()
add_custom_command(TARGET diadraw_macos POST_BUILD
  COMMAND codesign --force --deep ${codesign_options} --sign "${DIADRAW_CODESIGN_IDENTITY}"
          "$<TARGET_BUNDLE_DIR:diadraw_macos>"
  COMMENT "Signing DiaDraw.app"
  VERBATIM)

install(TARGETS diadraw_macos BUNDLE DESTINATION . COMPONENT app)

# Scripted gesture tests: they open a window, so they need a logged-in session (label "gui").
if(DIADRAW_BUILD_TESTS)
  foreach(name gestures undo inspector styles relationship-inspector reconnect style-editor grid fit multi-select views
               rename rename-inspector invalid-fields clear-canvas)
    add_test(NAME macos_${name}
             COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../shell-tests/run.sh" "$<TARGET_FILE:diadraw_macos>" ${name})
    set_tests_properties(macos_${name} PROPERTIES LABELS gui TIMEOUT 60)
  endforeach()
endif()
