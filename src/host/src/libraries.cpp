// Port of packages/library/src/node.ts and packages/cli/src/lib.ts.

#include "host/libraries.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include "diadraw/base/js_regex.hpp"
#include "diadraw/base/text.hpp"
#include "diadraw/io/codec.hpp"
#include "diadraw/io/drawio_stencil.hpp"
#include "diadraw/library/builtin.hpp"
#include "host/files.hpp"

namespace dd::host {

namespace fs = std::filesystem;
using js::Regex;

const char* const kLibHelp = R"(Usage: diadraw lib <command> [options]

Commands:
  init [dir] [--name NAME]          Start a library: manifest, an example shape and fragment
  build [dir] [-o library.json]     Check a library and build it into one JSON bundle
  pack [dir] [-o dir]               Build and write <name>-<version>.ddlib (gzipped bundle)
  add <dir|file.ddlib|name@range> [--range R]
                                    Install a library into the cache and pin it in diadraw-lock.json
  install                           Install every library diadraw-lock.json pins
  list                              Built-in and installed libraries

Rendering and importing never fetch: they use the lockfile, the cache ($DIADRAW_CACHE or
~/.cache/diadraw/libraries) and the built-in libraries. The native CLI does not fetch URLs.
)";

namespace {

std::optional<std::string> read_text(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool write_text(const fs::path& path, std::string_view text) {
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  out << text;
  return static_cast<bool>(out);
}

/// An environment variable as UTF-8 (paths are UTF-8 everywhere, host/files.hpp). On Windows
/// `getenv` answers in the ANSI code page, which is UTF-8 only in hosts with a UTF-8 manifest
/// (the CLI, the WinUI app) and not in others, such as Python.
std::string env(const char* name) {
#if defined(_WIN32)
  std::wstring wide(name, name + std::char_traits<char>::length(name));  // names are ASCII
  const wchar_t* v = _wgetenv(wide.c_str());
  return v ? path_utf8(fs::path(v)) : "";
#else
  const char* v = std::getenv(name);
  return v ? v : "";
#endif
}

std::string folder(const std::string& name) {
  std::string f = name;
  if (auto slash = f.find('/'); slash != std::string::npos) f.replace(slash, 1, "__");
  return f;
}

const std::string* string_of(const Json* v) { return v && v->is_string() ? &v->as_string() : nullptr; }
std::string str(const Json& j, const char* key) {
  const std::string* s = string_of(j.get(key));
  return s ? *s : "";
}

// ── draw.io stencil sets ───────────────────────────────────────────────────────────────────────

std::string& drawio_override() {
  static std::string dir;
  return dir;
}

/// Where draw.io's converted stencil sets live: set by the host, `$DIADRAW_DRAWIO`, else next to
/// the sources.
fs::path drawio_dir() {
  if (!drawio_override().empty()) return utf8_path(drawio_override());
  if (std::string d = env("DIADRAW_DRAWIO"); !d.empty()) return utf8_path(d);
  // Installed on Linux: <prefix>/bin/<program> and <prefix>/share/diadraw/drawio.
  std::error_code ec;
  fs::path exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    fs::path installed = exe.parent_path().parent_path() / "share" / "diadraw" / "drawio";
    if (fs::exists(installed / "index.json.gz", ec)) return installed;
  }
#ifdef DIADRAW_DRAWIO_DIR
  return DIADRAW_DRAWIO_DIR;
#else
  return {};
#endif
}

/// The host file system, for offline library resolution in the core. Paths are UTF-8 strings
/// (host/files.hpp), so non-ASCII folders work on Windows in hosts without a UTF-8 code page,
/// such as Python.
class NativeFileSystem final : public library::FileSystem {
 public:
  std::optional<std::string> read(const std::string& path) const override { return read_text(utf8_path(path)); }
  std::vector<std::string> list(const std::string& dir) const override {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path folder = utf8_path(dir);
    if (!fs::is_directory(folder, ec)) return out;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
      std::string name = path_utf8(entry.path().filename());
      if (!name.empty()) out.push_back(std::move(name));
    }
    return out;
  }
  std::string join(const std::string& dir, const std::string& name) const override {
    return path_utf8(utf8_path(dir) / utf8_path(name));
  }
  std::string parent(const std::string& path) const override { return path_utf8(utf8_path(path).parent_path()); }
  std::string env(const std::string& name) const override { return host::env(name.c_str()); }
};

}  // namespace

const library::DiskLibraries& disk() {
  static const library::DiskLibraries libraries(std::make_shared<NativeFileSystem>(), path_utf8(drawio_dir()));
  return libraries;
}

const Json* drawio_index() { return disk().drawio_index(); }

namespace {

std::string counts(const Json& b) {
  auto size = [&](const char* key) {
    const Json* v = b.get(key);
    return v && v->is_object() ? v->as_object().size() : 0;
  };
  std::string out = std::to_string(size("shapes")) + " shapes";
  if (b.get("composites")) out += ", " + std::to_string(size("composites")) + " composites";
  if (b.get("fragments")) out += ", " + std::to_string(size("fragments")) + " fragments";
  if (const Json* open = b.get("open"); open && !open->is_null() && !(open->is_bool() && !open->as_bool())) out += ", open";
  return out;
}

void report(const std::string& where, const std::vector<library::ManifestIssue>& issues) {
  for (const auto& i : issues)
    std::cerr << where << ": " << (i.error ? "error" : "warning") << ": " << (i.path ? *i.path + ": " : "") << i.message
              << "\n";
}

struct Built {
  library::BuildResult result;
  fs::path dir;
};

/// Reads a library source folder (or a manifest file) and builds it.
Built build_folder(const fs::path& path) {
  std::error_code ec;
  fs::path file = fs::is_directory(path, ec) ? path / library::kManifestFile : path;
  Built built;
  built.dir = file.parent_path();
  auto text = read_text(file);
  if (!text) {
    built.result.issues.push_back({true, std::string("No ") + library::kManifestFile + " in " + path.string(), {}});
    return built;
  }
  auto manifest = Json::parse(*text);
  if (!manifest) {
    built.result.issues.push_back({true, file.string() + ": " + manifest.error().message, {}});
    return built;
  }
  fs::path dir = fs::weakly_canonical(built.dir, ec);
  built.result = library::build_bundle(*manifest, [&](const std::string& p) -> std::optional<std::string> {
    fs::path target = fs::weakly_canonical(dir / p, ec);
    auto rel = target.lexically_relative(dir).string();
    if (rel.rfind("..", 0) == 0) return std::nullopt;  // stay inside the library
    return read_text(target);
  });
  return built;
}

std::optional<Json> read_bundle_bytes(const std::string& bytes) {
  bool gz = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1F && static_cast<unsigned char>(bytes[1]) == 0x8B;
  auto text = gz ? io::gunzip(bytes) : std::optional<std::string>(bytes);
  return text ? library::parse_bundle(*text) : std::nullopt;
}

/// Loads a library from a folder, a manifest or a bundle file (`.ddlib` or JSON).
Json load_source(const std::string& source, const fs::path& base) {
  static const Regex kUrl("^https?:\\/\\/", "i");
  if (kUrl.test(source)) throw std::runtime_error(source + ": the native CLI does not fetch URLs");
  fs::path path = fs::path(source).is_absolute() ? fs::path(source) : base / source;
  std::error_code ec;
  if (!fs::exists(path, ec)) throw std::runtime_error(source + " not found");
  if (fs::is_directory(path, ec) || path.filename() == library::kManifestFile) {
    Built built = build_folder(path);
    if (!built.result.bundle) {
      std::string messages;
      for (const auto& i : built.result.issues) messages += (messages.empty() ? "" : "; ") + i.message;
      throw std::runtime_error(messages);
    }
    return *built.result.bundle;
  }
  auto bytes = read_text(path);
  auto bundle = bytes ? read_bundle_bytes(*bytes) : std::nullopt;
  if (!bundle) throw std::runtime_error(source + " is not a DiaDraw library");
  return *bundle;
}

std::optional<library::Lockfile> read_lockfile(const std::optional<std::string>& file) {
  return disk().read_lockfile(file);
}

void write_lockfile(const std::string& file, const library::Lockfile& lock) {
  Json libraries = Json::object();
  for (const auto& [spec, entry] : lock.libraries) {  // a std::map: sorted, as the TypeScript sorts
    Json e(Json::Object{{"version", Json(entry.version)}, {"resolved", Json(entry.resolved)}});
    if (entry.integrity) e.set("integrity", Json(*entry.integrity));
    libraries.set(spec, e);
  }
  Json root(Json::Object{{"lockfileVersion", Json(1)}, {"libraries", libraries}});
  write_text(file, root.dump_pretty(2) + "\n");
}

std::string write_to_cache(const Json& bundle, const std::string& dir) {
  fs::path file = fs::path(dir) / folder(str(bundle, "name")) / (str(bundle, "version") + ".json");
  write_text(file, library::serialize_bundle(bundle));
  return file.string();
}

std::string pack_file_name(const Json& bundle) {
  std::string name = str(bundle, "name");
  if (!name.empty() && name[0] == '@') name.erase(0, 1);
  if (auto slash = name.find('/'); slash != std::string::npos) name[slash] = '-';
  return name + "-" + str(bundle, "version") + ".ddlib";
}

std::vector<std::string> init_library(const fs::path& dir, const std::string& name) {
  std::vector<std::string> written;
  auto put = [&](const std::string& path, const std::string& text) {
    fs::path file = dir / path;
    std::error_code ec;
    if (fs::exists(file, ec)) return;
    write_text(file, text);
    written.push_back(file.string());
  };
  Json manifest(Json::Object{
      {"name", Json(name)},
      {"version", Json("0.1.0")},
      {"description", Json("Shapes for DiaDraw")},
      {"license", Json("MIT")},
      {"source", Json(Json::Object{{"kind", Json("original")}})},
      {"shapes", Json(Json::Object{{"example", Json(Json::Object{{"label", Json("Example")},
                                                                 {"category", Json("Examples")},
                                                                 {"kind", Json("component")},
                                                                 {"svg", Json("shapes/example.svg")},
                                                                 {"width", Json(48)},
                                                                 {"height", Json(48)}})}})},
      {"fragments",
       Json(Json::Object{{"pair", Json(Json::Object{{"label", Json("Two connected shapes")},
                                                    {"params", Json(Json::Array{Json(Json::Object{{"name", Json("from")}, {"default", Json("a")}}),
                                                                                Json(Json::Object{{"name", Json("to")}, {"default", Json("b")}})})},
                                                    {"text", Json("fragments/pair.draw")}})}})}});
  put(library::kManifestFile, manifest.dump_pretty(2) + "\n");
  put("shapes/example.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 48 48\"><rect x=\"4\" y=\"4\" width=\"40\" height=\"40\" "
      "rx=\"8\" fill=\"#dbeafe\" stroke=\"#1d4ed8\" stroke-width=\"2\"/></svg>\n");
  put("fragments/pair.draw",
      "${from} = component \"${from}\" shape ${alias}.example\n${to} = component \"${to}\" shape ${alias}.example\n"
      "${from} -> ${to}\n");
  static const Regex kNonId("[^a-z0-9_]", "gi");
  std::string base = name.substr(name.find('/') == std::string::npos ? 0 : name.rfind('/') + 1);
  put("README.md", "# " + name +
                       "\n\nA DiaDraw shape library. Build it with `diadraw lib build`, pack it with `diadraw lib pack`, "
                       "and use it in a document with:\n\n```\nuse \"" +
                       name + "@^0.1\" as " + kNonId.replace(base, "_", true) + "\n```\n");
  return written;
}

}  // namespace

void set_drawio_dir(const std::string& dir) { drawio_override() = dir; }

std::string cache_dir() { return disk().cache_dir(); }

library::LibrarySource cache_source(const std::string& dir) { return disk().cache_source(dir); }

library::LibrarySource builtin_source() { return disk().builtin_source(); }

std::string integrity_of(const Json& bundle) {
  return "sha256-" + io::base64_encode(io::sha256(library::serialize_bundle(bundle)));
}

std::optional<std::string> find_lockfile(const std::string& from) {
  std::error_code ec;
  return disk().find_lockfile(path_utf8(fs::absolute(utf8_path(from), ec)));
}

library::Resolution resolve_for_file(const std::vector<LibraryUse>& uses, const std::string& file) {
  std::error_code ec;
  return disk().resolve_for_file(uses, path_utf8(fs::absolute(utf8_path(file), ec)));
}

std::vector<std::pair<std::string, std::vector<std::string>>> list_cache(const std::string& dir) {
  return disk().list_cache(dir);
}

/// The licence notice draw.io's stencils come with (stencils/LICENSE in jgraph/drawio).
const char* const kDrawioNotes =
    "draw.io stencils may not be used in, distributed with or incorporated into Atlassian products or the "
    "Atlassian marketplace without written permission (stencils/LICENSE in jgraph/drawio). Vendor icon sets also "
    "carry the vendors' trademark terms.";

/// `lib build --drawio`: one library per stencil set, named `drawio-<set>`.
int build_drawio(const fs::path& file, const LibOptions& options, const fs::path& cwd) {
  if (!options.output) {
    std::cerr << "lib build --drawio: pass -o <dir> for the libraries\n";
    return 2;
  }
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    std::cerr << "lib build --drawio: " << file.lexically_normal().string() << " not found\n";
    return 1;
  }
  std::vector<std::string> wanted;
  if (options.set) {
    std::string_view list = *options.set;
    std::size_t start = 0;
    while (start <= list.size()) {
      std::size_t comma = list.find(',', start);
      if (comma == std::string_view::npos) comma = list.size();
      wanted.push_back(js::to_lower(js_trim(list.substr(start, comma - start))));
      start = comma + 1;
    }
  }
  auto has = [&](const std::string& v) { return std::find(wanted.begin(), wanted.end(), v) != wanted.end(); };
  std::string version = "1.0.0";
  for (const fs::path& candidate : {file.parent_path() / "VERSION", file.parent_path() / ".." / ".." / ".." / ".." / "VERSION"})
    if (fs::exists(candidate, ec)) {
      std::string v(js_trim(read_text(candidate).value_or("")));
      static const Regex kVersion(R"(^\d+\.\d+\.\d+$)");
      if (kVersion.test(v)) version = v;
      break;
    }
  fs::path out = (cwd / *options.output).lexically_normal();
  fs::create_directories(out, ec);
  static const Regex kNonName("[^a-z0-9]+", "g");
  static const Regex kSeparators("[._]", "g");
  int written = 0;
  std::string text = read_text(file).value_or("");
  for (const auto& set : io::read_stencil_sets(text, file.filename().string())) {
    std::string short_name = set.name.starts_with("mxgraph.") ? set.name.substr(8) : set.name;
    std::string file_stem = set.file.ends_with(".xml") ? set.file.substr(0, set.file.size() - 4) : set.file;
    if (!wanted.empty() && !has(short_name) && !has(set.name) && !has(file_stem)) continue;
    if (set.shapes.empty()) continue;
    std::string name = "drawio-" + kNonName.replace(short_name, "-", true);
    std::string category = kSeparators.replace(short_name, " ", true);
    Json shapes = Json::object(), mapping = Json::object();
    for (const auto& shape : set.shapes) {
      shapes.set(shape.id, Json(Json::Object{
                               {"label", Json(shape.label)},
                               {"category", Json(category)},
                               {"svg", Json(shape.body)},
                               {"viewBox", Json("0 0 " + js_number_string(shape.width) + " " + js_number_string(shape.height))},
                               {"width", Json(shape.width)},
                               {"height", Json(shape.height)},
                           }));
      mapping.set(shape.style, Json(shape.id));
    }
    Json bundle(Json::Object{
        {"format", Json("diadraw-library")},
        {"formatVersion", Json(1)},
        {"name", Json(name)},
        {"version", Json(version)},
        {"description", Json("draw.io's " + set.name + " stencils (" + set.file + "), converted to SVG.")},
        {"license", Json("Apache-2.0")},
        {"licenseNotes", Json(kDrawioNotes)},
        {"source", Json(Json::Object{{"kind", Json("drawio-stencils")},
                                     {"url", Json("https://github.com/jgraph/drawio")},
                                     {"sets", Json(Json::Array{Json(set.file)})}})},
        {"shapes", std::move(shapes)},
        {"mappings", Json(Json::Object{{"drawio", std::move(mapping)}})},
    });
    fs::path target = out / (name + "-" + version + ".json");
    write_text(target, library::serialize_bundle(bundle));
    ++written;
    std::cout << name << "@" << version << ": " << set.shapes.size() << " shapes";
    if (!set.skipped.empty()) std::cout << " (" << set.skipped.size() << " skipped)";
    std::cout << " → " << target.string() << "\n";
  }
  std::cerr << "warning: licence restricted: Apache-2.0 with extra terms. " << kDrawioNotes
            << " These libraries are for local use; do not ship them with DiaDraw.\n";
  std::cout << written << " libraries\n";
  return written ? 0 : 1;
}

int run_lib(const std::vector<std::string>& args, const LibOptions& options) {
  std::string sub = args.empty() ? "" : args[0];
  std::optional<std::string> target = args.size() > 1 ? std::optional(args[1]) : std::nullopt;
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (sub == "init") {
    fs::path dir = fs::weakly_canonical(cwd / target.value_or("."), ec);
    static const Regex kNonName("[^a-z0-9._-]+", "g");
    std::string name = options.name ? *options.name : kNonName.replace(js::to_lower(dir.filename().string()), "-", true);
    auto written = init_library(dir, name);
    for (const auto& f : written) std::cout << "created " << f << "\n";
    if (written.empty()) std::cout << dir.string() << " already holds a library\n";
    return 0;
  }
  if (sub == "build") {
    if (options.drawio) return build_drawio(cwd / *options.drawio, options, cwd);
    Built built = build_folder(cwd / target.value_or("."));
    report(target.value_or("."), built.result.issues);
    if (!built.result.bundle) return 1;
    fs::path out = options.output ? cwd / *options.output : built.dir / "dist" / "library.json";
    write_text(out, library::serialize_bundle(*built.result.bundle));
    std::cout << str(*built.result.bundle, "name") << "@" << str(*built.result.bundle, "version") << ": "
              << counts(*built.result.bundle) << " → " << out.string() << "\n";
    return 0;
  }
  if (sub == "pack") {
    Built built = build_folder(cwd / target.value_or("."));
    report(target.value_or("."), built.result.issues);
    if (!built.result.bundle) return 1;
    fs::path dir = options.output ? cwd / *options.output : built.dir / "dist";
    fs::path file = dir / pack_file_name(*built.result.bundle);
    write_text(file, io::gzip(library::serialize_bundle(*built.result.bundle)));
    std::cout << file.string() << "\n" << integrity_of(*built.result.bundle) << "\n";
    return 0;
  }
  if (sub == "add") {
    if (!target) {
      std::cerr << "lib add: missing library (a folder, a .ddlib file or name@range)\n";
      return 2;
    }
    try {
      std::string lockfile = find_lockfile(cwd.string()).value_or((cwd / kLockfile).string());
      fs::path base = fs::path(lockfile).parent_path();
      static const Regex kSpec(R"re(^(@[\w.-]+\/)?[\w.-]+(@.+)?$)re");
      std::optional<std::string> range = options.range;
      std::shared_ptr<const Json> bundle;
      std::string resolved;
      if (kSpec.test(*target) && !fs::exists(cwd / *target, ec)) {
        auto at = target->rfind('@');
        std::string name = at != std::string::npos && at > 0 ? target->substr(0, at) : *target;
        std::string wanted = at != std::string::npos && at > 0 ? target->substr(at + 1) : "*";
        auto builtin = builtin_source();
        auto resolution = library::resolve_uses({{name, wanted, "x"}}, {cache_source(cache_dir()), builtin});
        const Json* hit = resolution.set.library("x");
        if (!hit) throw std::runtime_error(resolution.unresolved.empty() ? *target + " not found" : resolution.unresolved[0].message);
        bundle = std::make_shared<const Json>(*hit);
        auto builtin_versions = builtin.versions(name);
        resolved = std::find(builtin_versions.begin(), builtin_versions.end(), str(*hit, "version")) != builtin_versions.end()
                       ? "builtin"
                       : "cache:" + name + "@" + str(*hit, "version");
        if (!range) range = wanted == "*" ? "^" + str(*hit, "version") : wanted;
      } else {
        bundle = std::make_shared<const Json>(load_source((cwd / *target).string(), cwd));
        std::string rel = fs::weakly_canonical(cwd / *target, ec).lexically_relative(fs::weakly_canonical(base, ec)).string();
        resolved = rel.empty() ? "." : rel;
      }
      write_to_cache(*bundle, cache_dir());
      std::string spec = str(*bundle, "name") + "@" + (range ? *range : "^" + str(*bundle, "version"));
      library::Lockfile lock = read_lockfile(lockfile).value_or(library::Lockfile{});
      lock.libraries[spec] = {str(*bundle, "version"), resolved, integrity_of(*bundle)};
      write_lockfile(lockfile, lock);
      static const Regex kNonId("[^A-Za-z0-9_]+", "g");
      std::string name = str(*bundle, "name");
      std::string alias = kNonId.replace(name.substr(name.find('/') == std::string::npos ? 0 : name.rfind('/') + 1), "_", true);
      auto license = library::check_license(*bundle);
      if (license.status != library::LicenseStatus::Allowed)
        std::cerr << "warning: licence " << library::to_string(license.status) << ": " << license.reason << "\n";
      std::cout << "added " << name << "@" << str(*bundle, "version") << " as " << spec << " (" << lockfile << ")\n"
                << "use it with: use \"" << spec << "\" as " << alias << "\n";
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "lib add: " << e.what() << "\n";
      return 1;
    }
  }
  if (sub == "install") {
    auto lockfile = find_lockfile(cwd.string());
    auto lock = read_lockfile(lockfile);
    if (!lockfile || !lock) {
      std::cout << "nothing to install (no diadraw-lock.json)\n";
      return 0;
    }
    auto cache = cache_source(cache_dir());
    std::vector<std::string> installed, cached;
    std::vector<std::pair<std::string, std::string>> failed;
    for (const auto& [spec, entry] : lock->libraries) {
      auto at = spec.rfind('@');
      std::string name = at != std::string::npos && at > 0 ? spec.substr(0, at) : spec;
      auto have = cache.load(name, entry.version);
      if (have && (!entry.integrity || integrity_of(*have) == *entry.integrity)) {
        cached.push_back(spec);
        continue;
      }
      try {
        std::shared_ptr<const Json> bundle;
        if (entry.resolved == "builtin") bundle = builtin_source().load(name, entry.version);
        else if (entry.resolved.rfind("cache:", 0) != 0)
          bundle = std::make_shared<const Json>(load_source(entry.resolved, fs::path(*lockfile).parent_path()));
        if (!bundle) throw std::runtime_error(entry.resolved + " is not available");
        if (str(*bundle, "version") != entry.version)
          throw std::runtime_error(entry.resolved + " holds " + str(*bundle, "version") + ", the lockfile pins " + entry.version);
        if (entry.integrity && integrity_of(*bundle) != *entry.integrity)
          throw std::runtime_error("Integrity check failed for " + spec);
        write_to_cache(*bundle, cache_dir());
        installed.push_back(spec);
      } catch (const std::exception& e) {
        failed.emplace_back(spec, e.what());
      }
    }
    for (const auto& s : installed) std::cout << "installed " << s << "\n";
    for (const auto& s : cached) std::cout << "cached " << s << "\n";
    for (const auto& [s, m] : failed) std::cerr << "failed " << s << ": " << m << "\n";
    if (installed.empty() && cached.empty() && failed.empty()) std::cout << "nothing to install (no diadraw-lock.json)\n";
    return failed.empty() ? 0 : 1;
  }
  if (sub == "list") {
    std::cout << "built in:\n";
    for (const auto& b : library::builtin_bundles().as_array())
      std::cout << "  " << str(b, "name") << "@" << str(b, "version") << "  " << counts(b) << "\n";
    if (const Json* index = drawio_index()) {
      const Json* drawio = index->get("drawio");
      std::cout << "draw.io stencils (draw.io " << (drawio ? str(*drawio, "version") : "")
                << "; RESTRICTED: Apache-2.0, not for use in or with Atlassian products without permission; vendor icons "
                   "are subject to the vendors' trademark terms; see assets/NOTICE):\n";
      if (const Json* sets = index->get("sets"); sets && sets->is_array())
        for (const auto& s : sets->as_array()) {
          const Json* shapes = s.get("shapes");
          const Json* vendor = s.get("vendor");
          std::cout << "  " << str(s, "name") << "@" << str(s, "version") << "  "
                    << (shapes && shapes->is_number() ? js_number_string(shapes->as_number()) : "0") << " shapes  [restricted"
                    << (vendor && vendor->is_bool() && vendor->as_bool() ? ", vendor trademarks" : "") << "]\n";
        }
    }
    std::string dir = cache_dir();
    std::cout << "installed (" << dir << "):\n";
    auto cachedLibs = list_cache(dir);
    for (const auto& [name, versions] : cachedLibs) {
      std::cout << "  " << name << "  ";
      for (std::size_t i = 0; i < versions.size(); ++i) std::cout << (i ? ", " : "") << versions[i];
      std::cout << "\n";
    }
    if (cachedLibs.empty()) std::cout << "  (none)\n";
    return 0;
  }
  if (sub.empty()) {
    std::cout << kLibHelp;
    return 0;
  }
  std::cerr << "Unknown lib command \"" << sub << "\"\n\n" << kLibHelp;
  return 2;
}

}  // namespace dd::host
