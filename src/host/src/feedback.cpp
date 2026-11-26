#include "host/feedback.hpp"

#include <string_view>

#include "diadraw/diadraw.h"
#include "diadraw/io/codec.hpp"

#ifndef DIADRAW_FEEDBACK_URL
#error "DIADRAW_FEEDBACK_URL is set by src/host/CMakeLists.txt"
#endif

namespace dd::host {

std::string feedback_url(FeedbackKind kind, const FeedbackSource& source) {
  std::string url = DIADRAW_FEEDBACK_URL;
  url += kind == FeedbackKind::Bug ? "?template=bug_report.yml" : "?template=feedback.yml";
  auto add = [&url](std::string_view key, std::string_view value) {
    if (value.empty()) return;
    url += '&';
    url += key;
    url += '=';
    url += io::percent_encode(value);
  };
  add("app", source.app);
  add("version", dd_version());
  add("os", source.os);
  return url;
}

}  // namespace dd::host
