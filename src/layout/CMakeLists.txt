target_sources(diadraw PRIVATE
  src/layout.cpp
  src/sequence.cpp
  src/constraints.cpp
  src/layered.cpp
  src/placement.cpp
  src/json.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
