// The platform's HTTPS client for host/cloud.hpp: WinHTTP on Windows, libcurl elsewhere.

#include "host/cloud.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>
#else
#include <curl/curl.h>

#include <mutex>
#endif

namespace dd::host::cloud {

namespace {

constexpr long kTimeoutSeconds = 60;

#ifdef _WIN32

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string last_error(const char* what) {
  return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

/// Closes a WinHTTP handle when it goes out of scope.
struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET handle) : h(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
  explicit operator bool() const { return h != nullptr; }
};

HttpResponse send(const HttpRequest& request) {
  HttpResponse out;
  std::wstring url = widen(request.url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
    out.error = last_error("WinHttpCrackUrl");
    return out;
  }
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  Handle session(WinHttpOpen(L"DiaDraw", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    out.error = last_error("WinHttpOpen");
    return out;
  }
  const int ms = static_cast<int>(kTimeoutSeconds * 1000);
  WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
  Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
  if (!connection) {
    out.error = last_error("WinHttpConnect");
    return out;
  }
  // Only HTTPS: every service address is, and a token must never go out in the clear.
  if (parts.nScheme != INTERNET_SCHEME_HTTPS) {
    out.error = "Refused to send a request without HTTPS";
    return out;
  }
  std::wstring method = widen(request.method);
  Handle req(WinHttpOpenRequest(connection.h, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!req) {
    out.error = last_error("WinHttpOpenRequest");
    return out;
  }
  // As with libcurl: a redirect is an answer to read, not one to follow with the token.
  DWORD no_redirects = WINHTTP_DISABLE_REDIRECTS;
  WinHttpSetOption(req.h, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirects, sizeof(no_redirects));
  std::wstring headers;
  for (const auto& [name, value] : request.headers) {
    if (value.empty()) continue;
    headers += widen(name) + L": " + widen(value) + L"\r\n";
  }
  if (!WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          headers.empty() ? 0 : static_cast<DWORD>(-1L),
                          request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
                          static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()), 0) ||
      !WinHttpReceiveResponse(req.h, nullptr)) {
    out.error = last_error("Sending the request");
    return out;
  }
  DWORD status = 0;
  DWORD size = sizeof(status);
  if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status, &size, WINHTTP_NO_HEADER_INDEX)) {
    out.error = last_error("Reading the status");
    return out;
  }
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(req.h, &available)) {
      out.error = last_error("Reading the answer");
      return out;
    }
    if (available == 0) break;
    const std::size_t at = out.body.size();
    out.body.resize(at + available);
    DWORD read = 0;
    if (!WinHttpReadData(req.h, out.body.data() + at, available, &read)) {
      out.error = last_error("Reading the answer");
      return out;
    }
    out.body.resize(at + read);
  }
  out.status = static_cast<int>(status);
  return out;
}

#else

std::size_t append(char* data, std::size_t size, std::size_t count, void* out) {
  static_cast<std::string*>(out)->append(data, size * count);
  return size * count;
}

/// Frees a curl handle and its header list when it goes out of scope.
struct Easy {
  CURL* curl = curl_easy_init();
  curl_slist* headers = nullptr;
  Easy() = default;
  Easy(const Easy&) = delete;
  Easy& operator=(const Easy&) = delete;
  ~Easy() {
    curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
  }
};

HttpResponse send(const HttpRequest& request) {
  static std::once_flag init;
  std::call_once(init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  HttpResponse out;
  Easy easy;
  if (!easy.curl) {
    out.error = "Could not start an HTTP request";
    return out;
  }
  CURL* c = easy.curl;
  for (const auto& [name, value] : request.headers) {
    if (value.empty()) continue;
    std::string line = name + ": " + value;
    curl_slist* next = curl_slist_append(easy.headers, line.c_str());
    if (!next) {
      out.error = "Out of memory";
      return out;
    }
    easy.headers = next;
  }
  // Without this, curl sends "Expect: 100-continue" for larger bodies and waits for an answer.
  if (curl_slist* next = curl_slist_append(easy.headers, "Expect:")) easy.headers = next;
  curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, request.method.c_str());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, easy.headers);
  // Only HTTPS: every service address is, and a token must never go out in the clear.
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(c, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);  // safe on threads other than the main one
  curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSeconds);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "DiaDraw");
  if (request.method != "GET" && request.method != "DELETE") {
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, request.body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
  }
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, append);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &out.body);
  if (CURLcode code = curl_easy_perform(c); code != CURLE_OK) {
    out.error = curl_easy_strerror(code);
    out.body.clear();
    return out;
  }
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  out.status = static_cast<int>(status);
  return out;
}

#endif

}  // namespace

Transport system_transport() {
  return send;
}

}  // namespace dd::host::cloud
