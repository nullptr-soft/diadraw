# DiaDraw for Linux: a gtkmm 4 shell over the C++ core. Built with DIADRAW_BUILD_GTK
# (on by default on Linux when gtkmm-4.0 is found; on macOS pass -DDIADRAW_BUILD_GTK=ON after
# `brew install gtkmm4`): diadraw-build/<preset>/apps/gtk/diadraw-gtk.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)

if(NOT TARGET diadraw_cloud)
  message(FATAL_ERROR "DiaDraw for Linux needs libcurl for Google Drive and OneDrive: install libcurl4-openssl-dev (or libcurl-devel)")
endif()

add_executable(diadraw_gtk
  src/main.cpp
  src/window.cpp
  src/canvas_view.cpp
  src/inspector.cpp
  src/field_controls.cpp
  src/style_panel.cpp
  src/painter.cpp
  src/cloud_dialog.cpp
  src/text_folds.cpp)
set_target_properties(diadraw_gtk PROPERTIES OUTPUT_NAME diadraw-gtk)
target_link_libraries(diadraw_gtk PRIVATE diadraw diadraw_host diadraw_cloud diadraw_export PkgConfig::GTKMM)
# About's logo when the app runs from the build tree, before its icons are installed.
target_compile_definitions(diadraw_gtk PRIVATE
                           DIADRAW_SOURCE_ICON="${CMAKE_CURRENT_SOURCE_DIR}/../icons/hicolor/128.png")
if(APPLE)
  target_link_libraries(diadraw_gtk PRIVATE objc)
endif()

# Installed layout (GNU): the app, its desktop entry, file type, AppStream data and icons, its
# licence and credits in share/diadraw (kept by the Flatpak, which drops share/doc), and draw.io's
# stencil sets in share/diadraw/drawio with their NOTICE.
include(GNUInstallDirs)
set(app_id com.diadraw.DiaDraw)
install(TARGETS diadraw_gtk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT app)
install(FILES data/${app_id}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT app)
install(FILES data/${app_id}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT app)
install(FILES data/${app_id}.mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME ${app_id}.xml
        COMPONENT app)
set(icons "${CMAKE_CURRENT_SOURCE_DIR}/../icons")
foreach(size 16 24 32 48 64 128 256 512)
  install(FILES ${icons}/hicolor/${size}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
          RENAME ${app_id}.png COMPONENT app)
endforeach()
install(FILES ${icons}/diadraw.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ${app_id}.svg COMPONENT app)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/docs/credits.md"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/diadraw COMPONENT app)
set(drawio "${PROJECT_SOURCE_DIR}/assets/drawio")
if(EXISTS "${drawio}/index.json.gz")
  install(DIRECTORY ${drawio}/ DESTINATION ${CMAKE_INSTALL_DATADIR}/diadraw/drawio COMPONENT app)
  install(FILES "${PROJECT_SOURCE_DIR}/assets/NOTICE"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/diadraw/drawio COMPONENT app)
endif()

# Scripted gesture tests, the same scripts as the macOS app's (label "gui": they open a window).
if(DIADRAW_BUILD_TESTS)
  foreach(name gestures undo inspector styles relationship-inspector reconnect style-editor grid fit multi-select views
               rename rename-inspector invalid-fields clear-canvas format
               folding)
    add_test(NAME gtk_${name}
             COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/../shell-tests/run.sh" "$<TARGET_FILE:diadraw_gtk>" ${name})
    set_tests_properties(gtk_${name} PROPERTIES LABELS gui TIMEOUT 60)
  endforeach()
endif()
