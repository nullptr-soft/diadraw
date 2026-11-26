// Offline library resolution over a host file system (a port of packages/library/src/node.ts).

#include "diadraw/library/disk.hpp"

#include <algorithm>
#include <map>
#include <mutex>

#include "diadraw/io/codec.hpp"

namespace dd::library {

namespace {

std::string folder(const std::string& name) {
  std::string f = name;
  if (auto slash = f.find('/'); slash != std::string::npos) f.replace(slash, 1, "__");
  return f;
}

std::string str(const Json& j, const char* key) {
  const Json* v = j.get(key);
  return v && v->is_string() ? v->as_string() : "";
}

std::optional<Json> gzip_json(const std::optional<std::string>& bytes) {
  if (!bytes) return std::nullopt;
  auto text = io::gunzip(*bytes);
  if (!text) return std::nullopt;
  auto json = Json::parse(*text);
  return json ? std::optional<Json>(std::move(*json)) : std::nullopt;
}

}  // namespace

struct DiskLibraries::State {
  std::once_flag index_once;
  std::optional<Json> index;
  std::mutex sets_mutex;
  std::map<std::string, std::shared_ptr<const Json>, std::less<>> sets;
};

DiskLibraries::DiskLibraries(std::shared_ptr<const FileSystem> fs, std::string drawio_dir)
    : fs_(std::move(fs)), drawio_dir_(std::move(drawio_dir)), state_(std::make_shared<State>()) {}

std::string DiskLibraries::cache_dir() const {
  if (std::string d = fs_->env("DIADRAW_CACHE"); !d.empty()) return d;
  std::string xdg = fs_->env("XDG_CACHE_HOME");
  std::string base = !xdg.empty() ? xdg : fs_->join(fs_->env("HOME"), ".cache");
  return fs_->join(fs_->join(base, "diadraw"), "libraries");
}

LibrarySource DiskLibraries::cache_source(const std::string& dir) const {
  LibrarySource s;
  auto fs = fs_;
  s.versions = [fs, dir](const std::string& name) {
    std::vector<std::string> out;
    for (const auto& f : fs->list(fs->join(dir, folder(name)))) {
      if (f.size() > 5 && f.compare(f.size() - 5, 5, ".json") == 0) {
        std::string v = f.substr(0, f.size() - 5);
        if (parse_version(v)) out.push_back(v);
      }
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  s.load = [fs, dir](const std::string& name, const std::string& version) -> std::shared_ptr<const Json> {
    auto text = fs->read(fs->join(fs->join(dir, folder(name)), version + ".json"));
    if (!text) return nullptr;
    auto bundle = parse_bundle(*text);
    return bundle ? std::make_shared<const Json>(std::move(*bundle)) : nullptr;
  };
  return s;
}

const Json* DiskLibraries::drawio_index() const {
  std::call_once(state_->index_once, [&] {
    if (!drawio_dir_.empty()) state_->index = gzip_json(fs_->read(fs_->join(drawio_dir_, "index.json.gz")));
  });
  return state_->index ? &*state_->index : nullptr;
}

LibrarySource DiskLibraries::builtin_source() const {
  LibrarySource own = library::builtin_source();
  // The source outlives neither `this` nor its state: hold both.
  auto self = std::make_shared<DiskLibraries>(*this);
  LibrarySource s;
  s.versions = [own, self](const std::string& name) {
    auto out = own.versions(name);
    if (const Json* index = self->drawio_index())
      if (const Json* sets = index->get("sets"); sets && sets->is_array())
        for (const auto& set : sets->as_array())
          if (str(set, "name") == name) out.push_back(str(set, "version"));
    return out;
  };
  s.load = [own, self](const std::string& name, const std::string& version) -> std::shared_ptr<const Json> {
    if (auto b = own.load(name, version)) return b;
    if (name.rfind("drawio-", 0) != 0 || !self->drawio_index()) return nullptr;
    State& state = *self->state_;
    std::shared_ptr<const Json> bundle;
    {
      std::lock_guard lock(state.sets_mutex);
      if (auto it = state.sets.find(name); it != state.sets.end()) bundle = it->second;
    }
    if (!bundle) {
      const FileSystem& fs = *self->fs_;
      auto json = gzip_json(fs.read(fs.join(fs.join(self->drawio_dir_, "sets"), name + ".json.gz")));
      if (!json) return nullptr;
      bundle = std::make_shared<const Json>(std::move(*json));
      std::lock_guard lock(state.sets_mutex);
      state.sets[name] = bundle;
    }
    return str(*bundle, "version") == version ? bundle : nullptr;
  };
  return s;
}

std::optional<std::string> DiskLibraries::find_lockfile(const std::string& dir) const {
  std::string d = dir;
  while (!d.empty()) {
    std::string file = fs_->join(d, kLockfile);
    if (fs_->read(file)) return file;
    std::string up = fs_->parent(d);
    if (up == d) break;
    d = std::move(up);
  }
  return std::nullopt;
}

std::optional<Lockfile> DiskLibraries::read_lockfile(const std::optional<std::string>& file) const {
  if (!file) return std::nullopt;
  auto text = fs_->read(*file);
  return text ? parse_lockfile(*text) : std::nullopt;
}

Resolution DiskLibraries::resolve_for_file(const std::vector<LibraryUse>& uses, const std::string& file,
                                           const std::vector<LibrarySource>& extra) const {
  auto lock = read_lockfile(find_lockfile(fs_->parent(file)));
  std::vector<LibrarySource> sources = extra;
  sources.push_back(cache_source(cache_dir()));
  sources.push_back(builtin_source());
  return resolve_uses(uses, sources, lock ? &*lock : nullptr);
}

std::vector<std::pair<std::string, std::vector<std::string>>> DiskLibraries::list_cache(const std::string& dir) const {
  std::vector<std::pair<std::string, std::vector<std::string>>> out;
  std::vector<std::string> entries = fs_->list(dir);
  std::sort(entries.begin(), entries.end());
  auto source = cache_source(dir);
  for (auto name : entries) {
    if (auto u = name.find("__"); u != std::string::npos) name.replace(u, 2, "/");
    auto versions = source.versions(name);
    std::sort(versions.begin(), versions.end(), [](const std::string& a, const std::string& b) {
      return compare_versions(*parse_version(b), *parse_version(a)) < 0;
    });
    if (!versions.empty()) out.emplace_back(name, versions);
  }
  return out;
}

}  // namespace dd::library
