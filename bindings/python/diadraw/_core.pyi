"""Type stubs for the DiaDraw core extension module."""

from typing import Any, Literal, overload

Model = dict[str, Any]
Operation = dict[str, Any]

class DiaDrawError(ValueError):
    """A document DiaDraw cannot use; ``diagnostics`` lists what is wrong in its source."""

    diagnostics: list[Diagnostic]

class DiaDrawWarning(UserWarning):
    """Something DiaDraw left out: an unresolved library, a layout hint it could not honour."""

class Diagnostic:
    """A problem in DiaDraw source; line and column are 1-based, in code points."""

    @property
    def severity(self) -> Literal["error", "warning"]: ...
    @property
    def message(self) -> str: ...
    @property
    def line(self) -> int: ...
    @property
    def column(self) -> int: ...
    @property
    def start(self) -> int:
        """Code point offset where the problem starts."""
    @property
    def end(self) -> int:
        """Code point offset where it ends."""

class Document:
    """A model with an undo history."""

    def __init__(self) -> None:
        """An empty model."""
    @staticmethod
    def from_source(source: str) -> Document:
        """A document from DiaDraw source; raises DiaDrawError on errors."""
    @staticmethod
    def from_model(model: Model) -> Document:
        """A document from a model dict (the model JSON)."""
    def apply(self, operation: Operation) -> None:
        """Applies one operation, e.g. ``{"type": "addElement", "element": {...}}``."""
    def undo(self) -> bool:
        """Undoes the last operation; False when there was none."""
    def redo(self) -> bool:
        """Redoes the last undone operation; False when there was none."""
    @property
    def can_undo(self) -> bool: ...
    @property
    def can_redo(self) -> bool: ...
    @property
    def model(self) -> Model:
        """The model as a new dict, in the shape of the model JSON; each access converts the whole model."""
    @property
    def views(self) -> list[str]:
        """The ids of the views, in order."""
    def to_source(self) -> str:
        """The model as canonical DiaDraw source."""
    def resolve_view(self, view: str) -> dict[str, Any]:
        """The elements, relationships and groups a view shows, as a dict."""

DocumentLike = str | Model | Document

def _set_drawio_dir(dir: str) -> None: ...
def version() -> str:
    """The version of the DiaDraw core."""

def parse(source: str) -> tuple[Model, list[Diagnostic]]:
    """Parses DiaDraw source: (model dict, diagnostics). Never raises for errors in it."""

def check(source: str, *, file: str = "") -> list[Diagnostic]:
    """Every problem in DiaDraw source, libraries included."""

def format(source: str) -> str:
    """DiaDraw source in canonical form; raises DiaDrawError on errors."""

def print_model(model: Model) -> str:
    """A model dict as canonical DiaDraw source."""

def update_source(source: str, operation: Operation) -> dict[str, Any]:
    """Applies an operation to DiaDraw source, keeping the rest of the text as written."""

def highlight(text: str) -> list[tuple[int, int, str]]:
    """Syntax tokens: (start, end, kind) with code point offsets."""

@overload
def render(
    document: DocumentLike,
    view: str | None = None,
    *,
    format: Literal["svg", "drawio", "bpmn"] = "svg",
    scale: float = 2.0,
    background: str | None = None,
    file: str = "",
) -> str: ...
@overload
def render(
    document: DocumentLike,
    view: str | None = None,
    *,
    format: Literal["png", "pdf"],
    scale: float = 2.0,
    background: str | None = None,
    file: str = "",
) -> bytes: ...
def import_diagram(
    data: str | bytes,
    format: Literal["mermaid", "plantuml", "structurizr", "drawio", "bpmn"] | None = None,
    *,
    file_name: str = "",
    keep_geometry: bool = True,
) -> dict[str, Any]:
    """Converts a Mermaid, PlantUML, Structurizr, draw.io or BPMN diagram."""
