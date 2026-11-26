// The one-shot loopback listener that catches an OAuth redirect (RFC 8252 §7.3).

#include <chrono>
#include <string>
#include <string_view>

#include "host/cloud.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>

#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dd::host::cloud {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
int poll_one(Socket s, int ms) {
  WSAPOLLFD p{};
  p.fd = s;
  p.events = POLLRDNORM;
  return WSAPoll(&p, 1, ms);
}
/// Winsock, started once for the process.
bool start_sockets() {
  static const bool started = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return started;
}
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
void close_socket(Socket s) { ::close(s); }
int poll_one(Socket s, int ms) {
  pollfd p{};
  p.fd = s;
  p.events = POLLIN;
  return ::poll(&p, 1, ms);
}
bool start_sockets() { return true; }
#endif

/// A socket that a browser started meanwhile does not inherit, and whose writes never raise
/// SIGPIPE (which would end the macOS app when a client hangs up early).
void harden(Socket s) {
#ifdef _WIN32
  SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
#else
  ::fcntl(s, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  int on = 1;
  ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

/// Whether poll failed for good (not just interrupted by a signal).
bool poll_failed(int ready) {
#ifdef _WIN32
  return ready < 0;
#else
  return ready < 0 && errno != EINTR;
#endif
}

Socket as_socket(std::intptr_t s) { return static_cast<Socket>(s); }

constexpr std::string_view kDonePage =
    "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>DiaDraw</title></head>"
    "<body style=\"font: 15px system-ui, sans-serif; padding: 2rem\">"
    "<h1 style=\"font-size: 1.2rem\">You can close this tab</h1>"
    "<p>DiaDraw got the answer from the sign-in page. Go back to DiaDraw to carry on.</p></body></html>";

void answer(Socket s, std::string_view status, std::string_view body) {
  std::string response = "HTTP/1.1 ";
  response += status;
  response += "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
              "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  response += body;
  std::size_t sent = 0;
  while (sent < response.size()) {
    auto n = ::send(s, response.data() + sent, static_cast<int>(response.size() - sent), kSendFlags);
    if (n <= 0) break;
    sent += static_cast<std::size_t>(n);
  }
}

}  // namespace

Result<std::unique_ptr<LoopbackListener>> LoopbackListener::open() {
  if (!start_sockets()) return fail("Could not start networking");
  Socket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kNoSocket) return fail("Could not open a socket for the sign-in");
  harden(s);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;  // any free port
  socklen_t length = sizeof(address);
  if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(s, 4) != 0 ||
      ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    close_socket(s);
    return fail("Could not listen for the sign-in on 127.0.0.1");
  }
  return std::unique_ptr<LoopbackListener>(
      new LoopbackListener(static_cast<std::intptr_t>(s), ntohs(address.sin_port)));
}

LoopbackListener::~LoopbackListener() {
  close_socket(as_socket(socket_));
}

Result<std::string> LoopbackListener::wait(int timeout_ms, const std::atomic<bool>& cancel,
                                           const std::function<bool(std::string_view)>& accept) {
  using Clock = std::chrono::steady_clock;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  const Socket listener = as_socket(socket_);
  while (!cancel.load()) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return fail("Sign-in timed out");
    // Wake up now and then to notice a cancel.
    const int ready = poll_one(listener, static_cast<int>(left < 250 ? left : 250));
    if (poll_failed(ready)) return fail("Could not wait for the sign-in");
    if (ready <= 0) continue;
    Socket client = ::accept(listener, nullptr, nullptr);
    if (client == kNoSocket) continue;
    harden(client);
    // Read the request head (the browser sends it at once; give it two seconds).
    std::string head;
    char buffer[2048];
    const auto read_deadline = Clock::now() + std::chrono::seconds(2);
    while (head.find("\r\n\r\n") == std::string::npos && head.size() < 16384 && Clock::now() < read_deadline) {
      if (poll_one(client, 200) <= 0) continue;
      auto n = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
      if (n <= 0) break;
      head.append(buffer, static_cast<std::size_t>(n));
    }
    if (head.find("\r\n\r\n") == std::string::npos) {
      // Cut short, too slow or too long to be a browser's redirect.
      answer(client, "400 Bad Request", "");
      close_socket(client);
      continue;
    }
    // "GET /?code=…&state=… HTTP/1.1"
    std::string_view line(head);
    line = line.substr(0, line.find("\r\n"));
    std::string_view target;
    if (line.rfind("GET ", 0) == 0) {
      target = line.substr(4);
      target = target.substr(0, target.find(' '));
    }
    if (target.empty() || target[0] != '/' || (target.size() > 1 && target[1] != '?')) {
      answer(client, "404 Not Found", "");
      close_socket(client);
      continue;
    }
    const std::string_view query = target.size() > 1 ? target.substr(2) : std::string_view{};
    if (!accept(query)) {
      // Not the answer to this sign-in (a stray request, or one for another sign-in): wait on.
      answer(client, "400 Bad Request", "");
      close_socket(client);
      continue;
    }
    answer(client, "200 OK", kDonePage);
    close_socket(client);
    return std::string(query);
  }
  return fail("Sign-in was cancelled");
}

}  // namespace dd::host::cloud
