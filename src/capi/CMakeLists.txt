target_sources(diadraw PRIVATE
  src/capi.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ABI 2 of the C ABI (the DSL, layout, rendering and import) needs the host and export libraries,
# so it is a library of its own for language bindings (src/rust) and other C callers.
if(NOT EMSCRIPTEN)
  add_library(diadraw_capi STATIC src/capi_diagram.cpp)
  target_link_libraries(diadraw_capi PUBLIC diadraw diadraw_host diadraw_export PRIVATE diadraw_warnings)

  # Every static library a C caller links for ABI 2, one path per line in link order, so a build
  # outside CMake (Cargo's build script) needs no knowledge of the dependencies. One file per
  # configuration, since multi-config generators (Visual Studio, Xcode) build several.
  set(_capi_libs "$<TARGET_LINKER_FILE:diadraw_capi>\n$<TARGET_LINKER_FILE:diadraw_host>\n"
                 "$<TARGET_LINKER_FILE:diadraw_export>\n$<TARGET_LINKER_FILE:diadraw>\n")
  # The PNG backend's libraries, in-tree or (plutovg) found on the system.
  foreach(_lib lunasvg::lunasvg plutovg::plutovg)
    if(TARGET ${_lib})
      string(APPEND _capi_libs "$<TARGET_LINKER_FILE:${_lib}>\n")
    endif()
  endforeach()
  string(APPEND _capi_libs "$<TARGET_LINKER_FILE:miniz_inflate>\n")
  string(JOIN "" _capi_libs ${_capi_libs})
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/diadraw_capi_libs-$<CONFIG>.txt" CONTENT "${_capi_libs}")
endif()
