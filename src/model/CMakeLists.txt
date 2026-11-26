target_sources(diadraw PRIVATE
  src/model.cpp
  src/operations.cpp
  src/history.cpp
  src/views.cpp
  src/json.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
