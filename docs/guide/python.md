# DiaDraw from Python

The `diadraw` Python package runs the same C++ core as the apps and the command line, so a
script, a notebook or a build step can check, format, render and import diagrams without calling
the `diadraw` binary. It works on macOS, Linux and Windows, with Python 3.10 or later.

```python
import diadraw

source = open("shop.draw", encoding="utf-8").read()

for problem in diadraw.check(source, file="shop.draw"):
    print(f"shop.draw:{problem}")            # 3:18: error: Unexpected "42"

svg = diadraw.render(source, file="shop.draw")                 # first view, SVG text
png = diadraw.render(source, "checkout", format="png", scale=2)  # bytes
open("shop.drawio", "w").write(diadraw.render(source, format="drawio"))  # every view

mermaid = diadraw.import_diagram(open("flow.mmd").read(), "mermaid")
open("flow.draw", "w").write(mermaid["source"])
```

What each call does matches a command of the CLI:

- `check` is `diadraw check`: syntax and model errors, and libraries that did not resolve.
- `format` is `diadraw format`: the text in canonical form.
- `render` is `diadraw render`: SVG, PNG, PDF, draw.io or BPMN, picked with `format=`. Pass the
  file's path as `file=` so libraries resolve from the lockfile next to it, as they do on the
  command line.
- `import_diagram` is `diadraw import`: Mermaid, PlantUML, Structurizr, draw.io and BPMN, with a
  report of what could not be carried over.

A diagram with errors raises `diadraw.DiaDrawError`, whose `diagnostics` list each problem with
its line and column. Warnings the CLI would print, such as a layout hint the layout could not
honour, arrive as `diadraw.DiaDrawWarning` through Python's `warnings` module.

For tools that edit diagrams, `diadraw.Document` holds a model with undo and redo, and
`diadraw.update_source` applies an edit to the text while keeping your comments and layout, the
way the canvas writes its edits back.

## Installing

The package is built from the repository: `pip install .` in a checkout (it needs CMake and a
C++20 compiler), or a ready-made wheel from the repository's `Python` workflow. Every function,
and how to work on the bindings, is in [src/python/README.md](../../src/python/README.md).
