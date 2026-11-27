# DiaDraw for Python

The `diadraw` package runs DiaDraw's C++ core (`libdiadraw`) from Python: parse, check, format,
render (SVG, PNG, PDF, draw.io, BPMN) and import (Mermaid, PlantUML, Structurizr, draw.io, BPMN)
diagrams, and edit models with undo. It is a [nanobind](https://github.com/wjakob/nanobind)
extension module built from this repository's CMake tree, on macOS, Linux and Windows, for
Python 3.10 and later.

## Install

From a checkout (needs CMake 3.25+, a C++20 compiler and access to github.com for the pinned
third-party sources, as for every native build):

```bash
pip install .
```

Wheels for every platform come from the `Python` workflow (`.github/workflows/python.yml`,
cibuildwheel); download them from the run's artifacts and `pip install` the one for your platform.
The package is not on PyPI.

## Use

```python
import diadraw

source = """
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
"""

svg = diadraw.render(source)                              # the first view, as SVG text
png = diadraw.render(source, "checkout", format="png", scale=2)   # bytes
pdf = diadraw.render(source, format="pdf")                # bytes
xml = diadraw.render(source, format="drawio")             # every view as a draw.io page

for d in diadraw.check(source):                           # errors and warnings
    print(f"shop.draw:{d}")                               # line:column: severity: message

print(diadraw.format(source))                             # canonical text

imported = diadraw.import_diagram("flowchart LR\n  a --> b", "mermaid")
print(imported["source"])                                 # DiaDraw source
print(imported["report"])                                 # what could not be carried over
```

| Function | Does |
| --- | --- |
| `parse(source)` | `(model, diagnostics)`; never raises for problems in the source |
| `check(source, *, file="")` | every diagnostic, plus unresolved libraries as warnings |
| `format(source)` | the source in canonical form |
| `print_model(model)` | a model dict as DiaDraw source |
| `update_source(source, operation)` | applies an operation to the text, keeping the rest as written: `{source, model, minimal, diagnostics}` |
| `render(document, view=None, *, format="svg", scale=2.0, background=None, file="")` | `str` for `svg`, `drawio` and `bpmn`; `bytes` for `png` and `pdf`. `scale` (PNG only) is above 0 and at most 8; `background` is `#rgb` or `#rrggbb` |
| `import_diagram(data, format=None, *, file_name="", keep_geometry=True)` | `{source, model, report}`; the format is guessed from `file_name` and the data when not given |
| `highlight(text)` | syntax tokens `(start, end, kind)` |
| `Document` | a model with undo: `from_source`, `from_model`, `apply`, `undo`, `redo`, `model`, `views`, `resolve_view`, `to_source` |

A `document` is DiaDraw source (`str`), a model `dict` or a `Document`. Models, operations and
resolved views are dicts in the shape of DiaDraw's model JSON (see `docs/architecture.md`), for
example `{"type": "addElement", "element": {"id": "a", "kind": "system", "name": "A"}}`.

Libraries resolve offline, as in the CLI: the lockfile next to `file`, the library cache and the
built-in libraries. Offsets and columns count code points, as Python strings do.

Errors in a document raise `diadraw.DiaDrawError`, a `ValueError` whose `diagnostics` list says
where. What the CLI prints as warnings (a library that did not resolve, a kind, shape or theme a
library does not have, a layout hint the layout could not honour) is issued as
`diadraw.DiaDrawWarning` through Python's `warnings` module, so
`warnings.simplefilter("error", diadraw.DiaDrawWarning)` turns it into an error.

## Develop

```bash
python -m venv .venv && . .venv/bin/activate
pip install scikit-build-core nanobind pytest
pip install --no-build-isolation -e . -C editable.rebuild=true   # rebuilds the module on import after a change
pytest
```

The bindings are in `bindings/python/src/module.cpp`, the package in `bindings/python/diadraw`
(with type stubs, `_core.pyi`), the tests in `bindings/python/tests`. The build is the root
`CMakeLists.txt` with `DIADRAW_BUILD_PYTHON=ON` and the apps, CLI and C++ tests off (see
`pyproject.toml`).
