# The Node.js addon (`@diadraw/node`), built by cmake-js from bindings/node/scripts/build.mjs, which
# configures the repository root with DIADRAW_BUILD_NODE=ON. cmake-js supplies Node's headers
# (CMAKE_JS_INC), the Windows delay-load hook (CMAKE_JS_SRC) and node.lib (CMAKE_JS_LIB); the
# build script passes node-addon-api's include directory as NODE_ADDON_API_DIR.
if(NOT DEFINED CMAKE_JS_INC OR NOT DEFINED NODE_ADDON_API_DIR)
  message(FATAL_ERROR "DIADRAW_BUILD_NODE needs cmake-js: run `npm run build -w @diadraw/node`")
endif()

add_library(diadraw_node SHARED src/addon.cpp ${CMAKE_JS_SRC})
set_target_properties(diadraw_node PROPERTIES PREFIX "" SUFFIX ".node" OUTPUT_NAME diadraw
                                              CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
# One place for every generator and platform (a DLL is a runtime output on Windows); the generator
# expression keeps multi-configuration generators from adding a <config> folder.
set_target_properties(diadraw_node PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/addon>"
                                              RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/addon>")
target_include_directories(diadraw_node SYSTEM PRIVATE ${CMAKE_JS_INC} "${NODE_ADDON_API_DIR}")
# Node-API 8 is in every Node the package supports (20 on); C++ exceptions carry errors to JavaScript.
target_compile_definitions(diadraw_node PRIVATE NAPI_VERSION=8 NAPI_CPP_EXCEPTIONS NODE_ADDON_API_DISABLE_DEPRECATED)
target_link_libraries(diadraw_node PRIVATE diadraw_host diadraw_export diadraw diadraw_warnings ${CMAKE_JS_LIB})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Only the module's entry point is exported, not the static libraries' symbols (the core,
  # lunasvg, plutovg, miniz), which could clash with another addon's copies.
  target_link_options(diadraw_node PRIVATE "LINKER:--exclude-libs,ALL")
endif()
if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
  # node.lib from Node-API's .def file, for cmake-js versions that leave generating it to us.
  execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF} /out:${CMAKE_JS_NODELIB_TARGET} ${CMAKE_STATIC_LINKER_FLAGS})
endif()
