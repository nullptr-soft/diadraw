#include "host/feedback.hpp"

#include <gtest/gtest.h>

#include "diadraw/diadraw.h"
#include "diadraw/io/codec.hpp"

namespace dd::host {
namespace {

std::string version_field() { return "&version=" + io::percent_encode(dd_version()); }

TEST(Feedback, BugReportFillsTheForm) {
  EXPECT_EQ(feedback_url(FeedbackKind::Bug, {"macOS", "macOS 15.1 (Build 24B83)"}),
            DIADRAW_FEEDBACK_URL "?template=bug_report.yml&app=macOS" + version_field() +
                "&os=macOS%2015.1%20%28Build%2024B83%29");
}

TEST(Feedback, FeedbackUsesItsFormAndSkipsEmptyFields) {
  EXPECT_EQ(feedback_url(FeedbackKind::Feedback, {"Windows", ""}),
            DIADRAW_FEEDBACK_URL "?template=feedback.yml&app=Windows" + version_field());
  EXPECT_EQ(feedback_url(FeedbackKind::Feedback, {"", ""}), DIADRAW_FEEDBACK_URL "?template=feedback.yml" + version_field());
}

}  // namespace
}  // namespace dd::host
