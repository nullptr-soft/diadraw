target_sources(diadraw PRIVATE
  src/editor.cpp
  src/rename.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
