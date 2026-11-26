//! Raw FFI bindings to libdiadraw's C ABI (`src/capi/include/diadraw/diadraw.h`), ABI version 2.
//!
//! Every pointer this library returns is owned by the caller and released with
//! [`dd_string_free`] (strings and byte buffers) or [`dd_document_free`] (documents). The core
//! keeps caches without locks, so call it from one thread at a time. Use the `diadraw` crate for a
//! safe API.

#![allow(non_camel_case_types)]

use std::os::raw::{c_char, c_int};

/// The ABI version these declarations describe; `dd_abi_version()` must be at least this.
pub const DD_ABI_VERSION: c_int = 2;

/// An editable model with undo history.
#[repr(C)]
pub struct dd_document {
    _private: [u8; 0],
    _not_send_sync: std::marker::PhantomData<(*mut u8, std::marker::PhantomPinned)>,
}

pub type dd_status = c_int;
pub const DD_OK: dd_status = 0;
pub const DD_INVALID_ARGUMENT: dd_status = 1;
pub const DD_REJECTED: dd_status = 2;
pub const DD_NOT_FOUND: dd_status = 3;
pub const DD_UNSUPPORTED: dd_status = 4;
pub const DD_FAILED: dd_status = 5;

pub type dd_format = c_int;
pub const DD_FORMAT_SVG: dd_format = 0;
pub const DD_FORMAT_PNG: dd_format = 1;
pub const DD_FORMAT_PDF: dd_format = 2;
pub const DD_FORMAT_DRAWIO: dd_format = 3;
pub const DD_FORMAT_BPMN: dd_format = 4;

extern "C" {
    // ── ABI 1 ──
    pub fn dd_version() -> *const c_char;
    pub fn dd_abi_version() -> c_int;
    pub fn dd_string_free(s: *mut c_char);

    pub fn dd_document_new() -> *mut dd_document;
    pub fn dd_document_from_json(
        model_json: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut dd_document;
    pub fn dd_document_free(doc: *mut dd_document);
    pub fn dd_document_apply(
        doc: *mut dd_document,
        operation_json: *const c_char,
        error: *mut *mut c_char,
    ) -> dd_status;
    pub fn dd_document_undo(doc: *mut dd_document) -> c_int;
    pub fn dd_document_redo(doc: *mut dd_document) -> c_int;
    pub fn dd_document_can_undo(doc: *const dd_document) -> c_int;
    pub fn dd_document_can_redo(doc: *const dd_document) -> c_int;
    pub fn dd_document_model_json(doc: *const dd_document) -> *mut c_char;
    pub fn dd_document_resolve_view_json(
        doc: *const dd_document,
        view_id: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut c_char;

    // ── ABI 2 ──
    pub fn dd_parse_json(source: *const c_char) -> *mut c_char;
    pub fn dd_check_json(source: *const c_char, file: *const c_char) -> *mut c_char;
    pub fn dd_format_source(source: *const c_char, error: *mut *mut c_char) -> *mut c_char;
    pub fn dd_print_model(model_json: *const c_char, error: *mut *mut c_char) -> *mut c_char;
    pub fn dd_document_from_source(
        source: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut dd_document;
    pub fn dd_document_source(doc: *const dd_document) -> *mut c_char;
    pub fn dd_layout_json(
        source: *const c_char,
        file: *const c_char,
        view_id: *const c_char,
        error: *mut *mut c_char,
    ) -> *mut c_char;
    pub fn dd_render(
        source: *const c_char,
        file: *const c_char,
        view_id: *const c_char,
        format: dd_format,
        scale: f64,
        out: *mut *mut c_char,
        out_len: *mut usize,
        error: *mut *mut c_char,
    ) -> dd_status;
    pub fn dd_import_json(
        data: *const c_char,
        file: *const c_char,
        format: *const c_char,
        keep_geometry: c_int,
        error: *mut *mut c_char,
    ) -> *mut c_char;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_matches() {
        assert!(unsafe { dd_abi_version() } >= DD_ABI_VERSION);
    }
}
