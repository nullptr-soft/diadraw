#pragma once

namespace dd::cli {

/// `diadraw lsp`: serves the Language Server Protocol over stdin and stdout until `exit`.
int run_lsp();

}  // namespace dd::cli
