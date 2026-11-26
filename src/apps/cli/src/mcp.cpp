// `diadraw mcp`: the MCP server (dd::host::McpServer) over stdio, MCP's local transport: one
// JSON-RPC message per line on stdin, one per line on stdout. Nothing else may reach stdout.

#include "mcp.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "diadraw/diadraw.h"
#include "host/files.hpp"
#include "host/mcp.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#endif

namespace dd::cli {

int run_mcp() {
#ifdef _WIN32
  // Lines end in \n alone, whatever the platform.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::ios::sync_with_stdio(false);
  std::error_code ec;
  const host::McpServer server({host::path_utf8(std::filesystem::current_path(ec)), dd_version()});
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    auto message = Json::parse(line);
    std::optional<Json> reply = message ? server.handle(*message) : host::McpServer::parse_error(message.error().message);
    if (!reply) continue;
    std::cout << host::McpServer::serialize(*reply) << '\n';
    std::cout.flush();
  }
  return 0;
}

}  // namespace dd::cli
