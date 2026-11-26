#pragma once

// Language features over source text and byte offsets, a port of
// packages/language-server/src/features.ts. The server adapts them to LSP positions.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diadraw/dsl/parse.hpp"
#include "diadraw/library/library.hpp"

namespace dd::lsp {

struct Symbol {
  std::string name;
  std::string detail;
  std::string kind;  ///< `element`, `view`, `group` or `model`
  dsl::Span range;
  dsl::Span selection;
  std::vector<Symbol> children;
};

/// An outline: the model block with elements nested under their parents, then the views.
std::vector<Symbol> document_symbols(const dsl::ParseResult& result);

struct Reference {
  std::string ref;
  dsl::Span span;
};
/// The dotted reference under the cursor and its span.
std::optional<Reference> reference_at(std::string_view text, std::size_t offset);

/// Resolves a reference like the DSL does: innermost scope first, then outwards.
const Element* resolve_reference(const dsl::ParseResult& result, const std::string& ref, std::size_t offset);

std::optional<dsl::Span> definition(std::string_view text, std::size_t offset, const dsl::ParseResult& result);

struct Hover {
  std::string markdown;
  dsl::Span span;
};
std::optional<Hover> hover(std::string_view text, std::size_t offset, const dsl::ParseResult& result);

struct Completion {
  std::string label;
  std::string kind;  ///< `keyword`, `element`, `kind`, `value`, `property` or `snippet`
  std::optional<std::string> detail;
  /// Text to insert instead of the label (LSP snippet syntax for `snippet`).
  std::optional<std::string> insert_text;
};

/// What completion knows about libraries: the ones the document uses, and the installable ones.
struct LibraryContext {
  library::LibrarySet set;
  struct Available {
    std::string name;
    std::string version;
    std::optional<std::string> description;
  };
  std::vector<Available> available;
};

/// Context-aware completion from the text before the cursor on the current line.
std::vector<Completion> completions(std::string_view text, std::size_t offset, const dsl::ParseResult& result,
                                    const LibraryContext* libraries = nullptr);

}  // namespace dd::lsp
