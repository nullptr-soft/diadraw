"""DiaDraw for Python: parse, check, format, render and import diagrams with the DiaDraw core.

>>> import diadraw
>>> svg = diadraw.render('model { a = system "A"\\n b = system "B"\\n a -> b "uses" }')

Models, operations and resolved views are plain dicts and lists in the shape of DiaDraw's model
JSON. Problems in a document raise :class:`DiaDrawError` (a ``ValueError``) whose
``diagnostics`` say where; what the command line prints as warnings is issued as
:class:`DiaDrawWarning` through the :mod:`warnings` module.
"""

import os as _os
from pathlib import Path as _Path

from . import _core

# draw.io's stencil sets ship inside the package; $DIADRAW_DRAWIO still points elsewhere.
_DRAWIO = _Path(__file__).resolve().parent / "drawio"
if not _os.environ.get("DIADRAW_DRAWIO") and (_DRAWIO / "index.json.gz").is_file():
    _core._set_drawio_dir(str(_DRAWIO))

from ._core import (  # noqa: E402
    Diagnostic,
    DiaDrawError,
    DiaDrawWarning,
    Document,
    check,
    format,
    highlight,
    import_diagram,
    parse,
    print_model,
    render,
    update_source,
    version,
)

__version__: str = version()

__all__ = [
    "Diagnostic",
    "DiaDrawError",
    "DiaDrawWarning",
    "Document",
    "__version__",
    "check",
    "format",
    "highlight",
    "import_diagram",
    "parse",
    "print_model",
    "render",
    "update_source",
    "version",
]
