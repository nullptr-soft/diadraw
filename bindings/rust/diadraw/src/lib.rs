//! Safe Rust bindings to libdiadraw, the DiaDraw C++ core.
//!
//! The functions here do what the `diadraw` command line does, on text in memory: [`check`],
//! [`format`], [`parse`], [`layout`], [`render`] (SVG, PNG, PDF, draw.io, BPMN) and [`import`]
//! (Mermaid, PlantUML, Structurizr, draw.io, BPMN). [`Document`] is an editable model with undo
//! history that takes the model operations the editors use.
//!
//! Structured results are JSON strings in the same shape as the `@diadraw/wasm` TypeScript types;
//! parse them with the JSON library you already use. Text positions in them (diagnostic columns,
//! parse offsets) count UTF-16 code units.
//!
//! ```no_run
//! let source = "model {\n  a = process \"A\"\n  b = process \"B\"\n  a -> b\n}\n";
//! let svg = diadraw::render(source, diadraw::Format::Svg, &diadraw::RenderOptions::default())?;
//! std::fs::write("diagram.svg", svg)?;
//! # Ok::<(), Box<dyn std::error::Error>>(())
//! ```
//!
//! The core keeps caches without locks, so every call goes through one process-wide lock: calls
//! from several threads are safe and run one at a time.

use std::ffi::{c_char, CStr, CString};
use std::fmt;
use std::path::Path;
use std::ptr::{self, NonNull};
use std::sync::{Mutex, MutexGuard};

use diadraw_sys as sys;

// ── Errors ──────────────────────────────────────────────────────────────────────────────────────

/// What kind of failure an [`Error`] is.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum ErrorKind {
    /// Malformed input: JSON that does not parse, text with a NUL byte, an unknown format.
    InvalidArgument,
    /// Well-formed, but refused: source with errors, an operation the model rejects.
    Rejected,
    /// An unknown view.
    NotFound,
    /// This build cannot do it (for example, it has no PNG backend).
    Unsupported,
    /// The core failed.
    Failed,
}

/// An error from the core, with its message.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    kind: ErrorKind,
    message: String,
}

impl Error {
    fn new(kind: ErrorKind, message: impl Into<String>) -> Self {
        Self {
            kind,
            message: message.into(),
        }
    }

    pub fn kind(&self) -> ErrorKind {
        self.kind
    }

    /// The core's message; for source with errors, one `line:column: error: message` per line.
    pub fn message(&self) -> &str {
        &self.message
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

fn kind_of(status: sys::dd_status) -> ErrorKind {
    match status {
        sys::DD_INVALID_ARGUMENT => ErrorKind::InvalidArgument,
        sys::DD_REJECTED => ErrorKind::Rejected,
        sys::DD_NOT_FOUND => ErrorKind::NotFound,
        sys::DD_UNSUPPORTED => ErrorKind::Unsupported,
        _ => ErrorKind::Failed,
    }
}

// ── Plumbing ────────────────────────────────────────────────────────────────────────────────────

static CORE: Mutex<()> = Mutex::new(());

/// The process-wide lock around the core. A panic while it was held cannot have left the core
/// inconsistent (no Rust code runs inside it), so a poisoned lock is taken over.
fn lock() -> MutexGuard<'static, ()> {
    CORE.lock().unwrap_or_else(|e| e.into_inner())
}

fn c_string(what: &str, text: &str) -> Result<CString> {
    CString::new(text).map_err(|_| {
        Error::new(
            ErrorKind::InvalidArgument,
            format!("{what} contains a NUL byte"),
        )
    })
}

fn c_path(path: Option<&Path>) -> Result<Option<CString>> {
    path.map(|p| {
        let text = p.to_str().ok_or_else(|| {
            Error::new(
                ErrorKind::InvalidArgument,
                format!("{} is not UTF-8", p.display()),
            )
        })?;
        c_string("file", text)
    })
    .transpose()
}

fn opt_ptr(s: &Option<CString>) -> *const c_char {
    s.as_ref().map_or(ptr::null(), |s| s.as_ptr())
}

/// Takes ownership of a string the core returned.
///
/// # Safety
/// `s` is NULL or a NUL-terminated string from the core that nothing else frees.
unsafe fn take_string(s: *mut c_char) -> Option<String> {
    if s.is_null() {
        return None;
    }
    let out = CStr::from_ptr(s).to_string_lossy().into_owned();
    sys::dd_string_free(s);
    Some(out)
}

/// Takes the `error` out-parameter, if the core set it.
///
/// # Safety
/// As for [`take_string`].
unsafe fn take_error(kind: ErrorKind, error: *mut c_char) -> Error {
    Error::new(
        kind,
        take_string(error).unwrap_or_else(|| "unknown error".into()),
    )
}

/// Calls a core function that returns a string, or NULL and an error message.
fn string_call(
    kind: ErrorKind,
    call: impl FnOnce(*mut *mut c_char) -> *mut c_char,
) -> Result<String> {
    let mut error: *mut c_char = ptr::null_mut();
    let out = {
        let _core = lock();
        call(&mut error)
    };
    // SAFETY: both pointers come from the core and are freed only here.
    unsafe {
        match take_string(out) {
            Some(s) => {
                drop(take_string(error));
                Ok(s)
            }
            None => Err(take_error(kind, error)),
        }
    }
}

// ── Library ─────────────────────────────────────────────────────────────────────────────────────

/// The core's version, e.g. `0.0.6` (the repository's VERSION file).
pub fn version() -> &'static str {
    // SAFETY: a static NUL-terminated string.
    unsafe { CStr::from_ptr(sys::dd_version()) }
        .to_str()
        .unwrap_or("")
}

/// The C ABI version the linked core implements.
pub fn abi_version() -> i32 {
    unsafe { sys::dd_abi_version() }
}

// ── Text ────────────────────────────────────────────────────────────────────────────────────────

/// Parses DSL source: `{ model, diagnostics, sourceMap, operations }` as JSON. Source with errors
/// still parses; its diagnostics say what is wrong.
pub fn parse(source: &str) -> Result<String> {
    let source = c_string("source", source)?;
    string_call(ErrorKind::Failed, |_| unsafe {
        sys::dd_parse_json(source.as_ptr())
    })
}

/// Checks DSL source as `diadraw check` does: `{ errors, diagnostics: [{ line, column, severity,
/// message }], warnings }` as JSON, where `warnings` are what the libraries could not supply.
///
/// With `file`, libraries resolve as for a document saved there (its lockfile, the library cache,
/// then the built-in libraries; the file need not exist); without, from the built-in ones only.
pub fn check(source: &str, file: Option<&Path>) -> Result<String> {
    let source = c_string("source", source)?;
    let file = c_path(file)?;
    string_call(ErrorKind::Failed, |_| unsafe {
        sys::dd_check_json(source.as_ptr(), opt_ptr(&file))
    })
}

/// Canonical source, as `diadraw format`. Fails with [`ErrorKind::Rejected`] when the source has
/// errors.
pub fn format(source: &str) -> Result<String> {
    let source = c_string("source", source)?;
    string_call(ErrorKind::Rejected, |e| unsafe {
        sys::dd_format_source(source.as_ptr(), e)
    })
}

/// Canonical source for a model given as JSON.
pub fn print_model(model_json: &str) -> Result<String> {
    let json = c_string("model JSON", model_json)?;
    string_call(ErrorKind::InvalidArgument, |e| unsafe {
        sys::dd_print_model(json.as_ptr(), e)
    })
}

// ── Layout and rendering ────────────────────────────────────────────────────────────────────────

/// Output formats of [`render`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Format {
    Svg,
    Png,
    Pdf,
    /// draw.io XML, one page per view (or the chosen view).
    Drawio,
    /// BPMN 2.0 XML, one diagram per view (or the chosen view).
    Bpmn,
}

impl Format {
    fn raw(self) -> sys::dd_format {
        match self {
            Format::Svg => sys::DD_FORMAT_SVG,
            Format::Png => sys::DD_FORMAT_PNG,
            Format::Pdf => sys::DD_FORMAT_PDF,
            Format::Drawio => sys::DD_FORMAT_DRAWIO,
            Format::Bpmn => sys::DD_FORMAT_BPMN,
        }
    }

    /// The format for a file name's extension: `.svg`, `.png`, `.pdf`, `.drawio`/`.dio`, `.bpmn`.
    pub fn from_path(path: &Path) -> Option<Self> {
        let ext = path.extension()?.to_str()?.to_ascii_lowercase();
        Some(match ext.as_str() {
            "svg" => Format::Svg,
            "png" => Format::Png,
            "pdf" => Format::Pdf,
            "drawio" | "dio" => Format::Drawio,
            "bpmn" => Format::Bpmn,
            _ => return None,
        })
    }
}

/// Options for [`layout`] and [`render`].
#[derive(Clone, Debug, Default)]
pub struct RenderOptions<'a> {
    /// Where the document lives, for its libraries (see [`check`]).
    pub file: Option<&'a Path>,
    /// The view to draw; the first view when `None` (for draw.io and BPMN: every view).
    pub view: Option<&'a str>,
    /// PNG pixel density, at most 8; 2 when `None` (or 0). A larger, infinite or NaN scale is an
    /// [`ErrorKind::InvalidArgument`] error; a PNG over 16384 pixels a side is [`ErrorKind::Failed`].
    pub scale: Option<f64>,
}

/// Lays out a view: `{ width, height, nodes: { id: { x, y, width, height } }, edges: { id:
/// { points: [{ x, y }], label? } }, rotations, ignoredHints }` as JSON. Fails with
/// [`ErrorKind::Rejected`] for source with errors or an unknown view.
pub fn layout(source: &str, options: &RenderOptions<'_>) -> Result<String> {
    let source = c_string("source", source)?;
    let file = c_path(options.file)?;
    let view = options.view.map(|v| c_string("view", v)).transpose()?;
    string_call(ErrorKind::Rejected, |e| unsafe {
        sys::dd_layout_json(source.as_ptr(), opt_ptr(&file), opt_ptr(&view), e)
    })
}

/// Renders DSL source to the bytes of a file in `format`.
pub fn render(source: &str, format: Format, options: &RenderOptions<'_>) -> Result<Vec<u8>> {
    let source = c_string("source", source)?;
    let file = c_path(options.file)?;
    let view = options.view.map(|v| c_string("view", v)).transpose()?;
    let mut out: *mut c_char = ptr::null_mut();
    let mut len: usize = 0;
    let mut error: *mut c_char = ptr::null_mut();
    let status = {
        let _core = lock();
        unsafe {
            sys::dd_render(
                source.as_ptr(),
                opt_ptr(&file),
                opt_ptr(&view),
                format.raw(),
                options.scale.unwrap_or(0.0),
                &mut out,
                &mut len,
                &mut error,
            )
        }
    };
    // SAFETY: on DD_OK `out` holds `len` bytes from the core; both pointers are freed only here.
    unsafe {
        if status != sys::DD_OK || out.is_null() {
            if !out.is_null() {
                sys::dd_string_free(out);
            }
            return Err(take_error(kind_of(status), error));
        }
        let bytes = std::slice::from_raw_parts(out.cast::<u8>(), len).to_vec();
        sys::dd_string_free(out);
        drop(take_string(error));
        Ok(bytes)
    }
}

// ── Import ──────────────────────────────────────────────────────────────────────────────────────

/// Formats [`import`] reads.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ImportFormat {
    Mermaid,
    PlantUml,
    Structurizr,
    Drawio,
    Bpmn,
}

impl ImportFormat {
    fn name(self) -> &'static CStr {
        match self {
            ImportFormat::Mermaid => c"mermaid",
            ImportFormat::PlantUml => c"plantuml",
            ImportFormat::Structurizr => c"structurizr",
            ImportFormat::Drawio => c"drawio",
            ImportFormat::Bpmn => c"bpmn",
        }
    }
}

/// Options for [`import`].
#[derive(Clone, Debug)]
pub struct ImportOptions<'a> {
    /// The file the data came from: its name tells the format when `format` is `None`, and
    /// includes (`!include`) are read from disk relative to it, or at absolute paths, never over
    /// the network. Without a file, includes are not read at all; leave it `None` for data from
    /// an untrusted source.
    pub file: Option<&'a Path>,
    pub format: Option<ImportFormat>,
    /// Keep the positions of formats that have them (draw.io, BPMN).
    pub keep_geometry: bool,
}

impl Default for ImportOptions<'_> {
    fn default() -> Self {
        Self {
            file: None,
            format: None,
            keep_geometry: true,
        }
    }
}

/// Imports a diagram in another format: `{ source, model, report: { dropped, notes, … } }` as
/// JSON, where `source` is the DiaDraw DSL text and `report` says what could not be carried over.
pub fn import(data: &str, options: &ImportOptions<'_>) -> Result<String> {
    let data = c_string("data", data)?;
    let file = c_path(options.file)?;
    let format = options.format.map_or(ptr::null(), |f| f.name().as_ptr());
    string_call(ErrorKind::InvalidArgument, |e| unsafe {
        sys::dd_import_json(
            data.as_ptr(),
            opt_ptr(&file),
            format,
            i32::from(options.keep_geometry),
            e,
        )
    })
}

// ── Documents ───────────────────────────────────────────────────────────────────────────────────

/// An editable model with undo history.
pub struct Document {
    raw: NonNull<sys::dd_document>,
}

// SAFETY: a document owns its model and history outright; every call into the core goes through
// the process-wide lock, so it may move between threads.
unsafe impl Send for Document {}

impl Document {
    fn wrap(raw: *mut sys::dd_document, error: *mut c_char, kind: ErrorKind) -> Result<Self> {
        match NonNull::new(raw) {
            Some(raw) => {
                unsafe { drop(take_string(error)) };
                Ok(Self { raw })
            }
            None => Err(unsafe { take_error(kind, error) }),
        }
    }

    /// A document with an empty model.
    pub fn new() -> Self {
        let raw = {
            let _core = lock();
            unsafe { sys::dd_document_new() }
        };
        Self {
            raw: NonNull::new(raw).expect("out of memory"),
        }
    }

    /// A document from model JSON.
    pub fn from_json(model_json: &str) -> Result<Self> {
        let json = c_string("model JSON", model_json)?;
        let mut error = ptr::null_mut();
        let raw = {
            let _core = lock();
            unsafe { sys::dd_document_from_json(json.as_ptr(), &mut error) }
        };
        Self::wrap(raw, error, ErrorKind::InvalidArgument)
    }

    /// A document from DSL source; fails with [`ErrorKind::Rejected`] when it has errors.
    pub fn from_source(source: &str) -> Result<Self> {
        let source = c_string("source", source)?;
        let mut error = ptr::null_mut();
        let raw = {
            let _core = lock();
            unsafe { sys::dd_document_from_source(source.as_ptr(), &mut error) }
        };
        Self::wrap(raw, error, ErrorKind::Rejected)
    }

    /// Applies one model operation given as JSON, e.g.
    /// `{"type":"addElement","element":{"id":"a","kind":"process"}}`.
    pub fn apply(&mut self, operation_json: &str) -> Result<()> {
        let op = c_string("operation JSON", operation_json)?;
        let mut error = ptr::null_mut();
        let status = {
            let _core = lock();
            unsafe { sys::dd_document_apply(self.raw.as_ptr(), op.as_ptr(), &mut error) }
        };
        if status == sys::DD_OK {
            unsafe { drop(take_string(error)) };
            Ok(())
        } else {
            Err(unsafe { take_error(kind_of(status), error) })
        }
    }

    /// Undoes the last operation; `false` when there was nothing to undo.
    pub fn undo(&mut self) -> bool {
        let _core = lock();
        unsafe { sys::dd_document_undo(self.raw.as_ptr()) != 0 }
    }

    /// Redoes the last undone operation; `false` when there was nothing to redo.
    pub fn redo(&mut self) -> bool {
        let _core = lock();
        unsafe { sys::dd_document_redo(self.raw.as_ptr()) != 0 }
    }

    pub fn can_undo(&self) -> bool {
        let _core = lock();
        unsafe { sys::dd_document_can_undo(self.raw.as_ptr()) != 0 }
    }

    pub fn can_redo(&self) -> bool {
        let _core = lock();
        unsafe { sys::dd_document_can_redo(self.raw.as_ptr()) != 0 }
    }

    /// The model as JSON.
    pub fn model_json(&self) -> String {
        string_call(ErrorKind::Failed, |_| unsafe {
            sys::dd_document_model_json(self.raw.as_ptr())
        })
        .expect("out of memory")
    }

    /// The model as canonical DSL source.
    pub fn source(&self) -> String {
        string_call(ErrorKind::Failed, |_| unsafe {
            sys::dd_document_source(self.raw.as_ptr())
        })
        .expect("out of memory")
    }

    /// The elements, relationships and groups view `view_id` shows, as JSON.
    pub fn resolve_view(&self, view_id: &str) -> Result<String> {
        let id = c_string("view", view_id)?;
        string_call(ErrorKind::NotFound, |e| unsafe {
            sys::dd_document_resolve_view_json(self.raw.as_ptr(), id.as_ptr(), e)
        })
    }
}

impl Default for Document {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        let _core = lock();
        unsafe { sys::dd_document_free(self.raw.as_ptr()) };
    }
}

impl fmt::Debug for Document {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Document").finish_non_exhaustive()
    }
}
