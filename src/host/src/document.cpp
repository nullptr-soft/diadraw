#include "host/document.hpp"

#include <algorithm>
#include <filesystem>

#include "diadraw/base/text.hpp"
#include "diadraw/dsl/format.hpp"
#include "diadraw/dsl/patch.hpp"
#include "diadraw/dsl/print.hpp"
#include "diadraw/export/export.hpp"
#include "diadraw/io/bpmn.hpp"
#include "diadraw/io/drawio.hpp"
#include "diadraw/io/import.hpp"
#include "host/diagram.hpp"
#include "host/libraries.hpp"
#include "host/styles.hpp"

namespace dd::host {

namespace {

std::string trimmed(std::string_view s) {
  auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && space(s.front())) s.remove_prefix(1);
  while (!s.empty() && space(s.back())) s.remove_suffix(1);
  return std::string(s);
}

std::optional<std::string> optional_text(std::string_view s) {
  std::string t = trimmed(s);
  if (t.empty()) return std::nullopt;
  return t;
}

/// Splits on `separator`, trimming each part and dropping empty ones.
std::vector<std::string> split_trimmed(std::string_view s, char separator) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    auto end = s.find(separator, start);
    if (end == std::string_view::npos) end = s.size();
    std::string part = trimmed(s.substr(start, end - start));
    if (!part.empty()) out.push_back(std::move(part));
    start = end + 1;
  }
  return out;
}

std::string tags_text(const std::vector<std::string>& tags) {
  std::string out;
  for (const auto& tag : tags) out += (out.empty() ? "" : ", ") + tag;
  return out;
}

std::string props_text(const Properties& properties) {
  std::string out;
  for (const auto& [k, v] : properties) out += (out.empty() ? "" : "\n") + k + "=" + v;
  return out;
}

/// One `key=value` per line; a line without `=` is a key with an empty value.
Properties props_from_text(std::string_view text) {
  Properties props;
  for (const auto& line : split_trimmed(text, '\n')) {
    auto eq = line.find('=');
    std::string key = trimmed(std::string_view(line).substr(0, eq));
    if (key.empty()) continue;
    props.set(key, eq == std::string::npos ? "" : trimmed(std::string_view(line).substr(eq + 1)));
  }
  return props;
}

void apply_style(std::optional<Style>& style, std::string_view field) {
  std::string text = trimmed(field);
  // `style ""` still marks an item as styled; an empty field keeps that as it was.
  if (text.empty()) {
    if (style && !style->empty()) style.reset();
  } else {
    style = dsl::parse_style_text(text);
  }
}

}  // namespace

std::string arrow_of(const Relationship& relationship) {
  return relationship.bidirectional ? "<->" : relationship.line == LineStyle::Dashed ? "-->" : "->";
}

RelationshipFields fields_of(const Relationship& relationship) {
  RelationshipFields f;
  f.id = relationship.id;
  f.source = dsl::print_endpoint(relationship.source);
  f.target = dsl::print_endpoint(relationship.target);
  if (dsl::is_identifier(relationship.id)) f.name = relationship.id;
  f.label = relationship.label.value_or("");
  f.technology = relationship.technology.value_or("");
  f.arrow = arrow_of(relationship);
  f.tags = tags_text(relationship.tags);
  if (relationship.style) f.style = dsl::print_style_text(*relationship.style);
  f.props = props_text(relationship.properties);
  return f;
}

Relationship apply_fields(Relationship relationship, const RelationshipFields& fields) {
  relationship.label = optional_text(fields.label);
  relationship.technology = optional_text(fields.technology);
  std::string arrow = trimmed(fields.arrow);
  if (arrow == "->" || arrow == "-->" || arrow == "<->") {
    relationship.line = arrow == "-->" ? LineStyle::Dashed : LineStyle::Solid;
    relationship.bidirectional = arrow == "<->";
  }
  relationship.tags = split_trimmed(fields.tags, ',');
  apply_style(relationship.style, fields.style);
  relationship.properties = props_from_text(fields.props);
  return relationship;
}

ElementFields fields_of(const Element& element) {
  ElementFields f;
  f.id = element.id;
  f.local_id = dsl::local_name(element);
  f.kind = element.kind;
  f.name = element.name;
  f.technology = element.technology.value_or("");
  f.shape = element.shape.value_or("");
  f.description = element.description.value_or("");
  f.tags = tags_text(element.tags);
  if (element.style) f.style = dsl::print_style_text(*element.style);
  f.props = props_text(element.properties);
  return f;
}

Element apply_fields(Element element, const ElementFields& fields) {
  element.kind = trimmed(fields.kind);
  // An emptied name falls back to the identifier, as the DSL does without a label.
  std::string name = trimmed(fields.name);
  element.name = name.empty() ? dsl::local_name(element) : name;
  element.technology = optional_text(fields.technology);
  element.shape = optional_text(fields.shape);
  element.description = optional_text(fields.description);
  element.tags = split_trimmed(fields.tags, ',');
  apply_style(element.style, fields.style);
  element.properties = props_from_text(fields.props);
  return element;
}

Result<Imported> import_file(const std::string& path) {
  auto bytes = read_file(path);
  std::string name = path_utf8(utf8_path(path).filename());
  if (!bytes) return Error{"Cannot read " + name};
  auto format = io::detect_format(path, *bytes);
  if (!format) return Error{"Cannot tell the format of " + name};
  auto result = import_text(*bytes, *format, path, true);
  if (!result) return result.error();
  Imported out{dsl::print(result->model), ""};
  std::size_t dropped = result->report.dropped.size();
  if (dropped || !result->report.notes.empty()) {
    out.report = "Imported " + name + ": " + std::to_string(dropped) + " item" + (dropped == 1 ? "" : "s") +
                 " not carried over";
    if (!result->report.notes.empty()) out.report += " · " + result->report.notes[0];
  }
  return out;
}

const std::vector<std::string>& element_kinds() {
  static const std::vector<std::string> kinds = {"process", "decision", "start",     "end",      "person",
                                                  "system",  "container", "component", "database", "note"};
  return kinds;
}

Document::Document(std::string text, std::string file)
    : session_(std::move(text),
               [file](const Model& model) {
                 return resolve_for_model(model, file).set.renderer();
               }),
      file_(std::move(file)) {}

void Document::set_text(const std::string& text) {
  if (text != session_.source()) session_.set_source(text);
}

Document::Refreshed Document::refresh() {
  views_ = session_.model().views.keys();
  if (std::find(views_.begin(), views_.end(), view_) == views_.end()) view_ = views_.empty() ? "" : views_.front();
  Refreshed out;
  std::string first;
  int errors = 0;
  for (const auto& d : session_.parsed().diagnostics)
    if (d.severity == dsl::Severity::Error && errors++ == 0) first = "line " + std::to_string(d.line) + ": " + d.message;
  if (!view_.empty()) {
    if (auto r = session_.render(view_)) {
      if (!r->geometry.ignored_hints.empty() && errors == 0)
        out.status = std::to_string(r->geometry.ignored_hints.size()) + " layout hint(s) could not be honoured";
      out.render = std::move(*r);
    } else {
      out.status = "Layout failed: " + r.error().message;
      out.error = true;
    }
  }
  if (errors) {
    out.status = std::to_string(errors) + " error(s), first on " + first + ". The diagram shows what could be read.";
    out.error = true;
  }
  return out;
}

Result<std::optional<Change>> Document::changed(Result<dsl::SourceUpdate> update, const std::string& before) {
  if (!update) return update.error();
  if (update->source == before) return std::optional<Change>{};
  Change change;
  change.edits = update->edits;
  if (change.edits.empty()) change.edits.push_back({0, static_cast<std::uint32_t>(before.size()), update->source});
  std::sort(change.edits.begin(), change.edits.end(), [](const auto& a, const auto& b) { return a.from > b.from; });
  if (!update->minimal) change.note = "Reformatted the file to apply that change";
  return std::optional(std::move(change));
}

Result<std::optional<Change>> Document::drop(const std::string& element, const Box& box, bool freeform) {
  std::string before = session_.source();
  auto r = session_.drop(view_, element, box, freeform);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::move(const std::vector<std::string>& elements, const Point& offset) {
  std::string before = session_.source();
  auto r = session_.move(view_, elements, offset);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::resize(const std::string& element, const Box& box) {
  std::string before = session_.source();
  auto r = session_.resize(view_, element, box);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::fit(const std::vector<std::string>& elements) {
  if (view_.empty() || elements.empty()) return std::optional<Change>{};
  std::string before = session_.source();
  auto r = session_.fit(view_, elements);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::rotate(const std::string& element, double degrees) {
  std::string before = session_.source();
  auto r = session_.rotate(view_, element, degrees);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::reroute(const std::string& relationship, const std::vector<Point>& points) {
  std::string before = session_.source();
  auto r = session_.reroute(view_, relationship, points);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::connect(const std::string& source, const std::string& target) {
  std::string before = session_.source();
  return changed(session_.connect(source, target), before);
}

Result<std::optional<Change>> Document::reconnect(const std::string& relationship, editor::End end,
                                                  const std::string& element, std::string* reconnected) {
  // Dropping an end back on its own element keeps it as it was, port and all.
  if (const Relationship* r = session_.model().relationships.find(relationship))
    if ((end == editor::End::Source ? r->source : r->target).element == element) return std::optional<Change>{};
  std::string before = session_.source();
  auto r = session_.reconnect(relationship, end, Endpoint{element, std::nullopt});
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  if (reconnected) *reconnected = (*r)->id;
  return changed(std::move((*r)->update), before);
}

Result<std::optional<Change>> Document::rename(const std::string& element, const std::string& name) {
  std::string before = session_.source();
  return changed(session_.apply(op::RenameElement{element, name}), before);
}

Result<std::optional<Change>> Document::relabel(const std::string& relationship, const std::optional<std::string>& label) {
  std::string before = session_.source();
  return changed(session_.apply(op::SetRelationshipLabel{relationship, label}), before);
}

Result<std::optional<Change>> Document::remove(const editor::Selection& selection) {
  std::string before = session_.source();
  auto r = session_.remove(selection);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

Result<std::optional<Change>> Document::add(const std::string& kind, std::string* created) {
  if (view_.empty()) return std::optional<Change>{};
  const View* view = session_.model().views.find(view_);
  std::string before = session_.source();
  auto r = session_.create(kind, view ? view->scope : std::nullopt);
  if (!r) return r.error();
  if (created) *created = r->first;
  return changed(std::move(r->second), before);
}

std::string Document::fresh_view() const { return editor::fresh_view_id(session_.model()); }

Result<std::optional<Change>> Document::add_view(const std::string& id_typed) {
  std::string id(dd::trim(id_typed));
  const View* current = session_.model().views.find(view_);
  auto op = editor::add_view_operation(session_.parsed().model, id, current ? current->scope : std::nullopt);
  if (!op) return op.error();
  std::string before = session_.source();
  auto change = changed(session_.apply(*op), before);
  if (change && *change) view_ = id;
  return change;
}

Result<std::optional<Change>> Document::remove_view() {
  auto op = editor::remove_view_operation(session_.parsed().model, view_);
  if (!op) return op.error();
  std::string before = session_.source();
  return changed(session_.apply(*op), before);  // refresh() moves to the first view left
}

Result<std::optional<Change>> Document::rename_view(const std::string& to_typed) {
  std::string to(dd::trim(to_typed));
  auto op = editor::rename_view_operation(session_.parsed().model, view_, to);
  if (!op) return op.error();
  if (!*op) return std::optional<Change>{};
  std::string before = session_.source();
  auto change = changed(session_.apply(**op), before);
  if (change && *change) view_ = to;
  return change;
}

Result<std::optional<Change>> Document::clear() {
  if (session_.source() == kEmptyText) return std::optional<Change>{};
  auto size = static_cast<std::uint32_t>(session_.source().size());
  session_.set_source(std::string(kEmptyText));
  Change change;
  change.edits.push_back({0, size, std::string(kEmptyText)});
  return std::optional(std::move(change));
}

Result<std::optional<Change>> Document::format(int indent_size) {
  const std::string& before = session_.source();  // read only before set_source below
  std::string after = dsl::format_source(before, dsl::FormatOptions{indent_size});
  if (after == before) return std::optional<Change>{};
  // One edit over what differs, so the caret and the scroll position stay put elsewhere.
  Change change;
  change.edits = dsl::diff_edit(before, after);
  session_.set_source(std::move(after));
  return std::optional(std::move(change));
}

Result<std::optional<Change>> Document::rename_element(const std::string& element, const std::string& to_typed,
                                                       std::string* renamed) {
  if (renamed) *renamed = element;
  std::string before = session_.source();
  auto r = session_.rename_element(element, dd::trim(to_typed));
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  if (renamed) *renamed = (*r)->id;
  return changed(std::move((*r)->update), before);
}

std::optional<std::string> Document::element_at(std::size_t offset) const {
  auto target = editor::rename_target(session_.parsed(), offset);
  if (!target) return std::nullopt;
  return target->element;
}

std::optional<std::string> Document::element_to_rename(const editor::Selection& selection, bool text_focused,
                                                       std::size_t caret) const {
  if (!text_focused && selection.elements.size() == 1 && selection.relationships.empty())
    return selection.elements.front();
  return element_at(caret);
}

editor::Selection Document::follow_rename(editor::Selection selection, const std::string& from,
                                          const std::string& to) const {
  for (auto& id : selection.elements)
    if (is_within(id, from)) id = to + id.substr(from.size());
  const auto& model = session_.model();
  std::erase_if(selection.relationships, [&](const std::string& id) { return !model.relationships.contains(id); });
  return selection;
}

Result<std::optional<Change>> Document::update(const ElementFields& typed, std::string* renamed) {
  if (renamed) *renamed = typed.id;
  if (!session_.model().elements.contains(typed.id)) return Error{"Element \"" + typed.id + "\" not found"};
  // A new id first, so the other fields apply to the renamed element; as one change.
  std::string before = session_.source();
  ElementFields fields = typed;
  std::optional<Change> rename;
  if (const Element* e = session_.model().elements.find(typed.id); trimmed(typed.local_id) != dsl::local_name(*e)) {
    std::string was_name = e->name;
    std::string id;
    auto r = rename_element(typed.id, typed.local_id, &id);
    if (!r) return r.error();
    rename = std::move(*r);
    fields.id = id;
    if (renamed) *renamed = id;
    // A name left as it was shown follows the id when the text writes none.
    if (trimmed(fields.name) == was_name) fields.name = session_.model().elements.find(id)->name;
  }
  auto rest = update_fields(fields);
  if (!rename) return rest;
  if (!rest) {
    // The rename stands (the session holds it), so the shell must still apply it.
    rename->note = rest.error().message;
    return rename;
  }
  if (!*rest) return rename;
  // Both: one change from the text before the rename to the text now.
  Change both;
  both.edits = dsl::diff_edit(before, session_.source());
  both.note = rename->note ? rename->note : (*rest)->note;
  return std::optional(std::move(both));
}

Result<std::optional<Change>> Document::update_fields(const ElementFields& fields) {
  const Element* existing = session_.model().elements.find(fields.id);
  if (!existing) return Error{"Element \"" + fields.id + "\" not found"};
  Element updated = apply_fields(*existing, fields);
  std::vector<Operation> ops;
  if (!(updated == *existing)) ops.push_back(op::UpdateElement{updated});
  // Shared styles: only when the field was edited, so a kind or tag change alone never touches them.
  if (fields.shared_styles != shared_styles(session_.model(), *existing)) {
    auto styles = shared_styles_operation(session_.model(), updated, fields.shared_styles);
    if (!styles) return styles.error();
    if (*styles) ops.push_back(std::move(**styles));
  }
  if (ops.empty()) return std::optional<Change>{};
  std::string before = session_.source();
  return changed(session_.apply(ops.size() == 1 ? ops[0] : Operation{op::Batch{std::move(ops)}}), before);
}

std::vector<editor::FieldProblem> Document::problems(const ElementFields& typed) const {
  const Element* existing = session_.model().elements.find(typed.id);
  if (!existing) return {};
  Element edited = apply_fields(*existing, typed);
  // The Id apart, so its dry run is kept while only other fields change.
  std::string own = dsl::local_name(*existing);
  std::string local = trimmed(typed.local_id);
  auto out = editor::element_problems(session_.source(), session_.parsed(), edited, own);
  if (local != own) {
    const std::string& source = session_.source();
    if (!id_check_ || id_check_->element != typed.id || id_check_->local_id != local || id_check_->source != source)
      id_check_ = IdCheck{source, typed.id, local, editor::element_id_problem(source, session_.parsed(), typed.id, local)};
    if (id_check_->problem) out.insert(out.begin(), {"id", *id_check_->problem});
  }
  // As update_fields: shared styles count only when the field was edited.
  if (typed.shared_styles != shared_styles(session_.model(), *existing))
    if (auto styles = shared_styles_operation(session_.model(), edited, typed.shared_styles); !styles)
      out.push_back({"shared-styles", std::move(styles.error().message)});
  return out;
}

std::vector<editor::FieldProblem> Document::problems(const RelationshipFields& typed) const {
  const Relationship* existing = session_.model().relationships.find(typed.id);
  if (!existing) return {};
  return editor::relationship_problems(session_.model(), apply_fields(*existing, typed), optional_text(typed.name));
}

Result<std::optional<Change>> Document::apply(const Operation& operation) {
  std::string before = session_.source();
  return changed(session_.apply(operation), before);
}

Result<std::optional<Change>> Document::update(const RelationshipFields& fields, std::string* renamed) {
  if (renamed) *renamed = fields.id;
  const Relationship* existing = session_.model().relationships.find(fields.id);
  if (!existing) return Error{"Relationship \"" + fields.id + "\" not found"};
  std::string arrow = trimmed(fields.arrow);
  if (arrow != "->" && arrow != "-->" && arrow != "<->")
    return Error{"The arrow is ->, --> or <->, not \"" + arrow + "\""};
  std::string before = session_.source();
  auto r = session_.update_relationship(apply_fields(*existing, fields), optional_text(fields.name), renamed);
  if (!r) return r.error();
  if (!*r) return std::optional<Change>{};
  return changed(std::move(**r), before);
}

std::optional<RelationshipFields> Document::relationship_fields(const std::string& relationship) const {
  const Relationship* r = session_.model().relationships.find(relationship);
  if (!r) return std::nullopt;
  return fields_of(*r);
}

std::optional<ElementFields> Document::fields(const std::string& element) const {
  const Element* e = session_.model().elements.find(element);
  if (!e) return std::nullopt;
  ElementFields f = fields_of(*e);
  f.shared_styles = shared_styles(session_.model(), *e);
  return f;
}

FieldChoices Document::choices() const {
  const Model& model = session_.model();
  auto uses = model.libraries.value_or(std::vector<LibraryUse>{});
  std::string key;
  for (const auto& use : uses) key += library_spec(use) + " as " + use.alias + "\n";
  if (!library_choices_ || library_choices_->first != key)
    library_choices_.emplace(key, library_choices(resolve_for_file(uses, file_).set));
  FieldChoices out = library_choices_->second;
  add_model_choices(out, model);
  return out;
}

std::optional<LayoutHint> Document::preview_drop(const std::string& element, const Box& box, bool freeform) {
  auto r = session_.drop_hint(view_, element, box, freeform);
  return r ? *r : std::nullopt;
}

std::string Document::name_of(const std::string& element) const {
  const Element* e = session_.model().elements.find(element);
  return e ? e->name : element;
}

std::string Document::label_of(const std::string& relationship) const {
  const Relationship* r = session_.model().relationships.find(relationship);
  return r && r->label ? *r->label : "";
}

std::optional<dsl::Span> Document::declaration_of(const std::string& element) const {
  const auto& elements = session_.parsed().source_map.elements;
  auto it = elements.find(element);
  if (it == elements.end()) return std::nullopt;
  return it->second.name;
}

void Document::attach(canvas::Canvas& canvas) {
  canvas.callbacks.preview_drop = [this](const std::string& id, const Box& box, bool freeform) {
    return preview_drop(id, box, freeform);
  };
  canvas.callbacks.name_of = [this](const std::string& id) { return name_of(id); };
  canvas.callbacks.label_of = [this](const std::string& id) { return label_of(id); };
}

Result<std::string> Document::svg() {
  if (view_.empty()) return Error{"There is no view to export"};
  return session_.export_svg(view_);
}

Result<std::string> Document::png() {
  auto s = svg();
  if (!s) return s.error();
  auto rasterizer = exporting::make_rasterizer();
  if (!rasterizer) return Error{"PNG export is not available in this build"};
  return rasterizer->png(*s, {2, std::nullopt});
}

Result<std::string> Document::pdf() {
  auto s = svg();
  if (!s) return s.error();
  auto writer = exporting::make_pdf_writer();
  if (!writer) return Error{"PDF export is not available in this build"};
  return writer->pdf(*s);
}

namespace {

std::vector<io::DrawioPage> pages(editor::Session& session, const std::vector<std::string>& views,
                                  const std::string& current) {
  std::vector<io::DrawioPage> out;
  for (const auto& id : views)
    if (auto r = session.render(id)) out.push_back({id, r->geometry.nodes, r->geometry.edges});
  if (!current.empty()) (void)session.render(current);  // the canvas's view stays the last laid out
  return out;
}

}  // namespace

std::string Document::drawio() { return io::export_drawio(session_.model(), pages(session_, views_, view_)); }
std::string Document::bpmn() { return io::export_bpmn(session_.model(), pages(session_, views_, view_)); }

}  // namespace dd::host
