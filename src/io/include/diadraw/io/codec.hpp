#pragma once

// Byte-level helpers the importers need, with the browser semantics the TypeScript code relied
// on: `atob`, `DecompressionStream` (deflate, deflate-raw, gzip), `decodeURIComponent`, and
// `TextDecoder('latin1')` (which is windows-1252).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dd::io {

/// `atob(text.replace(/\s+/g, ''))`; nullopt where `atob` would throw.
std::optional<std::string> base64_decode(std::string_view text);

/// Raw deflate (RFC 1951); nullopt on malformed input.
std::optional<std::string> inflate_raw(std::string_view bytes);
/// zlib-wrapped deflate (RFC 1950).
std::optional<std::string> inflate_zlib(std::string_view bytes);
/// gzip (RFC 1952); plain bytes are returned as they are, as a server may have undone the gzip.
std::optional<std::string> gunzip(std::string_view bytes);
/// gzip-compresses bytes (deflate at the best compression level, no file name).
std::string gzip(std::string_view bytes);
/// Standard base64 with padding (`btoa`, `Buffer.toString('base64')`).
std::string base64_encode(std::string_view bytes);
/// The SHA-256 digest of bytes (32 raw bytes).
std::string sha256(std::string_view bytes);
/// CRC-32 (IEEE), as gzip and PNG use it.
std::uint32_t crc32(std::string_view bytes, std::uint32_t crc = 0);

/// `decodeURIComponent`; nullopt where it would throw (a malformed escape or invalid UTF-8).
std::optional<std::string> decode_uri_component(std::string_view text);
/// Percent-encodes `bytes` for a URL query value: everything but ASCII letters, digits and `-._~`
/// becomes `%XX`. Stricter than `encodeURIComponent` (which keeps `!'()*`), and works on bytes,
/// so `decode_uri_component` undoes it for any valid UTF-8.
std::string percent_encode(std::string_view bytes);
/// `try { decodeURIComponent(text) } catch { text }`
std::string safe_decode_uri(std::string_view text);

/// `new TextDecoder('latin1').decode(bytes)`, which browsers implement as windows-1252.
std::string decode_windows1252(std::string_view bytes);

}  // namespace dd::io
