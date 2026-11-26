target_sources(diadraw PRIVATE
  src/svg.cpp
  src/display_list.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
