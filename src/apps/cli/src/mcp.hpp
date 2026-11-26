#pragma once

namespace dd::cli {

/// `diadraw mcp`: serves the Model Context Protocol (host/mcp.hpp) over stdin and stdout, one
/// JSON-RPC message per line, until stdin closes.
int run_mcp();

}  // namespace dd::cli
