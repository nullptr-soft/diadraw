target_sources(diadraw PRIVATE
  src/builtin.cpp
  src/library.cpp
  src/svg_sanitize.cpp
  src/disk.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
