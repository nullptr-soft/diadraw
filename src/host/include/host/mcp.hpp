#pragma once

// DiaDraw as a Model Context Protocol server, so that coding agents (Claude Code, Claude Desktop
// and other MCP clients) can write a diagram, check it, render it and look at the picture. The
// server answers JSON-RPC 2.0 messages; the transport is up to the host (`diadraw mcp` serves
// newline-delimited JSON on stdin and stdout). See docs/guide/agents.md.
//
// Tools: `check`, `render` (a PNG the agent can see, or SVG, and optionally a file), `format`,
// `import` (Mermaid, PlantUML, Structurizr, draw.io, BPMN), `reference` (kinds, shapes, themes,
// layouts and libraries) and `examples` (the sample documents). Documents come inline (`source`)
// or from a file (`path`); libraries resolve offline, as the CLI resolves them.

#include <optional>
#include <string>
#include <string_view>

#include "diadraw/base/json.hpp"

namespace dd::host {

struct McpOptions {
  /// Where relative paths resolve, and where the lockfile of an inline document is looked for.
  std::string working_dir;
  /// The version reported in `serverInfo`.
  std::string version;
};

class McpServer {
 public:
  explicit McpServer(McpOptions options);

  /// Handles one message: the response to a request, or nothing for a notification or a
  /// response. Never throws.
  [[nodiscard]] std::optional<Json> handle(const Json& message) const;

  /// A message as one line of the stdio transport: its JSON, with any byte that is not valid
  /// UTF-8 (a stray byte in a user's file, say) replaced by U+FFFD, since MCP messages are UTF-8.
  [[nodiscard]] static std::string serialize(const Json& message);

  /// The response to a message that is not JSON (JSON-RPC's parse error, with a null id).
  [[nodiscard]] static Json parse_error(std::string_view detail);

  /// The protocol versions this server speaks, newest first.
  /// (2025-03-26 is left out: it requires JSON-RPC batches, which the others do not have.)
  static constexpr std::string_view kProtocolVersions[] = {"2025-06-18", "2024-11-05"};

 private:
  [[nodiscard]] Json initialize(const Json& params) const;
  [[nodiscard]] Json call_tool(const Json& params) const;

  McpOptions options_;
};

}  // namespace dd::host
