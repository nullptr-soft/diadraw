#pragma once

// Help › Report a Bug and Help › Give Feedback: the web address of a new issue on the project's
// tracker, with the issue form (.github/ISSUE_TEMPLATE) chosen and the app, version and system
// filled in. The shells open it in the browser; nothing is sent from the app itself, and the
// diagram is never included.

#include <string>

namespace dd::host {

enum class FeedbackKind { Bug, Feedback };

/// Where the report is filed from: the issue form's `app` choice ("macOS", "Linux (GTK)",
/// "Windows") and the operating system as the shell describes it ("macOS 15.1").
struct FeedbackSource {
  std::string app;
  std::string os;
};

/// The new-issue address: the base set at build time (DIADRAW_FEEDBACK_URL, by default this
/// repository's `issues/new`), the form for `kind`, and `source` and the core's version as
/// query fields. Empty fields are left out. The web app builds the same address in
/// src/apps/web/src/feedback.ts, which has its own copy of the base.
[[nodiscard]] std::string feedback_url(FeedbackKind kind, const FeedbackSource& source);

}  // namespace dd::host
