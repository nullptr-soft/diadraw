# The `diadraw` Python package's extension module (nanobind), built by scikit-build-core from the
# repository's pyproject.toml. See bindings/python/README.md.

find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module
             OPTIONAL_COMPONENTS Development.SABIModule)
if(NOT nanobind_DIR)
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
                  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_DIR RESULT_VARIABLE nanobind_status)
  if(NOT nanobind_status EQUAL 0)
    message(FATAL_ERROR "DIADRAW_BUILD_PYTHON needs nanobind for ${Python_EXECUTABLE}: pip install nanobind")
  endif()
endif()
find_package(nanobind CONFIG REQUIRED)

# One stable-ABI wheel covers Python 3.12 and later; older versions get a wheel each.
# NB_SUPPRESS_WARNINGS includes nanobind's headers as system headers, so the project's warnings
# apply to the bindings only.
nanobind_add_module(diadraw_python STABLE_ABI NB_STATIC NB_SUPPRESS_WARNINGS src/module.cpp)
set_target_properties(diadraw_python PROPERTIES OUTPUT_NAME _core)
target_link_libraries(diadraw_python PRIVATE diadraw diadraw_host diadraw_export diadraw_warnings)
install(TARGETS diadraw_python LIBRARY DESTINATION diadraw COMPONENT python)

# draw.io's converted stencil sets, so imported draw.io diagrams keep their artwork once the
# package is installed away from this tree (the package points the core at them on import).
install(DIRECTORY "${PROJECT_SOURCE_DIR}/assets/drawio/" DESTINATION diadraw/drawio COMPONENT python)
