// The native `diadraw` command line, a port of packages/cli: render (SVG, draw.io, BPMN), check,
// format, import and the `lib` commands. Libraries resolve offline from the lockfile, the cache,
// the built-in libraries and draw.io's stencil sets.

#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <variant>

#include "diadraw/diadraw.h"
#include "diadraw/dsl/parse.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/io/import.hpp"
#include "diadraw/model/views.hpp"
#include "host/diagram.hpp"
#include "host/files.hpp"
#include "host/libraries.hpp"
#include "lsp.hpp"
#include "mcp.hpp"

namespace {

const char* kHelp = R"(Usage: diadraw <command> [options]

Commands:
  render <file.draw> [-o out.svg|png|pdf|drawio|bpmn] [--view NAME] [--scale N]
                                   Render a view (SVG to stdout without -o); .drawio writes
                                   every view as a draw.io page, .bpmn as BPMN 2.0 with diagrams;
                                   --scale sets the PNG pixel density, above 0 and at most 8
  check <file.draw>                Report syntax and model errors
  format <file.draw> [--write]     Print the file in canonical form
  import <file> [-o out.draw] [--from mermaid|plantuml|structurizr|drawio|bpmn] [--geometry keep|drop]
                                   Convert a Mermaid, PlantUML, Structurizr, draw.io or BPMN file
                                   (.drawio, .xml, .drawio.svg, .drawio.png, .bpmn); draw.io pages
                                   and BPMN diagrams keep their positions unless --geometry drop
  lib <init|build|pack|add|install|list>
                                   Shape libraries; see diadraw lib --help
  lsp                              Run the language server on stdin and stdout
  mcp                              Run the MCP server on stdin and stdout, for AI agents
                                   (Claude Code, Claude Desktop); see docs/guide/agents.md

Options:
  -h, --help       Show this help
  -v, --version    Show the version
)";

using dd::host::ends_with_ci;
using dd::host::read_file;
using dd::host::write_file;

/// Options as node's `util.parseArgs` reads them for the TypeScript CLI.
struct Args {
  std::map<std::string, std::string, std::less<>> values;
  std::set<std::string, std::less<>> flags;
  std::vector<std::string> positionals;
  std::optional<std::string> get(std::string_view name) const {
    auto it = values.find(name);
    return it == values.end() ? std::nullopt : std::optional(it->second);
  }
};

std::optional<Args> parse_args(int argc, char** argv, std::string& error) {
  static const std::set<std::string, std::less<>> kBooleans{"help", "version", "write"};
  static const std::set<std::string, std::less<>> kStrings{"output", "view", "from", "geometry", "name",
                                                           "range", "drawio", "set", "scale"};
  static const std::map<char, std::string> kShort{{'h', "help"}, {'v', "version"}, {'o', "output"}};
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--") {
      for (++i; i < argc; ++i) args.positionals.emplace_back(argv[i]);
      break;
    }
    std::string name;
    std::optional<std::string> inline_value;
    if (a.rfind("--", 0) == 0) {
      name = a.substr(2);
      if (auto eq = name.find('='); eq != std::string::npos) {
        inline_value = name.substr(eq + 1);
        name.resize(eq);
      }
    } else if (a.size() >= 2 && a[0] == '-') {
      auto it = kShort.find(a[1]);
      if (it == kShort.end()) {
        error = "Unknown option '" + a + "'";
        return std::nullopt;
      }
      name = it->second;
      if (a.size() > 2) inline_value = a.substr(2);
    } else {
      args.positionals.push_back(a);
      continue;
    }
    if (kBooleans.count(name)) {
      args.flags.insert(name);
    } else if (kStrings.count(name)) {
      if (inline_value) args.values[name] = *inline_value;
      else if (i + 1 < argc) args.values[name] = argv[++i];
      else {
        error = "Option '--" + name + " <value>' argument missing";
        return std::nullopt;
      }
    } else {
      error = "Unknown option '" + a + "'";
      return std::nullopt;
    }
  }
  return args;
}

std::string format_diagnostic(const std::string& file, const dd::dsl::Diagnostic& d) {
  return file + ":" + dd::host::format_diagnostic(d) + "\n";
}

int import_command(const std::string& file, const std::string& source, const Args& args) {
  std::optional<dd::io::Format> format;
  if (auto from = args.get("from")) format = dd::host::format_named(*from);
  else format = dd::io::detect_format(file, source);
  if (!format) {
    std::cerr << "import: cannot tell the format of " << file
              << "; pass --from mermaid|plantuml|structurizr|drawio|bpmn\n";
    return 2;
  }
  auto result = dd::host::import_text(source, *format, file, args.get("geometry") != "drop");
  if (!result) {
    std::cerr << "import: " << result.error().message << "\n";
    return 1;
  }
  for (const auto& d : result->report.dropped)
    std::cerr << file << (d.line ? ":" + std::to_string(*d.line) : "") << ": dropped: " << d.message << "\n";
  for (const auto& note : result->report.notes) std::cerr << file << ": note: " << note << "\n";
  std::string text = dd::dsl::print(result->model);
  if (auto out = args.get("output")) return write_file(*out, text) ? 0 : 1;
  std::cout << text;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string error;
  auto parsed = parse_args(argc, argv, error);
  if (!parsed) {
    std::cerr << error << "\n\n" << kHelp;
    return 2;
  }
  const Args& args = *parsed;
  if (args.flags.count("version")) {
    std::cout << dd_version() << "\n";
    return 0;
  }
  std::optional<std::string> command = args.positionals.empty() ? std::nullopt : std::optional(args.positionals[0]);
  if (command == "lib") {
    if (args.flags.count("help")) {
      std::cout << dd::host::kLibHelp;
      return 0;
    }
    dd::host::LibOptions options{args.get("output"), args.get("name"), args.get("range"), args.get("drawio"),
                                args.get("set")};
    return dd::host::run_lib(std::vector<std::string>(args.positionals.begin() + 1, args.positionals.end()), options);
  }
  if (command == "lsp") return dd::cli::run_lsp();
  if (command == "mcp") return dd::cli::run_mcp();
  if (args.flags.count("help") || !command) {
    std::cout << kHelp;
    return 0;
  }
  if (*command != "render" && *command != "check" && *command != "format" && *command != "import") {
    std::cerr << "Unknown command \"" << *command << "\"\n\n" << kHelp;
    return 2;
  }
  if (args.positionals.size() < 2) {
    std::cerr << *command << ": missing input file\n";
    return 2;
  }
  const std::string file = args.positionals[1];
  auto source = read_file(file);
  if (!source) {
    std::cerr << *command << ": cannot read " << file << "\n";
    return 1;
  }
  if (*command == "import") return import_command(file, *source, args);

  dd::host::Analysis analysis = dd::host::analyze(*source, file);
  const auto& result = analysis.parse;
  const int errors = analysis.errors;
  for (const auto& d : result.diagnostics) std::cerr << format_diagnostic(file, d);
  // Libraries resolve offline (lockfile, cache, built-ins); missing ones are warnings.
  if (*command != "format")
    for (const auto& w : analysis.library_warnings) std::cerr << file << ": warning: " << w << "\n";
  dd::LibraryArt art = analysis.libraries.set.renderer();

  if (*command == "check") {
    if (errors == 0) std::cout << file << ": ok\n";
    return errors == 0 ? 0 : 1;
  }
  if (*command == "format") {
    if (errors > 0) return 1;
    std::string formatted = dd::dsl::print(result.model);
    if (args.flags.count("write")) return write_file(file, formatted) ? 0 : 1;
    std::cout << formatted;
    return 0;
  }

  // render
  if (errors > 0) return 1;
  dd::Model model = dd::with_default_view(result.model);
  auto output = args.get("output");
  using Format = dd::host::ExportFormat;
  Format format = Format::Svg;
  if (output) {
    if (ends_with_ci(*output, ".drawio") || ends_with_ci(*output, ".dio")) format = Format::Drawio;
    else if (ends_with_ci(*output, ".bpmn")) format = Format::Bpmn;
    else if (ends_with_ci(*output, ".png")) format = Format::Png;
    else if (ends_with_ci(*output, ".pdf")) format = Format::Pdf;
  }
  dd::host::ExportOptions options;
  options.view = args.get("view");
  if (auto scale = args.get("scale")) {
    try {
      options.scale = std::stod(*scale);
    } catch (const std::exception&) {
      std::cerr << "render: --scale takes a number\n";
      return 2;
    }
  }
  auto exported = dd::host::export_diagram(model, format, options, art);
  if (const auto* failure = std::get_if<dd::host::ExportError>(&exported)) {
    std::cerr << "render: " << failure->message << "\n";
    using Kind = dd::host::ExportError::Kind;
    return failure->kind == Kind::Unsupported || failure->kind == Kind::InvalidArgument ? 2 : 1;
  }
  const auto& done = std::get<dd::host::Exported>(exported);
  for (const auto& hint : done.ignored_hints)
    std::cerr << file << ": warning: " << dd::host::hint_warning(hint) << "\n";
  if (!output) {
    std::cout << done.bytes;
    return 0;
  }
  return write_file(*output, done.bytes) ? 0 : 1;
}
