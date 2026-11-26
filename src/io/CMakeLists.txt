target_sources(diadraw PRIVATE
  src/builder.cpp
  src/import.cpp
  src/mermaid.cpp
  src/codec.cpp
  src/xml.cpp
  src/drawio.cpp
  src/drawio_stencil.cpp
  src/drawio_export.cpp
  src/bpmn_model.cpp
  src/bpmn.cpp
  src/bpmn_export.cpp
  src/structurizr_ir.cpp
  src/structurizr_json.cpp
  src/structurizr_dsl.cpp
  src/plantuml/preprocess.cpp
  src/plantuml/common.cpp
  src/plantuml/structural.cpp
  src/plantuml/sequence.cpp
  src/plantuml/activity.cpp
  src/plantuml/c4.cpp
  src/plantuml/tree.cpp
  src/plantuml/plantuml.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
