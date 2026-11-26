target_sources(diadraw PRIVATE
  src/json.cpp
  src/text.cpp
  src/js_regex.cpp
)
target_include_directories(diadraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Apple's libc++ has std::from_chars for double only from a recent macOS; builds for an older one
# (the Python wheels) parse numbers with strtod_l instead (base/text.hpp, from_chars_double).
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
#include <charconv>
int main() {
  const char s[] = "1.5";
  double d = 0;
  return static_cast<int>(std::from_chars(s, s + 3, d).ec);
}
]=] DD_HAVE_FROM_CHARS_DOUBLE)
if(DD_HAVE_FROM_CHARS_DOUBLE)
  target_compile_definitions(diadraw PRIVATE DD_HAVE_FROM_CHARS_DOUBLE=1)
endif()
