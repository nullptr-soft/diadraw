#pragma once

// Google Drive and OneDrive for the native apps (the web app has its own, src/apps/web/src/cloud.ts,
// with the same scopes and folders, so a diagram saved from one opens in the other).
//
// Signing in follows RFC 8252 (OAuth for native apps): the system browser opens the provider's
// sign-in page, which redirects to a one-shot HTTP listener on the loopback interface; the code it
// brings is swapped for tokens with PKCE. Tokens stay in memory for as long as the app runs.
//
// Access is the narrowest each service has:
//   Google Drive: drive.file, the files DiaDraw created, saved in the root of My Drive.
//   OneDrive:     Files.ReadWrite.AppFolder, the Apps/DiaDraw folder.
//
// Every call that talks to a service blocks: shells run them with `in_background` and get the
// result back on their UI thread. Accounts are safe to use from several threads.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "diadraw/base/result.hpp"

namespace dd::host::cloud {

enum class Provider { GoogleDrive, OneDrive };
inline constexpr Provider kProviders[] = {Provider::GoogleDrive, Provider::OneDrive};

/// "Google Drive" or "OneDrive".
[[nodiscard]] std::string_view label(Provider provider);
/// Where diagrams go, for the dialog's note.
[[nodiscard]] std::string_view folder_note(Provider provider);

// ── HTTP ──

struct HttpRequest {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

struct HttpResponse {
  /// The HTTP status; 0 when the request did not get an answer (see `error`).
  int status = 0;
  std::string body;
  /// Why there is no answer (no network, TLS failure); empty otherwise.
  std::string error;
};

/// Sends one request and waits for the answer. Blocking; must be safe to call from any thread.
using Transport = std::function<HttpResponse(const HttpRequest&)>;

/// The platform's HTTPS client: libcurl on macOS and Linux, WinHTTP on Windows.
[[nodiscard]] Transport system_transport();

// ── Configuration ──

/// OAuth clients registered for DiaDraw (see docs/guide/cloud-storage.md). Each comes from the
/// build (CMake: DIADRAW_GOOGLE_CLIENT_ID, DIADRAW_GOOGLE_CLIENT_SECRET, DIADRAW_ONEDRIVE_CLIENT_ID)
/// and can be overridden by an environment variable of the same name.
struct ClientConfig {
  std::string google_client_id;
  /// Google's "Desktop app" clients have a secret that is not secret (it ships in every copy);
  /// Google's token endpoint still asks for it.
  std::string google_client_secret;
  std::string onedrive_client_id;
};

[[nodiscard]] ClientConfig client_config();

// ── Signing in ──

/// One HTTP request on the loopback interface, to catch the provider's redirect.
class LoopbackListener {
 public:
  /// Listens on 127.0.0.1 on a free port.
  [[nodiscard]] static Result<std::unique_ptr<LoopbackListener>> open();
  ~LoopbackListener();
  LoopbackListener(const LoopbackListener&) = delete;
  LoopbackListener& operator=(const LoopbackListener&) = delete;

  [[nodiscard]] int port() const { return port_; }
  /// Waits for a `GET /?…` whose query string (without `?`) `accept` takes, answers it with a page
  /// that says the browser tab can be closed, and returns the query. Anything else (a favicon, a
  /// stray or hostile request, an answer to another sign-in) is turned away and the wait goes on.
  /// Gives up after `timeout_ms` or once `cancel` is set.
  [[nodiscard]] Result<std::string> wait(int timeout_ms, const std::atomic<bool>& cancel,
                                         const std::function<bool(std::string_view query)>& accept);

 private:
  LoopbackListener(std::intptr_t socket, int port) : socket_(socket), port_(port) {}
  std::intptr_t socket_;
  int port_;
};

/// The parameters of a query string, decoded (`a=1&b=x%20y`).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> parse_query(std::string_view query);
/// A PKCE verifier (43 characters) and its S256 challenge (RFC 7636), from 32 random bytes.
struct Pkce {
  std::string verifier, challenge;
};
[[nodiscard]] Pkce make_pkce(std::string_view random32);
/// base64url without padding.
[[nodiscard]] std::string base64url(std::string_view bytes);
/// `n` bytes from the system's random source.
[[nodiscard]] std::string random_bytes(std::size_t n);
/// An RFC 3339 time (`2026-02-01T10:00:00.123Z`, or with an offset) in milliseconds since the
/// epoch; 0 when it does not parse.
[[nodiscard]] std::int64_t parse_time(std::string_view text);

/// A signed-in (or not) account with one service.
class Account {
 public:
  Account(Provider provider, std::string client_id, std::string client_secret, Transport transport);

  [[nodiscard]] Provider provider() const { return provider_; }
  /// Whether this build has a client ID for the service.
  [[nodiscard]] bool configured() const { return !client_id_.empty(); }
  [[nodiscard]] bool signed_in() const;

  /// The sign-in page for a redirect to `redirect_uri`, with `state` and the PKCE `challenge`.
  [[nodiscard]] std::string authorize_url(const std::string& redirect_uri, const std::string& state,
                                          const std::string& challenge) const;
  /// The redirect address for a loopback listener on `port`.
  [[nodiscard]] std::string redirect_uri(int port) const;

  /// Signs in: listens on the loopback interface, calls `open_url` with the sign-in page (from
  /// this thread: the shell posts it to its UI thread or opens it directly), and waits up to five
  /// minutes for the browser to come back. Blocking.
  [[nodiscard]] Result<bool> sign_in(const std::function<void(const std::string&)>& open_url,
                                     const std::atomic<bool>& cancel);
  /// Whether a redirect's query string answers the sign-in started with `state`.
  [[nodiscard]] static bool answers(std::string_view query, const std::string& state);
  /// Finishes a sign-in with the redirect's query string (split out for tests).
  [[nodiscard]] Result<bool> finish_sign_in(std::string_view query, const std::string& state,
                                            const std::string& verifier,
                                            const std::string& redirect_uri);
  void sign_out();

  /// A valid access token, refreshed when it has run out; an error when a sign-in is needed (then
  /// `signed_in()` is false).
  [[nodiscard]] Result<std::string> token();
  /// Forgets the access token after a 401, keeping the refresh token.
  void expire();

  /// The clock, in milliseconds since the epoch (tests move it).
  std::function<std::int64_t()> now;

 private:
  /// Posts a token request. `generation` is the sign-in it belongs to: a sign-out or another
  /// sign-in meanwhile drops its answer. `status` gets the HTTP status (0: no answer).
  [[nodiscard]] Result<bool> token_request(std::string form, std::uint64_t generation, int* status = nullptr);

  Provider provider_;
  std::string client_id_, client_secret_;
  Transport transport_;
  mutable std::mutex mutex_;
  std::string access_, refresh_;
  std::int64_t expires_ = 0;
  /// Bumped by every sign-in and sign-out, so that a refresh that was under way cannot undo one.
  std::uint64_t generation_ = 0;
};

/// A diagram's place in a cloud: where a document opened from, or saved to, a cloud lives.
struct Place {
  Provider provider = Provider::GoogleDrive;
  std::string id;
  std::string name;  ///< `name.draw`
};

/// What a cloud dialog shows for one service, the same in every shell.
struct PanelState {
  bool signed_in = false;
  /// Beside the account button: "Signed in to …", "Sign in to open and save …", or "Waiting …".
  std::string account_text;
  /// "Sign in to …", "Sign out" or "Cancel"; hidden when the build has no client ID.
  std::string button_text;
  bool show_button = false;
  /// The sign-in button is the dialog's main action.
  bool button_primary = false;
  /// Below the list: where diagrams go, or that this build is not set up for the service.
  std::string note;
  /// When the list is empty, and while it loads.
  std::string empty_text, loading_text = "Loading…";
  std::string save_tooltip;
  /// Delete's tooltip and its confirmation.
  std::string remove_tooltip, remove_detail;
};

[[nodiscard]] PanelState panel_state(Provider provider, bool signing_in);
/// Delete's tooltip and its confirmation, for each row of the list.
[[nodiscard]] std::string_view remove_tooltip(Provider provider);
[[nodiscard]] std::string_view remove_detail(Provider provider);

// ── Files ──

struct File {
  std::string id;
  /// As the service shows it: `name.draw`.
  std::string name;
  /// Milliseconds since the epoch.
  std::int64_t updated = 0;
  std::int64_t size = -1;  ///< -1 when unknown
};

struct Contents {
  File file;
  std::string text;
};

/// `name.draw`, keeping a `.draw` that is there (any case); `Untitled.draw` for a blank name.
[[nodiscard]] std::string draw_file_name(std::string_view name);
/// A name without `.draw`.
[[nodiscard]] std::string plain_name(std::string_view name);

/// A folder of diagrams in one service. All calls block.
class Drive {
 public:
  Drive(Account& account, Transport transport) : account_(account), transport_(std::move(transport)) {}

  [[nodiscard]] Account& account() const { return account_; }

  /// Every diagram, most recently changed first.
  [[nodiscard]] Result<std::vector<File>> list();
  /// A diagram and its text; nothing (not an error) when it is gone.
  [[nodiscard]] Result<std::optional<Contents>> read(const std::string& id);
  [[nodiscard]] Result<File> create(std::string_view name, std::string_view text);
  [[nodiscard]] Result<File> save(const std::string& id, std::string_view text);
  [[nodiscard]] Result<File> rename(const std::string& id, std::string_view name);
  /// Moves it to the service's trash or recycle bin.
  [[nodiscard]] Result<bool> remove(const std::string& id);

 private:
  /// An authorised request; on 401 renews the token once. `missing_ok` turns 404 into status 404.
  [[nodiscard]] Result<HttpResponse> call(HttpRequest request, bool missing_ok = false);
  [[nodiscard]] Result<File> file_call(HttpRequest request);

  Account& account_;
  Transport transport_;
};

/// The app's accounts and drives, one per service, made on first use with `system_transport()`
/// and `client_config()`. They are never destroyed, since background requests may still be using
/// them while the app exits.
[[nodiscard]] Drive& drive(Provider provider);

// ── Running calls off the UI thread ──

/// Runs `fn` on the shell's UI thread (g_idle_add, dispatch_async, DispatcherQueue).
using Post = std::function<void(std::function<void()>)>;

namespace detail {
/// Starts a detached thread; false when the system cannot start one.
[[nodiscard]] bool start_thread(std::function<void()> body) noexcept;
}  // namespace detail

/// Runs `work` on a new thread, then posts `done` with its result. The shell keeps whatever `done`
/// touches alive, or checks a weak pointer first: the window may close meanwhile. Nothing throws
/// out of here: an exception in `work`, or a thread that cannot start, becomes an error result.
template <class T>
void in_background(const Post& post, std::function<Result<T>()> work, std::function<void(Result<T>)> done) {
  auto finish = std::make_shared<std::function<void(Result<T>)>>(std::move(done));
  const bool started = detail::start_thread([post, work = std::move(work), finish]() mutable {
    auto result = std::make_shared<Result<T>>(Error{"DiaDraw ran out of memory"});
    try {
      *result = work();
    } catch (const std::exception& e) {
      *result = Error{e.what()};
    } catch (...) {
      *result = Error{"An unexpected error"};
    }
    try {
      post([finish, result] { (*finish)(std::move(*result)); });
    } catch (...) {
      // The UI is gone (the app is exiting): nobody is left to tell.
    }
  });
  if (!started) post([finish] { (*finish)(Error{"Could not start a background task"}); });
}

/// The outcome of saving a document to its place in a cloud, with what to tell the person.
struct SaveOutcome {
  bool ok = false;
  /// The save failed because the sign-in ran out: open the dialog on that service.
  bool needs_sign_in = false;
  /// "Saved … in …", "Sign in to …, then save again." or "Could not save …: …".
  std::string message;
  /// The text that was saved (or not): the document is clean when it still has this text.
  std::string text;
  Place place;
};

/// One document's saves to its place in a cloud, one at a time, so that an older text never
/// lands after a newer one: a save asked for while one runs waits for it and then saves the
/// latest text (only the latest of those waiting runs, and it answers the callers of the texts it
/// replaced too). Used on the UI thread only.
class Saver {
 public:
  void save(const Post& post, Place place, std::string text, std::function<void(const SaveOutcome&)> done);
  [[nodiscard]] bool busy() const { return state_->busy; }

 private:
  struct Job {
    Post post;
    Place place;
    std::string text;
    /// Every caller waiting on this text, including those of texts it replaced.
    std::vector<std::function<void(const SaveOutcome&)>> done;
  };
  struct State {
    bool busy = false;
    std::optional<Job> next;
  };
  static void start(const std::shared_ptr<State>& state, Job job);
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace dd::host::cloud
