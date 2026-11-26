#include "host/cloud.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <deque>
#include <functional>
#include <string>
#include <thread>

#include "diadraw/base/json.hpp"
#include "diadraw/io/codec.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dd::host::cloud {
namespace {

/// A transport that records requests and answers from a queue.
struct FakeServer {
  std::vector<HttpRequest> requests;
  std::deque<std::function<HttpResponse(const HttpRequest&)>> answers;

  Transport transport() {
    return [this](const HttpRequest& r) {
      requests.push_back(r);
      if (answers.empty()) return HttpResponse{500, R"({"error":{"message":"unexpected"}})", {}};
      auto next = std::move(answers.front());
      answers.pop_front();
      return next(r);
    };
  }
  void reply(int status, std::string body) {
    answers.emplace_back([status, body = std::move(body)](const HttpRequest&) { return HttpResponse{status, body, {}}; });
  }
};

std::string header(const HttpRequest& r, std::string_view name) {
  for (const auto& [k, v] : r.headers)
    if (k == name) return v;
  return {};
}

std::string param(std::string_view query, std::string_view key) {
  for (auto& [k, v] : parse_query(query))
    if (k == key) return v;
  return {};
}

std::string query_of(const std::string& url) {
  auto q = url.find('?');
  return q == std::string::npos ? std::string{} : url.substr(q + 1);
}

/// An account signed in with tokens `access` (an hour) and `refresh`.
void sign_in(Account& account, FakeServer& server, const std::string& access = "tok",
             const std::string& refresh = "ref") {
  server.reply(200, R"({"access_token":")" + access + R"(","refresh_token":")" + refresh + R"(","expires_in":3600})");
  ASSERT_TRUE(account.finish_sign_in("code=c&state=s", "s", "verifier", "http://127.0.0.1:1"));
  server.requests.clear();
}

TEST(Cloud, Names) {
  EXPECT_EQ(draw_file_name("Login flow"), "Login flow.draw");
  EXPECT_EQ(draw_file_name("  a.DRAW "), "a.DRAW");
  EXPECT_EQ(draw_file_name("   "), "Untitled.draw");
  EXPECT_EQ(plain_name("Flow.draw"), "Flow");
  EXPECT_EQ(plain_name("Flow"), "Flow");
  EXPECT_EQ(std::string(label(Provider::OneDrive)), "OneDrive");
}

TEST(Cloud, QueryAndTime) {
  auto q = parse_query("code=a%2Fb&state=x+y&empty=&flag");
  ASSERT_EQ(q.size(), 4u);
  EXPECT_EQ(q[0], (std::pair<std::string, std::string>{"code", "a/b"}));
  EXPECT_EQ(q[1].second, "x y");
  EXPECT_EQ(q[2].second, "");
  EXPECT_EQ(q[3].first, "flag");
  EXPECT_EQ(parse_time("1970-01-01T00:00:00Z"), 0);
  EXPECT_EQ(parse_time("2026-02-01T10:00:00.250Z"), 1769940000250);
  EXPECT_EQ(parse_time("2026-02-01T12:00:00+02:00"), 1769940000000);
  EXPECT_EQ(parse_time("not a time"), 0);
}

TEST(Cloud, Pkce) {
  const std::string random(32, '\x07');
  Pkce p = make_pkce(random);
  EXPECT_EQ(p.verifier.size(), 43u);
  EXPECT_EQ(p.verifier.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"),
            std::string::npos);
  EXPECT_EQ(p.challenge, base64url(io::sha256(p.verifier)));
  EXPECT_EQ(random_bytes(5).size(), 5u);
  EXPECT_NE(random_bytes(16), random_bytes(16));
}

TEST(Cloud, GoogleSignIn) {
  FakeServer server;
  Account account(Provider::GoogleDrive, "client-g", "secret-g", server.transport());
  EXPECT_TRUE(account.configured());
  EXPECT_FALSE(account.signed_in());
  EXPECT_FALSE(account.token());
  const std::string url = account.authorize_url(account.redirect_uri(5000), "st", "ch");
  EXPECT_EQ(url.rfind("https://accounts.google.com/o/oauth2/v2/auth?", 0), 0u);
  const auto q = query_of(url);
  EXPECT_EQ(param(q, "client_id"), "client-g");
  EXPECT_EQ(param(q, "redirect_uri"), "http://127.0.0.1:5000");
  EXPECT_EQ(param(q, "scope"), "https://www.googleapis.com/auth/drive.file");
  EXPECT_EQ(param(q, "code_challenge"), "ch");
  EXPECT_EQ(param(q, "code_challenge_method"), "S256");
  EXPECT_EQ(param(q, "state"), "st");

  server.reply(200, R"({"access_token":"a1","refresh_token":"r1","expires_in":3600})");
  ASSERT_TRUE(account.finish_sign_in("state=st&code=the%2Fcode&scope=x", "st", "ver", "http://127.0.0.1:5000"));
  ASSERT_EQ(server.requests.size(), 1u);
  const auto& exchange = server.requests[0];
  EXPECT_EQ(exchange.method, "POST");
  EXPECT_EQ(exchange.url, "https://oauth2.googleapis.com/token");
  EXPECT_EQ(param(exchange.body, "code"), "the/code");
  EXPECT_EQ(param(exchange.body, "code_verifier"), "ver");
  EXPECT_EQ(param(exchange.body, "client_secret"), "secret-g");
  EXPECT_EQ(param(exchange.body, "redirect_uri"), "http://127.0.0.1:5000");
  EXPECT_TRUE(account.signed_in());
  EXPECT_EQ(*account.token(), "a1");

  // An hour later the token is refreshed; Google's answer keeps the old refresh token.
  std::int64_t clock = account.now() + 3'600'000;
  account.now = [&clock] { return clock; };
  server.reply(200, R"({"access_token":"a2","expires_in":3600})");
  EXPECT_EQ(*account.token(), "a2");
  EXPECT_EQ(param(server.requests.back().body, "refresh_token"), "r1");
  clock += 3'600'000;
  server.reply(200, R"({"access_token":"a3","expires_in":3600})");
  EXPECT_EQ(*account.token(), "a3");
  EXPECT_EQ(param(server.requests.back().body, "refresh_token"), "r1");

  // A refused refresh token signs out.
  clock += 3'600'000;
  server.reply(400, R"({"error":"invalid_grant"})");
  auto token = account.token();
  ASSERT_FALSE(token);
  EXPECT_EQ(token.error().message, "Sign in to Google Drive first");
  EXPECT_FALSE(account.signed_in());
}

TEST(Cloud, SignInErrors) {
  FakeServer server;
  Account account(Provider::OneDrive, "client-m", "", server.transport());
  EXPECT_EQ(account.finish_sign_in("error=access_denied&state=s", "s", "v", "r").error().message,
            "Sign-in was cancelled");
  EXPECT_EQ(account.finish_sign_in("code=c&state=other", "s", "v", "r").error().message,
            "Sign-in answered a different request");
  // An error without this sign-in's state is someone else's, not shown.
  EXPECT_EQ(account.finish_sign_in("error=access_denied", "s", "v", "r").error().message,
            "Sign-in answered a different request");
  EXPECT_EQ(account.finish_sign_in("state=s", "s", "v", "r").error().message, "Sign-in answered a different request");
  EXPECT_FALSE(Account::answers("code=c&state=s", ""));
  EXPECT_TRUE(Account::answers("code=c&state=s", "s"));
  server.reply(400, R"({"error":"invalid_client","error_description":"AADSTS7000218: bad"})");
  EXPECT_EQ(account.finish_sign_in("code=c&state=s", "s", "v", "r").error().message, "OneDrive: AADSTS7000218: bad");
  // A token answer without an access token is an error, and leaves the account signed out.
  server.reply(200, R"({"refresh_token":"r","expires_in":3600})");
  EXPECT_FALSE(account.finish_sign_in("code=c&state=s", "s", "v", "r"));
  EXPECT_FALSE(account.signed_in());
  Account unset(Provider::GoogleDrive, "", "", server.transport());
  EXPECT_FALSE(unset.configured());
  std::atomic<bool> cancel{false};
  EXPECT_EQ(unset.sign_in([](const std::string&) {}, cancel).error().message,
            "Google Drive is not set up in this build of DiaDraw");
}

TEST(Cloud, MicrosoftSignInUrl) {
  FakeServer server;
  Account account(Provider::OneDrive, "client-m", "", server.transport());
  EXPECT_EQ(account.redirect_uri(6000), "http://localhost:6000");
  const auto url = account.authorize_url("http://localhost:6000", "st", "ch");
  EXPECT_EQ(url.rfind("https://login.microsoftonline.com/common/oauth2/v2.0/authorize?", 0), 0u);
  EXPECT_EQ(param(query_of(url), "scope"), "Files.ReadWrite.AppFolder offline_access");
  server.reply(200, R"({"access_token":"m1","refresh_token":"r1","expires_in":3600})");
  ASSERT_TRUE(account.finish_sign_in("code=c&state=st", "st", "v", "http://localhost:6000"));
  EXPECT_EQ(server.requests[0].url, "https://login.microsoftonline.com/common/oauth2/v2.0/token");
  EXPECT_EQ(param(server.requests[0].body, "client_secret"), "");
  EXPECT_EQ(param(server.requests[0].body, "scope"), "Files.ReadWrite.AppFolder offline_access");
}

TEST(Cloud, GoogleDriveFiles) {
  FakeServer server;
  Account account(Provider::GoogleDrive, "g", "s", server.transport());
  sign_in(account, server);
  Drive drive(account, server.transport());

  server.reply(200, R"({"files":[{"id":"a","name":"A.draw","modifiedTime":"2026-01-01T00:00:00Z","size":"12"}],
                        "nextPageToken":"p2"})");
  server.reply(200, R"({"files":[{"id":"b","name":"B.draw","modifiedTime":"2026-02-01T00:00:00Z"}]})");
  auto files = drive.list();
  ASSERT_TRUE(files) << files.error().message;
  ASSERT_EQ(files->size(), 2u);
  EXPECT_EQ((*files)[0].id, "b");  // newest first
  EXPECT_EQ((*files)[1].size, 12);
  EXPECT_EQ((*files)[0].size, -1);
  EXPECT_EQ(header(server.requests[0], "Authorization"), "Bearer tok");
  EXPECT_NE(param(query_of(server.requests[0].url), "q").find("trashed = false"), std::string::npos);
  EXPECT_EQ(param(query_of(server.requests[1].url), "pageToken"), "p2");

  server.requests.clear();
  const std::string file = R"({"id":"f1","name":"Flow.draw","modifiedTime":"2026-03-01T00:00:00Z","size":"5"})";
  server.reply(200, file);
  auto created = drive.create("Flow", "model {}\n");
  ASSERT_TRUE(created);
  EXPECT_EQ(created->id, "f1");
  const auto& create = server.requests[0];
  EXPECT_EQ(create.method, "POST");
  EXPECT_EQ(create.url.rfind("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart", 0), 0u);
  const std::string type = header(create, "Content-Type");
  ASSERT_EQ(type.rfind("multipart/related; boundary=", 0), 0u);
  const std::string boundary = type.substr(std::strlen("multipart/related; boundary="));
  EXPECT_NE(create.body.find("--" + boundary + "\r\nContent-Type: application/json"), std::string::npos);
  EXPECT_NE(create.body.find(R"({"name":"Flow.draw","mimeType":"text/plain"})"), std::string::npos);
  EXPECT_NE(create.body.find("\r\n\r\nmodel {}\n\r\n--" + boundary + "--"), std::string::npos);

  server.reply(200, file);
  ASSERT_TRUE(drive.save("f1", "model { a = system }\n"));
  EXPECT_EQ(server.requests[1].method, "PATCH");
  EXPECT_EQ(server.requests[1].body, "model { a = system }\n");
  EXPECT_NE(server.requests[1].url.find("/upload/drive/v3/files/f1?uploadType=media"), std::string::npos);

  server.reply(200, R"({"id":"f1","name":"New.draw"})");
  EXPECT_EQ(drive.rename("f1", "New")->name, "New.draw");
  EXPECT_EQ(server.requests[2].body, R"({"name":"New.draw"})");

  server.reply(200, file);
  ASSERT_TRUE(drive.remove("f1"));
  EXPECT_EQ(server.requests[3].body, R"({"trashed":true})");

  server.reply(200, file);
  server.reply(200, "model {}\n");
  auto read = drive.read("f1");
  ASSERT_TRUE(read && *read);
  EXPECT_EQ((*read)->text, "model {}\n");
  EXPECT_EQ((*read)->file.name, "Flow.draw");
  EXPECT_NE(server.requests[5].url.find("f1?alt=media"), std::string::npos);

  server.reply(404, R"({"error":{"message":"File not found"}})");
  auto gone = drive.read("gone");
  ASSERT_TRUE(gone);
  EXPECT_FALSE(*gone);
}

TEST(Cloud, RenewsOnceAfter401) {
  FakeServer server;
  Account account(Provider::GoogleDrive, "g", "s", server.transport());
  sign_in(account, server);
  Drive drive(account, server.transport());
  server.reply(401, "{}");
  server.reply(200, R"({"access_token":"fresh","expires_in":3600})");  // the refresh
  server.reply(200, R"({"files":[]})");
  ASSERT_TRUE(drive.list());
  EXPECT_EQ(header(server.requests[2], "Authorization"), "Bearer fresh");

  server.reply(400, R"({"error":{"message":"Bad id"}})");
  EXPECT_EQ(drive.read("x").error().message, "Google Drive: Bad id");

  server.reply(0, "");
  server.answers.back() = [](const HttpRequest&) { return HttpResponse{0, {}, "Could not resolve host"}; };
  EXPECT_EQ(drive.list().error().message, "Google Drive: Could not resolve host");

  server.reply(200, "[]");
  EXPECT_FALSE(drive.list());
}

TEST(Cloud, RefreshFailingOnTheNetworkKeepsTheSignIn) {
  FakeServer server;
  Account account(Provider::OneDrive, "m", "", server.transport());
  sign_in(account, server);
  std::int64_t clock = account.now() + 3'600'000;
  account.now = [&clock] { return clock; };
  server.reply(0, "");
  server.answers.back() = [](const HttpRequest&) { return HttpResponse{0, {}, "Network is unreachable"}; };
  EXPECT_FALSE(account.token());
  EXPECT_TRUE(account.signed_in());
  server.reply(503, R"({"error":"temporarily_unavailable"})");
  EXPECT_FALSE(account.token());
  EXPECT_TRUE(account.signed_in());
  server.reply(200, R"({"access_token":"again","expires_in":3600})");
  EXPECT_EQ(*account.token(), "again");
}

TEST(Cloud, SignOutDuringARefreshWins) {
  FakeServer server;
  Account account(Provider::OneDrive, "m", "", server.transport());
  sign_in(account, server);
  std::int64_t clock = account.now() + 3'600'000;
  account.now = [&clock] { return clock; };
  // Signed out while the refresh is on the wire: its answer is dropped.
  server.answers.emplace_back([&account](const HttpRequest&) {
    account.sign_out();
    return HttpResponse{200, R"({"access_token":"late","refresh_token":"r2","expires_in":3600})", {}};
  });
  EXPECT_FALSE(account.token());
  EXPECT_FALSE(account.signed_in());
}

TEST(Cloud, SignInDuringARefusedRefreshStays) {
  FakeServer server;
  Account account(Provider::OneDrive, "m", "", server.transport());
  sign_in(account, server);
  std::int64_t clock = account.now() + 3'600'000;
  account.now = [&clock] { return clock; };
  // A new sign-in lands while the old refresh token is being refused: the new one stays.
  server.answers.emplace_back([&](const HttpRequest&) {
    server.reply(200, R"({"access_token":"new","refresh_token":"r2","expires_in":3600})");
    EXPECT_TRUE(account.finish_sign_in("code=c&state=s", "s", "v", "r"));
    return HttpResponse{400, R"({"error":"invalid_grant"})", {}};
  });
  auto token = account.token();
  ASSERT_TRUE(token);
  EXPECT_EQ(*token, "new");
  EXPECT_TRUE(account.signed_in());
}

TEST(Cloud, BackgroundTurnsAnExceptionIntoAnError) {
  std::mutex m;
  std::condition_variable cv;
  std::function<void()> posted;
  Post post = [&](std::function<void()> fn) {
    std::lock_guard lock(m);
    posted = std::move(fn);
    cv.notify_one();
  };
  std::string message;
  in_background<int>(
      post, []() -> Result<int> { throw std::runtime_error("boom"); },
      [&](Result<int> r) { message = r ? "ok" : r.error().message; });
  std::unique_lock lock(m);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return static_cast<bool>(posted); }));
  posted();
  EXPECT_EQ(message, "boom");
}

TEST(Cloud, RefusesTextThatIsNotUtf8) {
  FakeServer server;
  Account account(Provider::GoogleDrive, "g", "s", server.transport());
  sign_in(account, server);
  Drive drive(account, server.transport());
  server.reply(200, R"({"id":"f","name":"Bin.draw"})");
  server.reply(200, std::string("model {}\n\xff\xfe", 12));
  auto read = drive.read("f");
  ASSERT_FALSE(read);
  EXPECT_EQ(read.error().message, "Bin.draw is not UTF-8 text");
}

TEST(Cloud, OneDriveFiles) {
  FakeServer server;
  Account account(Provider::OneDrive, "m", "", server.transport());
  sign_in(account, server, "ms");
  Drive drive(account, server.transport());
  const std::string graph = "https://graph.microsoft.com/v1.0/me/drive";

  server.reply(200, R"({"value":[{"id":"a","name":"A.draw","lastModifiedDateTime":"2026-01-01T00:00:00Z","size":3,"file":{}},
                                  {"id":"d","name":"Folder","folder":{}}],
                        "@odata.nextLink":"https://graph.microsoft.com/v1.0/me/drive/next"})");
  server.reply(200, R"({"value":[{"id":"b","name":"B.draw","lastModifiedDateTime":"2026-02-01T00:00:00Z","file":{}}]})");
  auto files = drive.list();
  ASSERT_TRUE(files);
  ASSERT_EQ(files->size(), 2u);
  EXPECT_EQ((*files)[0].id, "b");
  EXPECT_EQ((*files)[1].size, 3);
  EXPECT_EQ(server.requests[0].url.rfind(graph + "/special/approot/children?", 0), 0u);
  EXPECT_EQ(server.requests[1].url, graph + "/next");

  server.requests.clear();
  server.reply(200, R"({"id":"i1","name":"I.draw","@microsoft.graph.downloadUrl":"https://dl.example/i1"})");
  server.reply(200, "model {}\n");
  auto read = drive.read("i1");
  ASSERT_TRUE(read && *read);
  EXPECT_EQ((*read)->text, "model {}\n");
  EXPECT_EQ(server.requests[1].url, "https://dl.example/i1");
  EXPECT_EQ(header(server.requests[1], "Authorization"), "");

  server.reply(201, R"({"id":"i2","name":"Flow 1.draw","file":{}})");
  ASSERT_TRUE(drive.create("Flow / v2", "x"));
  EXPECT_EQ(server.requests[2].method, "PUT");
  EXPECT_EQ(server.requests[2].url,
            graph + "/special/approot:/Flow%20%2F%20v2.draw:/content?@microsoft.graph.conflictBehavior=rename");
  server.reply(200, R"({"id":"i2","name":"Flow 1.draw","file":{}})");
  ASSERT_TRUE(drive.save("i2", "y"));
  EXPECT_EQ(server.requests[3].url, graph + "/items/i2/content");
  server.reply(204, "");
  ASSERT_TRUE(drive.remove("i2"));
  EXPECT_EQ(server.requests[4].method, "DELETE");

  server.reply(404, R"({"error":{"code":"itemNotFound","message":"Item not found"}})");
  auto gone = drive.read("gone");
  ASSERT_TRUE(gone);
  EXPECT_FALSE(*gone);
}

TEST(Cloud, BackgroundPostsTheResult) {
  std::mutex m;
  std::condition_variable cv;
  std::function<void()> posted;
  Post post = [&](std::function<void()> fn) {
    std::lock_guard lock(m);
    posted = std::move(fn);
    cv.notify_one();
  };
  int got = 0;
  in_background<int>(post, [] { return Result<int>(42); }, [&](Result<int> r) { got = *r; });
  std::unique_lock lock(m);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return static_cast<bool>(posted); }));
  posted();
  EXPECT_EQ(got, 42);
}

TEST(Cloud, SaverRunsOneSaveAtATime) {
  // The app's own drives, signed out (no network is used): every save asks for a sign-in.
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::function<void()>> posted;
  Post post = [&](std::function<void()> fn) {
    std::lock_guard lock(m);
    posted.push_back(std::move(fn));
    cv.notify_one();
  };
  auto run_next = [&] {
    std::unique_lock lock(m);
    if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return !posted.empty(); })) return false;
    auto fn = std::move(posted.front());
    posted.pop_front();
    lock.unlock();
    fn();
    return true;
  };
  Saver saver;
  const Place place{Provider::OneDrive, "id", "Flow.draw"};
  std::vector<std::string> answered;
  auto note = [&](std::string who) {
    return [&answered, who](const SaveOutcome& o) { answered.push_back(who + ":" + o.text + ":" + (o.needs_sign_in ? "sign-in" : "?")); };
  };
  saver.save(post, place, "one", note("a"));
  EXPECT_TRUE(saver.busy());
  saver.save(post, place, "two", note("b"));
  saver.save(post, place, "three", note("c"));  // replaces "two", and answers its caller too
  ASSERT_TRUE(run_next());
  EXPECT_EQ(answered, (std::vector<std::string>{"a:one:sign-in"}));
  EXPECT_TRUE(saver.busy());
  ASSERT_TRUE(run_next());
  EXPECT_EQ(answered, (std::vector<std::string>{"a:one:sign-in", "b:three:sign-in", "c:three:sign-in"}));
  EXPECT_FALSE(saver.busy());
}

#ifndef _WIN32
/// Sends `request` to 127.0.0.1:`port` and returns the answer.
std::string fetch(int port, const std::string& request) {
  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
    ::close(s);
    return {};
  }
  (void)::send(s, request.data(), request.size(), 0);
  std::string out;
  char buf[1024];
  for (ssize_t n; (n = ::recv(s, buf, sizeof(buf), 0)) > 0;) out.append(buf, static_cast<std::size_t>(n));
  ::close(s);
  return out;
}

/// Connects to 127.0.0.1:`port` and hangs up without a word.
void hang_up(int port) {
  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(static_cast<std::uint16_t>(port));
  (void)::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  ::close(s);
}

const auto ours = [](std::string_view query) { return Account::answers(query, "xyz"); };

TEST(Cloud, LoopbackCatchesTheRedirect) {
  auto listener = LoopbackListener::open();
  ASSERT_TRUE(listener) << listener.error().message;
  const int port = (*listener)->port();
  EXPECT_GT(port, 0);
  std::string favicon, post, huge, other, redirect;
  std::thread browser([&] {
    hang_up(port);
    favicon = fetch(port, "GET /favicon.ico HTTP/1.1\r\nHost: x\r\n\r\n");
    post = fetch(port, "POST /?code=abc&state=xyz HTTP/1.1\r\nHost: x\r\n\r\n");
    huge = fetch(port, "GET /?state=xyz&pad=" + std::string(20000, 'a') + " HTTP/1.1\r\n\r\n");
    other = fetch(port, "GET /?code=abc&state=another HTTP/1.1\r\nHost: x\r\n\r\n");
    redirect = fetch(port, "GET /?code=abc&state=xyz HTTP/1.1\r\nHost: x\r\n\r\n");
  });
  std::atomic<bool> cancel{false};
  auto query = (*listener)->wait(10'000, cancel, ours);
  browser.join();
  ASSERT_TRUE(query) << query.error().message;
  EXPECT_EQ(*query, "code=abc&state=xyz");
  EXPECT_EQ(favicon.rfind("HTTP/1.1 404", 0), 0u);
  EXPECT_EQ(post.rfind("HTTP/1.1 404", 0), 0u);
  // Too long for a head: turned away (a 400, or a reset once the rest goes unread), never a 200.
  EXPECT_EQ(huge.find("HTTP/1.1 200"), std::string::npos);
  EXPECT_EQ(other.rfind("HTTP/1.1 400", 0), 0u);
  EXPECT_EQ(redirect.rfind("HTTP/1.1 200", 0), 0u);
  EXPECT_NE(redirect.find("You can close this tab"), std::string::npos);
}

TEST(Cloud, LoopbackStopsOnCancelAndTimeout) {
  auto listener = LoopbackListener::open();
  ASSERT_TRUE(listener);
  std::atomic<bool> cancel{true};
  EXPECT_EQ((*listener)->wait(10'000, cancel, ours).error().message, "Sign-in was cancelled");
  cancel = false;
  EXPECT_EQ((*listener)->wait(50, cancel, ours).error().message, "Sign-in timed out");
}
#endif

}  // namespace
}  // namespace dd::host::cloud
