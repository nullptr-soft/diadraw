#include "host/cloud.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <random>
#include <thread>

#include "diadraw/base/json.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/io/codec.hpp"
#include "host/files.hpp"

#ifndef DIADRAW_GOOGLE_CLIENT_ID
#error "DIADRAW_GOOGLE_CLIENT_ID is set by src/host/CMakeLists.txt"
#endif

namespace dd::host::cloud {

namespace {

constexpr std::string_view kGoogleScope = "https://www.googleapis.com/auth/drive.file";
constexpr std::string_view kOneDriveScope = "Files.ReadWrite.AppFolder offline_access";
constexpr std::string_view kMicrosoft = "https://login.microsoftonline.com/common/oauth2/v2.0";
constexpr std::string_view kDrive = "https://www.googleapis.com/drive/v3/files";
constexpr std::string_view kDriveUpload = "https://www.googleapis.com/upload/drive/v3/files";
constexpr std::string_view kDriveFields = "id,name,modifiedTime,size";
constexpr std::string_view kGraph = "https://graph.microsoft.com/v1.0/me/drive";
constexpr std::string_view kGraphSelect = "$select=id,name,lastModifiedDateTime,size,file";
constexpr int kSignInTimeoutMs = 5 * 60 * 1000;
/// token_request's generation for a new sign-in, which wins over everything before it.
constexpr std::uint64_t kNewSignIn = ~std::uint64_t{0};

std::int64_t system_now() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/// A form or query: `k=v&…`, percent-encoded.
std::string form(std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
  std::string out;
  for (const auto& [key, value] : fields) {
    if (!out.empty()) out += '&';
    out += io::percent_encode(key);
    out += '=';
    out += io::percent_encode(value);
  }
  return out;
}

const std::string* string_member(const Json& json, std::string_view key) {
  const Json* v = json.get(key);
  return v && v->is_string() ? &v->as_string() : nullptr;
}

/// The service's own message from an error answer, or the status.
std::string error_text(const HttpResponse& response) {
  if (response.status == 0) return response.error.empty() ? "no answer" : response.error;
  if (auto json = Json::parse(response.body)) {
    if (const Json* error = json->get("error")) {
      if (error->is_string()) {
        if (const auto* d = string_member(*json, "error_description")) return *d;
        return error->as_string();
      }
      if (const auto* m = string_member(*error, "message")) return *m;
    }
  }
  return "HTTP " + std::to_string(response.status);
}

}  // namespace

std::string_view label(Provider provider) {
  return provider == Provider::GoogleDrive ? "Google Drive" : "OneDrive";
}

std::string_view folder_note(Provider provider) {
  return provider == Provider::GoogleDrive
             ? "Diagrams are saved as .draw files in My Drive. DiaDraw can only see the files it saved there itself."
             : "Diagrams are saved as .draw files in your OneDrive, in the Apps › DiaDraw folder. Files you put in "
               "that folder show up here too.";
}

ClientConfig client_config() {
  auto pick = [](const char* name, const char* built) {
    // NOLINT(concurrency-mt-unsafe): DiaDraw never sets environment variables.
    const char* env = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
    return std::string(env && *env ? env : built);
  };
  return {pick("DIADRAW_GOOGLE_CLIENT_ID", DIADRAW_GOOGLE_CLIENT_ID),
          pick("DIADRAW_GOOGLE_CLIENT_SECRET", DIADRAW_GOOGLE_CLIENT_SECRET),
          pick("DIADRAW_ONEDRIVE_CLIENT_ID", DIADRAW_ONEDRIVE_CLIENT_ID)};
}

// ── Helpers ──

std::vector<std::pair<std::string, std::string>> parse_query(std::string_view query) {
  std::vector<std::pair<std::string, std::string>> out;
  auto decode = [](std::string_view part) {
    std::string s(part);
    std::replace(s.begin(), s.end(), '+', ' ');
    return io::safe_decode_uri(s);
  };
  while (!query.empty()) {
    auto amp = query.find('&');
    auto pair = query.substr(0, amp);
    query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
    if (pair.empty()) continue;
    auto eq = pair.find('=');
    out.emplace_back(decode(pair.substr(0, eq)),
                     eq == std::string_view::npos ? std::string{} : decode(pair.substr(eq + 1)));
  }
  return out;
}

std::string base64url(std::string_view bytes) {
  std::string s = io::base64_encode(bytes);
  for (char& c : s) {
    if (c == '+') c = '-';
    else if (c == '/') c = '_';
  }
  while (!s.empty() && s.back() == '=') s.pop_back();
  return s;
}

Pkce make_pkce(std::string_view random32) {
  Pkce p;
  p.verifier = base64url(random32);
  p.challenge = base64url(io::sha256(p.verifier));
  return p;
}

std::string random_bytes(std::size_t n) {
  // std::random_device reads the operating system's CSPRNG with libstdc++, libc++ and MSVC.
  std::random_device device;
  std::string out(n, '\0');
  for (std::size_t i = 0; i < n; i += 4) {
    auto word = device();
    for (std::size_t k = 0; k < 4 && i + k < n; ++k) out[i + k] = static_cast<char>((word >> (8 * k)) & 0xFF);
  }
  return out;
}

std::int64_t parse_time(std::string_view t) {
  // YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]
  auto digits = [&](std::size_t at, std::size_t n, int& out) {
    if (at + n > t.size()) return false;
    out = 0;
    for (std::size_t i = at; i < at + n; ++i) {
      if (t[i] < '0' || t[i] > '9') return false;
      out = out * 10 + (t[i] - '0');
    }
    return true;
  };
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (!digits(0, 4, y) || t.size() < 19 || t[4] != '-' || !digits(5, 2, mo) || t[7] != '-' ||
      !digits(8, 2, d) || (t[10] != 'T' && t[10] != 't' && t[10] != ' ') || !digits(11, 2, h) || t[13] != ':' ||
      !digits(14, 2, mi) || t[16] != ':' || !digits(17, 2, s) || mo < 1 || mo > 12 || d < 1 || d > 31)
    return 0;
  std::size_t at = 19;
  std::int64_t ms = 0;
  if (at < t.size() && t[at] == '.') {
    ++at;
    int scale = 100;
    while (at < t.size() && t[at] >= '0' && t[at] <= '9') {
      ms += (t[at] - '0') * scale;
      scale /= 10;
      ++at;
    }
  }
  std::int64_t offset = 0;
  if (at < t.size() && (t[at] == '+' || t[at] == '-')) {
    int oh = 0, om = 0;
    if (!digits(at + 1, 2, oh) || at + 3 >= t.size() || t[at + 3] != ':' || !digits(at + 4, 2, om)) return 0;
    offset = (t[at] == '+' ? 1 : -1) * (oh * 60 + om) * 60;
  }
  // Days from the civil date (Howard Hinnant's algorithm).
  y -= mo <= 2;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const std::int64_t days = era * 146097 + static_cast<std::int64_t>(doe) - 719468;
  return (days * 86400 + h * 3600 + mi * 60 + s - offset) * 1000 + ms;
}

std::string draw_file_name(std::string_view name) {
  const std::string_view trimmed = trim(name);
  if (trimmed.empty()) return "Untitled.draw";
  if (ends_with_ci(trimmed, ".draw")) return std::string(trimmed);
  return std::string(trimmed) + ".draw";
}

std::string plain_name(std::string_view name) {
  return std::string(ends_with_ci(name, ".draw") ? name.substr(0, name.size() - 5) : name);
}

PanelState panel_state(Provider provider, bool signing_in) {
  const Account& account = drive(provider).account();
  const std::string name(label(provider));
  PanelState p;
  p.signed_in = account.signed_in();
  if (signing_in) {
    p.account_text = "Waiting for you to sign in to " + name + " in your browser…";
    p.button_text = "Cancel";
  } else if (account.configured()) {
    p.account_text = p.signed_in ? "Signed in to " + name : "Sign in to open and save diagrams in " + name + ".";
    p.button_text = p.signed_in ? std::string("Sign out") : "Sign in to " + name;
    p.button_primary = !p.signed_in;
  }
  p.show_button = account.configured();
  p.note = account.configured() ? std::string(folder_note(provider))
                                : "This copy of DiaDraw is not set up for " + name + ": it was built without a " +
                                      name + " client ID.";
  p.empty_text = "No diagrams saved in " + name + " yet.";
  p.save_tooltip = "Save a copy of the current diagram in " + name;
  p.remove_tooltip = remove_tooltip(provider);
  p.remove_detail = remove_detail(provider);
  return p;
}

std::string_view remove_tooltip(Provider provider) {
  return provider == Provider::GoogleDrive ? "Move it to the Google Drive trash" : "Move it to the OneDrive recycle bin";
}

std::string_view remove_detail(Provider provider) {
  return provider == Provider::GoogleDrive ? "It moves to the Google Drive trash, where it can be restored for 30 days."
                                           : "It moves to the OneDrive recycle bin.";
}

// ── Account ──

Account::Account(Provider provider, std::string client_id, std::string client_secret, Transport transport)
    : now(system_now),
      provider_(provider),
      client_id_(std::move(client_id)),
      client_secret_(std::move(client_secret)),
      transport_(std::move(transport)) {}

bool Account::signed_in() const {
  std::lock_guard lock(mutex_);
  return !access_.empty() && (expires_ > now() || !refresh_.empty());
}

std::string Account::redirect_uri(int port) const {
  // Microsoft matches http://localhost with any port; Google, http://127.0.0.1 with any port.
  return (provider_ == Provider::OneDrive ? "http://localhost:" : "http://127.0.0.1:") + std::to_string(port);
}

std::string Account::authorize_url(const std::string& redirect_uri, const std::string& state,
                                   const std::string& challenge) const {
  if (provider_ == Provider::GoogleDrive) {
    return "https://accounts.google.com/o/oauth2/v2/auth?" +
           form({{"client_id", client_id_},
                 {"redirect_uri", redirect_uri},
                 {"response_type", "code"},
                 {"scope", kGoogleScope},
                 {"code_challenge", challenge},
                 {"code_challenge_method", "S256"},
                 {"access_type", "offline"},
                 {"state", state}});
  }
  return std::string(kMicrosoft) + "/authorize?" +
         form({{"client_id", client_id_},
               {"redirect_uri", redirect_uri},
               {"response_type", "code"},
               {"response_mode", "query"},
               {"scope", kOneDriveScope},
               {"code_challenge", challenge},
               {"code_challenge_method", "S256"},
               {"prompt", "select_account"},
               {"state", state}});
}

Result<bool> Account::sign_in(const std::function<void(const std::string&)>& open_url,
                              const std::atomic<bool>& cancel) {
  if (!configured()) return fail(label(provider_), " is not set up in this build of DiaDraw");
  auto listener = LoopbackListener::open();
  if (!listener) return listener.error();
  const std::string redirect = redirect_uri((*listener)->port());
  const std::string state = base64url(random_bytes(16));
  const Pkce pkce = make_pkce(random_bytes(32));
  open_url(authorize_url(redirect, state, pkce.challenge));
  // Only the answer to this sign-in ends the wait: a stray request, or a web page probing local
  // ports, is turned away.
  auto query = (*listener)->wait(kSignInTimeoutMs, cancel,
                                 [&state](std::string_view q) { return answers(q, state); });
  if (!query) return query.error();
  return finish_sign_in(*query, state, pkce.verifier, redirect);
}

bool Account::answers(std::string_view query, const std::string& state) {
  for (const auto& [k, v] : parse_query(query))
    if (k == "state") return !state.empty() && v == state;
  return false;
}

Result<bool> Account::finish_sign_in(std::string_view query, const std::string& state, const std::string& verifier,
                                     const std::string& redirect_uri) {
  // The state first: an error or a code from anything but this sign-in is not ours to show.
  if (!answers(query, state)) return fail("Sign-in answered a different request");
  std::map<std::string, std::string> params;
  for (auto& [k, v] : parse_query(query)) params[std::move(k)] = std::move(v);
  if (auto e = params.find("error"); e != params.end()) {
    if (e->second == "access_denied") return fail("Sign-in was cancelled");
    auto d = params.find("error_description");
    return fail(d != params.end() ? d->second : e->second);
  }
  auto code = params.find("code");
  if (code == params.end() || code->second.empty()) return fail("Sign-in answered a different request");
  std::string body = form({{"client_id", client_id_},
                           {"grant_type", "authorization_code"},
                           {"code", code->second},
                           {"redirect_uri", redirect_uri},
                           {"code_verifier", verifier}});
  if (provider_ == Provider::GoogleDrive) body += "&" + form({{"client_secret", client_secret_}});
  else body += "&" + form({{"scope", kOneDriveScope}});
  return token_request(std::move(body), kNewSignIn);
}

Result<bool> Account::token_request(std::string body, std::uint64_t generation, int* status) {
  HttpRequest request;
  request.method = "POST";
  request.url = provider_ == Provider::GoogleDrive ? "https://oauth2.googleapis.com/token" : std::string(kMicrosoft) + "/token";
  request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
  request.body = std::move(body);
  HttpResponse response = transport_(request);
  if (status) *status = response.status;
  if (response.status != 200) return fail(label(provider_), ": ", error_text(response));
  auto json = Json::parse(response.body);
  const std::string* access = json ? string_member(*json, "access_token") : nullptr;
  if (!access) return fail(label(provider_), " did not return an access token");
  double seconds = 3600;
  if (const Json* e = json->get("expires_in"); e && e->is_number()) seconds = e->as_number();
  // An odd or huge lifetime from the server: between two minutes (past token()'s one-minute
  // margin) and a day.
  seconds = std::clamp(std::isfinite(seconds) ? seconds : 3600.0, 120.0, 86400.0);
  std::lock_guard lock(mutex_);
  if (generation == kNewSignIn) ++generation_;
  else if (generation != generation_) return fail("Signed out meanwhile");  // a sign-out or sign-in won
  access_ = *access;
  expires_ = now() + static_cast<std::int64_t>(seconds * 1000);
  // A refresh answer may leave the refresh token out: keep the old one then.
  if (const auto* r = string_member(*json, "refresh_token")) refresh_ = *r;
  return true;
}

void Account::sign_out() {
  std::lock_guard lock(mutex_);
  ++generation_;
  access_.clear();
  refresh_.clear();
  expires_ = 0;
}

void Account::expire() {
  std::lock_guard lock(mutex_);
  expires_ = 0;
}

Result<std::string> Account::token() {
  std::string refresh;
  std::uint64_t generation = 0;
  {
    std::lock_guard lock(mutex_);
    // A minute's margin, so a token does not run out mid-request.
    if (!access_.empty() && expires_ - 60000 > now()) return access_;
    refresh = refresh_;
    generation = generation_;
  }
  if (!refresh.empty()) {
    std::string body = form({{"client_id", client_id_}, {"grant_type", "refresh_token"}, {"refresh_token", refresh}});
    if (provider_ == Provider::GoogleDrive) body += "&" + form({{"client_secret", client_secret_}});
    else body += "&" + form({{"scope", kOneDriveScope}});
    int status = 0;
    auto refreshed = token_request(std::move(body), generation, &status);
    std::lock_guard lock(mutex_);
    // Refreshed, or signed in again meanwhile.
    // (A sign-out between the refresh and this lock leaves no token: that is "sign in first".)
    if (!access_.empty() && (refreshed || expires_ - 60000 > now())) return access_;
    // Offline, or the service is down: the refresh token still stands, so stay signed in.
    if (status != 400 && status != 401) return refreshed.error();
    // The refresh token was refused (expired or revoked): sign out, unless a sign-in or sign-out
    // happened meanwhile.
    if (generation == generation_) {
      ++generation_;
      access_.clear();
      refresh_.clear();
      expires_ = 0;
    }
  } else {
    std::lock_guard lock(mutex_);
    if (generation == generation_ && !access_.empty()) {
      ++generation_;
      access_.clear();
      expires_ = 0;
    }
  }
  return fail("Sign in to ", label(provider_), " first");
}

// ── Drive ──

Result<HttpResponse> Drive::call(HttpRequest request, bool missing_ok) {
  for (int attempt = 0;; ++attempt) {
    auto token = account_.token();
    if (!token) return token.error();
    HttpRequest authorised = request;
    authorised.headers.emplace_back("Authorization", "Bearer " + *token);
    HttpResponse response = transport_(authorised);
    if (response.status == 401 && attempt == 0) {
      account_.expire();
      continue;
    }
    if (response.status == 404 && missing_ok) return response;
    if (response.status < 200 || response.status >= 300)
      return fail(label(account_.provider()), ": ", error_text(response));
    return response;
  }
}

namespace {

File file_from(const Json& json, Provider provider) {
  File f;
  if (const auto* id = string_member(json, "id")) f.id = *id;
  if (const auto* name = string_member(json, "name")) f.name = *name;
  const bool google = provider == Provider::GoogleDrive;
  if (const auto* t = string_member(json, google ? "modifiedTime" : "lastModifiedDateTime")) f.updated = parse_time(*t);
  if (const Json* size = json.get("size")) {
    // Drive sends sizes as strings (int64 in JSON), Graph as numbers.
    if (size->is_number()) {
      const double n = size->as_number();
      // An odd number from the server is not worth undefined behaviour.
      if (std::isfinite(n) && n >= 0 && n < 9.0e15) f.size = static_cast<std::int64_t>(n);
    }
    else if (size->is_string()) f.size = std::strtoll(size->as_string().c_str(), nullptr, 10);
  }
  return f;
}

HttpRequest get(std::string url) {
  HttpRequest r;
  r.url = std::move(url);
  return r;
}

HttpRequest with_body(std::string method, std::string url, std::string type, std::string body) {
  HttpRequest r;
  r.method = std::move(method);
  r.url = std::move(url);
  r.headers = {{"Content-Type", std::move(type)}};
  r.body = std::move(body);
  return r;
}

/// A file's name from its metadata, for messages.
std::string json_name_of(const Json& json) {
  const auto* name = string_member(json, "name");
  return name ? *name : std::string("The file");
}

std::string json_name(std::string_view name) {
  std::string out = "{\"name\":";
  append_json_string(out, draw_file_name(name));
  return out + "}";
}

}  // namespace

Result<File> Drive::file_call(HttpRequest request) {
  auto response = call(std::move(request));
  if (!response) return response.error();
  auto json = Json::parse(response->body);
  if (!json || !json->is_object()) return fail(label(account_.provider()), " sent an answer DiaDraw cannot read");
  return file_from(*json, account_.provider());
}

Result<std::vector<File>> Drive::list() {
  const Provider provider = account_.provider();
  const bool google = provider == Provider::GoogleDrive;
  std::vector<File> files;
  std::string page;  // Drive's page token, or Graph's next link
  std::string url = google ? "" : std::string(kGraph) + "/special/approot/children?" + std::string(kGraphSelect) + "&$top=200";
  do {
    if (google) {
      url = std::string(kDrive) + "?" +
            form({{"q", "trashed = false and mimeType != 'application/vnd.google-apps.folder'"},
                  {"spaces", "drive"},
                  {"orderBy", "modifiedTime desc"},
                  {"pageSize", "200"},
                  {"fields", "nextPageToken,files(" + std::string(kDriveFields) + ")"}});
      if (!page.empty()) url += "&" + form({{"pageToken", page}});
    }
    auto response = call(get(url));
    if (!response) return response.error();
    auto json = Json::parse(response->body);
    if (!json || !json->is_object()) return fail(label(provider), " sent a list DiaDraw cannot read");
    if (const Json* items = json->get(google ? "files" : "value"); items && items->is_array()) {
      for (const Json& item : items->as_array()) {
        if (!item.is_object() || (!google && !item.get("file"))) continue;  // Graph: skip folders
        files.push_back(file_from(item, provider));
      }
    }
    const auto* next = string_member(*json, google ? "nextPageToken" : "@odata.nextLink");
    // A server that hands out the same page again would keep this going for ever.
    if (next && *next == page) return fail(label(provider), " sent the same page of the list twice");
    page = next ? *next : std::string{};
    if (!google) url = page;
  } while (!page.empty());
  std::stable_sort(files.begin(), files.end(), [](const File& a, const File& b) {
    return a.updated != b.updated ? a.updated > b.updated : a.name < b.name;
  });
  return files;
}

Result<std::optional<Contents>> Drive::read(const std::string& id) {
  const Provider provider = account_.provider();
  const std::string key = io::percent_encode(id);
  if (provider == Provider::GoogleDrive) {
    auto meta = call(get(std::string(kDrive) + "/" + key + "?fields=" + std::string(kDriveFields)), true);
    if (!meta) return meta.error();
    if (meta->status == 404) return std::optional<Contents>{};
    auto json = Json::parse(meta->body);
    if (!json) return fail("Google Drive sent an answer DiaDraw cannot read");
    auto body = call(get(std::string(kDrive) + "/" + key + "?alt=media"), true);
    if (!body) return body.error();
    if (body->status == 404) return std::optional<Contents>{};
    if (to_valid_utf8(body->body) != body->body) return fail(json_name_of(*json), " is not UTF-8 text");
    return std::optional<Contents>(Contents{file_from(*json, provider), std::move(body->body)});
  }
  // OneDrive: the item's short-lived download address needs no token.
  auto meta = call(get(std::string(kGraph) + "/items/" + key), true);
  if (!meta) return meta.error();
  if (meta->status == 404) return std::optional<Contents>{};
  auto json = Json::parse(meta->body);
  const std::string* download = json ? string_member(*json, "@microsoft.graph.downloadUrl") : nullptr;
  if (!download) return fail("OneDrive gave no download address for this file");
  HttpResponse body = transport_(get(*download));
  if (body.status != 200) return fail("OneDrive: ", error_text(body));
  // Anything can be put in the app folder by hand; DiaDraw only edits text, as File › Open does.
  if (to_valid_utf8(body.body) != body.body) return fail(json_name_of(*json), " is not UTF-8 text");
  return std::optional<Contents>(Contents{file_from(*json, provider), std::move(body.body)});
}

Result<File> Drive::create(std::string_view name, std::string_view text) {
  if (account_.provider() == Provider::GoogleDrive) {
    const std::string boundary = "diadraw-" + base64url(random_bytes(12));
    std::string metadata = "{\"name\":";
    append_json_string(metadata, draw_file_name(name));
    metadata += ",\"mimeType\":\"text/plain\"}";
    std::string body = "--" + boundary + "\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n" + metadata +
                       "\r\n--" + boundary + "\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n" + std::string(text) +
                       "\r\n--" + boundary + "--";
    return file_call(with_body("POST",
                               std::string(kDriveUpload) + "?uploadType=multipart&fields=" + std::string(kDriveFields),
                               "multipart/related; boundary=" + boundary, std::move(body)));
  }
  return file_call(with_body("PUT",
                             std::string(kGraph) + "/special/approot:/" + io::percent_encode(draw_file_name(name)) +
                                 ":/content?@microsoft.graph.conflictBehavior=rename",
                             "text/plain", std::string(text)));
}

Result<File> Drive::save(const std::string& id, std::string_view text) {
  const std::string key = io::percent_encode(id);
  if (account_.provider() == Provider::GoogleDrive) {
    return file_call(with_body("PATCH",
                               std::string(kDriveUpload) + "/" + key + "?uploadType=media&fields=" + std::string(kDriveFields),
                               "text/plain; charset=UTF-8", std::string(text)));
  }
  return file_call(with_body("PUT", std::string(kGraph) + "/items/" + key + "/content", "text/plain", std::string(text)));
}

Result<File> Drive::rename(const std::string& id, std::string_view name) {
  const std::string key = io::percent_encode(id);
  if (account_.provider() == Provider::GoogleDrive) {
    return file_call(with_body("PATCH", std::string(kDrive) + "/" + key + "?fields=" + std::string(kDriveFields),
                               "application/json", json_name(name)));
  }
  return file_call(with_body("PATCH", std::string(kGraph) + "/items/" + key, "application/json", json_name(name)));
}

Result<bool> Drive::remove(const std::string& id) {
  const std::string key = io::percent_encode(id);
  Result<HttpResponse> response =
      account_.provider() == Provider::GoogleDrive
          ? call(with_body("PATCH", std::string(kDrive) + "/" + key, "application/json", "{\"trashed\":true}"))
          : call(HttpRequest{"DELETE", std::string(kGraph) + "/items/" + key, {}, {}});
  if (!response) return response.error();
  return true;
}

Drive& drive(Provider provider) {
  struct Services {
    Account google, microsoft;
    Drive google_drive, onedrive;
    explicit Services(const ClientConfig& c, const Transport& t)
        : google(Provider::GoogleDrive, c.google_client_id, c.google_client_secret, t),
          microsoft(Provider::OneDrive, c.onedrive_client_id, "", t),
          google_drive(google, t),
          onedrive(microsoft, t) {}
  };
  // Never destroyed: detached background requests may still use it while static destructors run.
  static Services& services = *new Services(client_config(), system_transport());  // NOLINT(cppcoreguidelines-owning-memory)
  return provider == Provider::GoogleDrive ? services.google_drive : services.onedrive;
}

namespace detail {
bool start_thread(std::function<void()> body) noexcept {
  try {
    std::thread(std::move(body)).detach();
    return true;
  } catch (...) {
    return false;
  }
}
}  // namespace detail

// ── Saving a document ──

void Saver::save(const Post& post, Place place, std::string text, std::function<void(const SaveOutcome&)> done) {
  Job job{post, std::move(place), std::move(text), {}};
  if (state_->busy && state_->next) job.done = std::move(state_->next->done);
  if (done) job.done.push_back(std::move(done));
  // One at a time: a newer text waits for the save under way, replacing any other that waits.
  if (state_->busy) state_->next = std::move(job);
  else start(state_, std::move(job));
}

void Saver::start(const std::shared_ptr<State>& state, Job job) {
  state->busy = true;
  const Place place = job.place;
  auto text = std::make_shared<const std::string>(std::move(job.text));
  auto finished = std::make_shared<Job>(std::move(job));
  in_background<File>(
      finished->post, [place, text] { return drive(place.provider).save(place.id, *text); },
      [state, finished, place, text](Result<File> saved) {
        SaveOutcome outcome;
        outcome.ok = saved.ok();
        outcome.place = place;
        outcome.text = *text;
        const std::string where(label(place.provider));
        if (saved) {
          outcome.message = "Saved " + place.name + " in " + where;
        } else if (!drive(place.provider).account().signed_in()) {
          outcome.needs_sign_in = true;
          outcome.message = "Sign in to " + where + ", then save again.";
        } else {
          outcome.message = "Could not save " + place.name + ": " + saved.error().message;
        }
        state->busy = false;
        // The waiting (newer) text starts before the callbacks run, so that a save they ask for
        // queues behind it rather than jumping ahead.
        if (state->next) {
          Job next = std::move(*state->next);
          state->next.reset();
          start(state, std::move(next));
        }
        for (const auto& done : finished->done) done(outcome);
      });
}

}  // namespace dd::host::cloud
