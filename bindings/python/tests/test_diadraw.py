"""Tests of the `diadraw` Python package against the C++ core."""

import inspect
import warnings
from pathlib import Path

import pytest

import diadraw

CORPUS = Path(__file__).resolve().parents[3] / "corpus"

SHOP = """\
// C4 container view
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
}

view context {
  include *
}
"""


def test_version_matches_the_version_file():
    version = (CORPUS.parent / "VERSION").read_text().strip()
    assert diadraw.__version__ == version
    assert diadraw.version() == version


def test_parse_returns_the_model_and_no_diagnostics():
    model, diagnostics = diadraw.parse(SHOP)
    assert diagnostics == []
    assert list(model["elements"]) == ["customer", "shop", "shop.web", "shop.db"]
    assert model["elements"]["shop.db"]["technology"] == "Postgres"
    assert list(model["views"]) == ["checkout", "context"]


def test_parse_reports_errors_with_code_point_positions():
    source = 'model {\n  a = system "Ünïcode"\n  b = system "é" 42\n}\n'
    _, diagnostics = diadraw.parse(source)
    errors = [d for d in diagnostics if d.severity == "error"]
    assert errors, diagnostics
    d = errors[0]
    assert d.line == 3
    assert source[d.start : d.end] == "42"
    assert d.column == 18
    # The column counts code points from the start of the line.
    line_start = source.rindex("\n", 0, d.start) + 1
    assert d.column == d.start - line_start + 1
    assert str(d).startswith(f"{d.line}:{d.column}: error: ")
    assert "Diagnostic" in repr(d)


def test_check_lists_problems_and_accepts_clean_source():
    assert diadraw.check(SHOP) == []
    problems = diadraw.check("model {\n  a = system\n  a -> b\n}\n")
    assert any(p.severity == "error" for p in problems)


def test_format_prints_canonical_source_and_raises_on_errors():
    formatted = diadraw.format(SHOP)
    assert "customer = person" in formatted
    assert diadraw.format(formatted) == formatted
    with pytest.raises(diadraw.DiaDrawError) as caught:
        diadraw.format("model {\n  a -> b\n")
    assert isinstance(caught.value, ValueError)
    assert caught.value.diagnostics
    assert all(isinstance(d, diadraw.Diagnostic) for d in caught.value.diagnostics)


def test_print_model_round_trips_a_parsed_model():
    model, _ = diadraw.parse(SHOP)
    printed = diadraw.print_model(model)
    assert diadraw.parse(printed)[0] == model


def test_render_svg_of_the_first_and_a_named_view():
    first = diadraw.render(SHOP)
    assert first.startswith("<svg")
    assert "Web app" in first
    context = diadraw.render(SHOP, "context")
    assert context.startswith("<svg") and context != first


def test_render_accepts_a_model_and_a_document():
    model, _ = diadraw.parse(SHOP)
    expected = diadraw.render(SHOP)
    assert diadraw.render(model) == expected
    assert diadraw.render(diadraw.Document.from_source(SHOP)) == expected


def test_render_drawio_and_bpmn():
    xml = diadraw.render(SHOP, format="drawio")
    assert xml.count("<diagram") == 2
    assert diadraw.render(SHOP, "context", format="drawio").count("<diagram") == 1
    assert "definitions" in diadraw.render(SHOP, format="bpmn")


def test_render_pdf():
    pdf = diadraw.render(SHOP, format="pdf")
    assert isinstance(pdf, bytes)
    assert pdf.startswith(b"%PDF-")


def test_render_png_or_reports_a_missing_backend():
    try:
        png = diadraw.render(SHOP, format="png", scale=1, background="#ffffff")
    except diadraw.DiaDrawError as e:
        assert "no PNG backend" in str(e)
        return
    assert png.startswith(b"\x89PNG\r\n\x1a\n")


def test_render_rejects_bad_arguments():
    with pytest.raises(ValueError, match="unknown format"):
        diadraw.render(SHOP, format="gif")
    with pytest.raises(ValueError, match="scale"):
        diadraw.render(SHOP, format="png", scale=0)
    with pytest.raises(ValueError, match="scale"):
        diadraw.render(SHOP, format="png", scale=float("nan"))
    # Options are checked before the document is read: the bad background is reported, not the bad source.
    with pytest.raises(ValueError, match="#rgb"):
        diadraw.render("model { a -> }", format="png", background="#zz")
    with pytest.raises(diadraw.DiaDrawError, match="nope"):
        diadraw.render(SHOP, "nope")
    with pytest.raises(diadraw.DiaDrawError) as caught:
        diadraw.render("model { a -> }")
    assert caught.value.diagnostics


def test_render_warns_about_libraries_it_cannot_resolve():
    source = 'use "acme-shapes@^1" as acme\nmodel {\n  a = system "A"\n}\n'
    with pytest.warns(diadraw.DiaDrawWarning):
        svg = diadraw.render(source)
    assert svg.startswith("<svg")
    with warnings.catch_warnings():
        warnings.simplefilter("error", diadraw.DiaDrawWarning)
        with pytest.raises(diadraw.DiaDrawWarning):
            diadraw.render(source)


def test_render_warns_about_what_a_models_libraries_lack():
    # c4 is built in, so it resolves offline; the shape it does not have is warned about.
    source = 'use "c4@^1" as c4\nmodel {\n  a = system shape c4.nosuch\n}\n'
    model, _ = diadraw.parse(source)
    for document in (source, model):
        with pytest.warns(diadraw.DiaDrawWarning, match='no shape "c4.nosuch"'):
            diadraw.render(document)


def test_render_every_corpus_file():
    files = sorted(CORPUS.glob("*.draw"))
    assert files
    for path in files:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", diadraw.DiaDrawWarning)
            assert diadraw.render(path.read_text(encoding="utf-8"), file=str(path)).startswith("<svg"), path


def test_import_mermaid():
    imported = diadraw.import_diagram("flowchart LR\n  a[Start] --> b[End]\n", "mermaid")
    assert set(imported) == {"source", "model", "report"}
    assert "Start" in imported["source"]
    assert diadraw.parse(imported["source"])[1] == []
    assert "dropped" in imported["report"]


def test_import_guesses_the_format_and_reads_bytes():
    imported = diadraw.import_diagram(b"flowchart TD\n  a --> b\n", file_name="flow.mmd")
    assert "a" in imported["model"]["elements"]
    with pytest.raises(diadraw.DiaDrawError, match="cannot tell the format"):
        diadraw.import_diagram("hello")
    with pytest.raises(ValueError, match="unknown format"):
        diadraw.import_diagram("graph TD", "visio")
    with pytest.raises(TypeError):
        diadraw.import_diagram(42)


def test_import_drawio_from_the_corpus():
    files = sorted((CORPUS / "drawio").rglob("*.drawio"))
    if not files:
        pytest.skip("no draw.io files in the corpus")
    imported = diadraw.import_diagram(files[0].read_bytes(), file_name=str(files[0]))
    assert imported["model"]["elements"]


def test_highlight_offsets_are_code_points():
    text = 'model { ü = system "Ünï" }'
    tokens = diadraw.highlight(text)
    assert "keyword" in {kind for _, _, kind in tokens}
    strings = [text[start:end] for start, end, kind in tokens if kind == "string"]
    assert strings == ['"Ünï"']


def test_update_source_keeps_the_text_as_written():
    op = {"type": "addElement", "element": {"id": "cache", "kind": "container", "name": "Cache"}}
    update = diadraw.update_source(SHOP, op)
    assert "// C4 container view" in update["source"]
    assert "cache" in update["model"]["elements"]
    assert update["diagnostics"] == []
    assert isinstance(update["minimal"], bool)


def test_document_apply_undo_redo():
    doc = diadraw.Document()
    assert not doc.can_undo and not doc.can_redo
    assert doc.undo() is False
    doc.apply({"type": "addElement", "element": {"id": "a", "kind": "process"}})
    assert doc.model["elements"]["a"]["name"] == "a"
    assert doc.can_undo
    with pytest.raises(diadraw.DiaDrawError):
        doc.apply({"type": "addElement", "element": {"id": "a", "kind": "process"}})
    with pytest.raises(diadraw.DiaDrawError, match="invalid operation"):
        doc.apply({"type": "noSuchOperation"})
    assert doc.undo() is True
    assert doc.model["elements"] == {}
    assert doc.redo() is True
    assert "a" in doc.model["elements"]
    assert "a = process" in doc.to_source()


def test_document_from_model_and_resolve_view():
    model = {
        "elements": {"a": {"id": "a", "kind": "process"}},
        "relationships": {},
        "groups": {},
        "views": {"v": {"id": "v"}},
    }
    doc = diadraw.Document.from_model(model)
    assert doc.views == ["v"]
    resolved = doc.resolve_view("v")
    assert resolved["elements"][0]["id"] == "a"
    with pytest.raises(diadraw.DiaDrawError, match="not found"):
        doc.resolve_view("nope")
    with pytest.raises(diadraw.DiaDrawError, match="invalid model"):
        diadraw.Document.from_model({"elements": []})


def test_document_from_source_raises_with_diagnostics():
    doc = diadraw.Document.from_source(SHOP)
    assert doc.views == ["checkout", "context"]
    with pytest.raises(diadraw.DiaDrawError) as caught:
        diadraw.Document.from_source("model { a -> }")
    assert caught.value.diagnostics[0].severity == "error"


def test_python_values_that_are_not_json_are_refused():
    with pytest.raises(TypeError, match="dict keys must be strings"):
        diadraw.print_model({1: "x"})
    with pytest.raises(TypeError, match="cannot convert"):
        diadraw.print_model({"elements": object()})
    looped: dict = {}
    looped["self"] = looped
    with pytest.raises(ValueError, match="nested too deeply"):
        diadraw.print_model(looped)


def test_numbers_cross_as_int_and_float():
    doc = diadraw.Document.from_source(
        'model {\n  a = system "A"\n}\nview v {\n  include *\n  layout fixed\n  pin a at 10.5, 20 size 100, 60\n}\n'
    )
    (hint,) = doc.model["views"]["v"]["hints"]
    assert hint["x"] == 10.5 and isinstance(hint["x"], float)
    assert hint["y"] == 20 and isinstance(hint["y"], int)
    # And back: the model round-trips through the core unchanged.
    assert diadraw.Document.from_model(doc.model).model == doc.model


def test_png_options_are_checked():
    # Checked before drawing, so in every build.
    with pytest.raises(diadraw.DiaDrawError, match="#rgb or #rrggbb"):
        diadraw.render(SHOP, format="png", background="#zzzzzz")
    with pytest.raises(diadraw.DiaDrawError, match="at most 8"):
        diadraw.render(SHOP, format="png", scale=9)
    try:
        diadraw.render(SHOP, format="png", scale=1)
    except diadraw.DiaDrawError as e:
        assert "no PNG backend" in str(e)
        pytest.skip("this build has no PNG backend")
    assert diadraw.render(SHOP, format="png", scale=1, background="#abc").startswith(b"\x89PNG")


def test_warnings_point_at_the_calling_line():
    source = 'use "acme-shapes@^1" as acme\nmodel {\n  a = system "A"\n}\n'
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always", diadraw.DiaDrawWarning)
        line = inspect.currentframe().f_lineno + 1
        diadraw.render(source)
    assert caught and caught[0].filename == __file__
    assert caught[0].lineno == line


def test_text_that_is_not_utf8_is_repaired_not_raised():
    imported = diadraw.import_diagram(b"flowchart LR\n  a[\xff\xfe x] --> b\n", "mermaid")
    assert "�" in imported["source"]
    assert "�" in str(imported["model"])


def test_the_package_ships_drawio_stencils():
    # The package's own copy, not the source tree's (whose path a build from source also knows).
    assert (Path(diadraw.__file__).parent / "drawio" / "index.json.gz").is_file()


def test_drawio_stencils_keep_their_artwork():
    path = CORPUS / "drawio" / "drawio-diagrams" / "examples_citrix-hybrid-infrastructure.drawio"
    if not path.is_file():
        pytest.skip("draw.io example not in the corpus")
    stats = diadraw.import_diagram(path.read_bytes(), file_name=str(path))["report"]["stats"]
    assert stats["artwork"] > 0, stats


def test_export_of_an_unknown_view_lists_the_views():
    with pytest.raises(diadraw.DiaDrawError, match=r'no view "nope" \(views: checkout, context\)'):
        diadraw.render(SHOP, "nope", format="drawio")


def test_files_in_folders_with_non_ascii_names(tmp_path):
    folder = tmp_path / "Діаграми ünï"
    folder.mkdir()
    path = folder / "shop.draw"
    path.write_text(SHOP, encoding="utf-8")
    assert diadraw.render(SHOP, file=str(path)).startswith("<svg")
    assert diadraw.check(SHOP, file=str(path)) == []
