#pragma once

// Shape libraries, a port of packages/library: semver ranges, manifests and their checks,
// bundles (built libraries), the set of libraries a document uses (art for the renderer, the
// palette, fragments, mappings) and resolution against sources. Bundles stay JSON values, so
// they round-trip exactly (key order included) and integrity hashes hold.
//
// Everything here is portable; reading the cache and the lockfile from disk is up to hosts.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "diadraw/base/json.hpp"
#include "diadraw/model/model.hpp"
#include "diadraw/render/svg.hpp"

namespace dd::library {

// ── semver ─────────────────────────────────────────────────────────────────────────────────────

struct Version {
  double major = 0, minor = 0, patch = 0;
  /// Pre-release identifiers: numbers or strings.
  std::vector<std::variant<double, std::string>> pre;
};

std::optional<Version> parse_version(std::string_view text);
bool is_version(std::string_view text);
int compare_versions(const Version& a, const Version& b);
bool is_valid_range(std::string_view range);
bool satisfies(std::string_view version, std::string_view range);
/// The highest version satisfying the range.
std::optional<std::string> max_satisfying(const std::vector<std::string>& versions, std::string_view range);

// ── manifests ──────────────────────────────────────────────────────────────────────────────────

/// The file a library's source folder is described by.
inline constexpr const char* kManifestFile = "diadraw-library.json";

struct ManifestIssue {
  bool error = true;  ///< else a warning
  std::string message;
  std::optional<std::string> path;
};

bool is_library_name(std::string_view name);
/// Checks a manifest (source or built); returns every problem rather than stopping at one.
std::vector<ManifestIssue> validate_manifest(const Json& manifest);
/// Checks the fragments of a built library: each must parse inside `model { … }`.
std::vector<ManifestIssue> validate_fragments(const Json& bundle, const std::string& alias = "lib");
/// Fills a fragment's placeholders. Unset parameters take their default, else their name.
std::string expand_fragment(const Json& fragment, const std::map<std::string, std::string, std::less<>>& values,
                            const std::string& alias);

enum class LicenseStatus { Allowed, Restricted, Disallowed, Unknown };
struct LicenseCheck {
  LicenseStatus status;
  std::string reason;
};
const char* to_string(LicenseStatus status);
/// Licences a library may carry and still be shipped with DiaDraw.
const std::vector<std::string>& allowed_licenses();
LicenseCheck check_license(const Json& manifest);

// ── bundles ────────────────────────────────────────────────────────────────────────────────────

struct SvgArt {
  std::string body;
  std::string view_box;
  double width = 0, height = 0;
};
/// Makes SVG markup safe to inline: the first `<svg>` element read into an allowlist of SVG
/// elements and attributes and written back out escaped, so no scripts, event handlers, styles,
/// foreign objects, animation or external references; ids prefixed. The inner markup and the view
/// box; nullopt without an `<svg>` or without a size.
std::optional<SvgArt> sanitize_svg(std::string_view svg, const std::string& id_prefix);
/// The same rules for the inner markup of an `<svg>` (a bundle's shape drawing). Without a prefix
/// it is idempotent: a sanitised drawing comes back as it is.
std::string sanitize_svg_body(std::string_view body, std::string_view id_prefix = {});

/// Reads a file next to the manifest; nullopt when missing.
using FileReader = std::function<std::optional<std::string>(const std::string& path)>;

struct BuildResult {
  std::optional<Json> bundle;
  std::vector<ManifestIssue> issues;
};
/// Builds a library from its source manifest: SVG files read, sanitised and inlined, fragment
/// files read, and the whole checked (structure, fragments, licence).
BuildResult build_bundle(const Json& manifest, const FileReader& read);
/// Parses a built library (the JSON text of a bundle); nullopt when it is not one.
std::optional<Json> parse_bundle(std::string_view text);
/// Canonical JSON for a bundle (`JSON.stringify(bundle, null, 1)` and a newline): what integrity
/// hashes cover.
std::string serialize_bundle(const Json& bundle);

// ── the libraries a document uses ──────────────────────────────────────────────────────────────

/// What the renderer needs to draw a library shape, and what the palette shows.
struct Art {
  ShapeArt shape;
  std::optional<std::string> kind;
  std::string label;
};

struct PaletteItem {
  std::string type;  ///< `shape`, `composite`, `kind` or `fragment`
  std::string alias;
  std::string library;
  std::string id;
  /// `alias.id`, what `shape` refers to (or, for a kind, what an element is declared as).
  std::string ref;
  std::string label;
  std::string category;
  /// The kind of the element the item adds: built-in (`component`) or a library's (`alias.id`).
  std::optional<std::string> kind;
  std::vector<std::string> keywords;
  std::optional<Art> art;
  std::optional<std::string> description;
};

struct ResolvedLibrary {
  LibraryUse use;
  std::shared_ptr<const Json> bundle;
};

class LibrarySet {
 public:
  LibrarySet() = default;
  explicit LibrarySet(std::vector<ResolvedLibrary> libraries);

  [[nodiscard]] const std::vector<ResolvedLibrary>& libraries() const { return libraries_; }
  [[nodiscard]] std::vector<std::string> aliases() const;
  [[nodiscard]] const Json* library(std::string_view alias) const;
  /// Art for `alias.shape`; nullopt for base shapes and unknown references.
  [[nodiscard]] std::optional<Art> art(std::string_view ref, int depth = 0) const;
  [[nodiscard]] bool has(std::string_view ref) const { return art(ref).has_value(); }
  /// The element kind `alias.kind` a library adds; nullopt for built-in kinds and unknown ones.
  [[nodiscard]] std::optional<KindArt> kind(std::string_view ref) const;
  [[nodiscard]] bool has_kind(std::string_view ref) const { return kind(ref).has_value(); }
  /// A resolver for the renderer.
  [[nodiscard]] ShapeResolver resolver() const;
  [[nodiscard]] KindResolver kind_resolver() const;
  /// The theme `alias.name` declares, its `extends` written as the renderer resolves it.
  [[nodiscard]] std::optional<ThemeDef> theme(std::string_view ref) const;
  /// Every library theme, as `alias.name`.
  [[nodiscard]] std::vector<std::string> theme_refs() const;
  [[nodiscard]] ThemeResolver theme_resolver() const;
  /// Shapes, kinds and themes together, what sessions and the renderer take.
  [[nodiscard]] LibraryArt renderer() const { return {resolver(), kind_resolver(), theme_resolver()}; }
  [[nodiscard]] std::vector<PaletteItem> palette() const;
  [[nodiscard]] const Json* fragment(std::string_view alias, std::string_view id) const;
  /// A fragment's text with its placeholders filled.
  [[nodiscard]] std::optional<std::string> expand(std::string_view alias, std::string_view id,
                                                  const std::map<std::string, std::string, std::less<>>& values = {}) const;
  /// Shape names in another format (`plantuml`, `drawio`, `bpmn`…) → `alias.shape`.
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> mapping(std::string_view format) const;

 private:
  std::string dependency_ref(const Json& lib, std::string_view ref) const;
  /// What a library's own `kind` field (on a shape or composite) means as an element kind.
  std::optional<std::string> kind_ref(const Json& lib, const std::string& alias, const Json* kind) const;

  std::vector<ResolvedLibrary> libraries_;
  std::vector<std::pair<std::string, std::shared_ptr<const Json>>> by_alias_;
};

Json to_json(const PaletteItem& item);

/// A `shape alias.x` or `= alias.x` whose library is used but has no such shape or kind.
struct UnknownReference {
  ElementId element;
  bool kind = false;  ///< the element's kind, else its shape
  std::string ref;
  [[nodiscard]] std::string message() const {
    return std::string("No ") + (kind ? "kind" : "shape") + " \"" + ref + "\" in its library";
  }
};
/// Library references in a model that the set cannot resolve. References to libraries the set
/// does not have are left out: the `use` is what is reported for those.
std::vector<UnknownReference> unknown_references(const Model& model, const LibrarySet& set);

/// Where installed libraries come from; sources read local data and never fetch.
struct LibrarySource {
  std::function<std::vector<std::string>(const std::string& name)> versions;
  std::function<std::shared_ptr<const Json>(const std::string& name, const std::string& version)> load;
};

/// A source over bundles held in memory.
LibrarySource memory_source(std::vector<std::shared_ptr<const Json>> bundles);
/// The libraries that ship with DiaDraw.
LibrarySource builtin_source();

struct LockedLibrary {
  std::string version;
  /// Where `lib install` gets it from: a path, a URL or `builtin`.
  std::string resolved;
  std::optional<std::string> integrity;
};
/// `diadraw-lock.json`: the version each `name@range` resolved to.
struct Lockfile {
  std::map<std::string, LockedLibrary, std::less<>> libraries;
};
std::optional<Lockfile> parse_lockfile(std::string_view text);

struct UnresolvedLibrary {
  LibraryUse use;
  std::string message;
};
struct Resolution {
  LibrarySet set;
  std::vector<UnresolvedLibrary> unresolved;
};
/// Sources in order: the first with a satisfying version wins (lockfile pins win first).
Resolution resolve_uses(const std::vector<LibraryUse>& uses, const std::vector<LibrarySource>& sources,
                        const Lockfile* lock = nullptr);

}  // namespace dd::library
