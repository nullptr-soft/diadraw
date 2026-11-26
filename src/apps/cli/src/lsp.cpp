// `diadraw lsp`: the language server (dd::lsp::Server) over stdio, with the libraries on this
// machine's disk.

#include "lsp.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "diadraw/base/json.hpp"
#include "diadraw/lsp/server.hpp"
#include "host/libraries.hpp"

namespace dd::cli {

namespace {

std::optional<Json> read_message(std::istream& in) {
  std::size_t length = 0;
  std::string line;
  bool any = false;
  while (std::getline(in, line)) {
    any = true;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    static const std::string kLength = "content-length:";
    std::string lower = line;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.rfind(kLength, 0) == 0) length = std::stoul(line.substr(kLength.size()));
  }
  if (!any || !in) return std::nullopt;
  std::string body(length, '\0');
  in.read(body.data(), static_cast<std::streamsize>(length));
  if (static_cast<std::size_t>(in.gcount()) != length) return std::nullopt;
  auto json = Json::parse(body);
  return json ? std::optional<Json>(std::move(*json)) : std::optional<Json>(Json::object());
}

void write_message(const Json& message) {
  std::string body = message.dump();
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  std::cout.flush();
}

}  // namespace

int run_lsp() {
  std::ios::sync_with_stdio(false);
  std::error_code ec;
  // The host's libraries live as long as the process; the resolver only borrows them.
  std::shared_ptr<const library::DiskLibraries> disk(&host::disk(), [](const library::DiskLibraries*) {});
  lsp::Server server(write_message, lsp::disk_resolver(disk, {}, std::filesystem::current_path(ec).string()));
  for (;;) {
    auto message = read_message(std::cin);
    if (!message) return server.shutting_down() ? 0 : 1;
    server.handle(*message);
    if (auto code = server.exit_code()) return *code;
  }
}

}  // namespace dd::cli
