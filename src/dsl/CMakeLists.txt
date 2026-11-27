target_sources(diadraw PRIVATE
  src/syntax.cpp
  src/highlight.cpp
  src/lower.cpp
  src/print.cpp
  src/format.cpp
  src/patch.cpp
  src/json.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
