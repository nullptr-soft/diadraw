#include "diadraw/io/codec.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "diadraw/base/text.hpp"
#include "miniz.h"

namespace dd::io {

std::optional<std::string> base64_decode(std::string_view text) {
  std::string clean;
  for (std::size_t i = 0; i < text.size();) {
    std::size_t start = i;
    if (!is_js_whitespace(next_code_point(text, i))) clean.append(text, start, i - start);
  }
  // atob: optional padding to a multiple of four, and a length that is not 1 mod 4.
  if (clean.size() % 4 == 0 && !clean.empty() && clean.back() == '=') {
    clean.pop_back();
    if (!clean.empty() && clean.back() == '=') clean.pop_back();
  }
  if (clean.size() % 4 == 1) return std::nullopt;
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  unsigned buffer = 0;
  int bits = 0;
  for (char c : clean) {
    int v = value(c);
    if (v < 0) return std::nullopt;
    buffer = (buffer << 6) | static_cast<unsigned>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

namespace {

constexpr std::string_view kTooLarge = "The decompressed data is larger than ";

std::string too_large_message(std::size_t limit) { return std::string(kTooLarge) + describe_size(limit); }

Result<std::string> tinfl(std::string_view bytes, int flags, std::size_t limit) {
  // tinfl_decompress_mem_to_heap, with the output capped: the buffer grows by doubling up to
  // `limit`, and a stream that still has output once the buffer holds `limit` bytes is refused.
  // The whole output stays in one buffer, as deflate refers back up to 32 KiB into it.
  tinfl_decompressor decomp;
  tinfl_init(&decomp);
  // A first guess that fits most text without regrowing; it never passes the limit.
  // Large inputs start smaller, so a 20 MiB page does not zero-fill the whole limit up front.
  std::size_t guess = bytes.size() < limit / 4   ? std::max<std::size_t>(1024, bytes.size() * 4)
                      : bytes.size() < limit / 2 ? bytes.size() * 2
                                                 : limit;
  std::string out(std::min(limit, guess), '\0');
  std::size_t in_at = 0, out_len = 0;
  for (;;) {
    std::size_t in_size = bytes.size() - in_at, out_size = out.size() - out_len;
    auto* base = reinterpret_cast<mz_uint8*>(out.data());
    tinfl_status status = tinfl_decompress(&decomp, reinterpret_cast<const mz_uint8*>(bytes.data()) + in_at, &in_size,
                                           base, base + out_len, &out_size,
                                           flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    in_at += in_size;
    out_len += out_size;
    if (status == TINFL_STATUS_DONE) break;
    if (status != TINFL_STATUS_HAS_MORE_OUTPUT) return fail("The data is not valid deflate data");
    if (out.size() >= limit) return fail(too_large_message(limit));
    out.resize(out.size() > limit / 2 ? limit : out.size() * 2);
  }
  // An empty stream decompresses to nothing, which no importer can use.
  if (out_len == 0) return fail("The data decompresses to nothing");
  out.resize(out_len);
  return out;
}

}  // namespace

std::string describe_size(std::size_t bytes) {
  constexpr std::size_t kMiB = std::size_t{1} << 20;
  return bytes % kMiB == 0 && bytes > 0 ? std::to_string(bytes / kMiB) + " MiB" : std::to_string(bytes) + " bytes";
}

bool is_too_large(const Error& error) { return error.message.starts_with(kTooLarge); }

Result<std::string> inflate_raw(std::string_view bytes, std::size_t limit) { return tinfl(bytes, 0, limit); }

Result<std::string> inflate_zlib(std::string_view bytes, std::size_t limit) {
  return tinfl(bytes, TINFL_FLAG_PARSE_ZLIB_HEADER, limit);
}

Result<std::string> gunzip(std::string_view bytes, std::size_t limit) {
  if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0x1F || static_cast<unsigned char>(bytes[1]) != 0x8B)
    return std::string(bytes);
  auto truncated = [] { return fail("The gzip data is truncated"); };
  if (bytes.size() < 18) return truncated();
  auto flags = static_cast<unsigned char>(bytes[3]);
  std::size_t at = 10;
  if (flags & 0x04) {  // FEXTRA
    if (at + 2 > bytes.size()) return truncated();
    at += 2 + (static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1]) << 8));
  }
  auto skip_zero = [&] {
    while (at < bytes.size() && bytes[at] != '\0') ++at;
    ++at;
  };
  if (flags & 0x08) skip_zero();  // FNAME
  if (flags & 0x10) skip_zero();  // FCOMMENT
  if (flags & 0x02) at += 2;      // FHCRC
  if (at + 8 > bytes.size()) return truncated();
  return inflate_raw(bytes.substr(at, bytes.size() - at - 8), limit);
}

std::uint32_t crc32(std::string_view bytes, std::uint32_t crc) {
  static const auto kTable = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (char ch : bytes) crc = kTable[(crc ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

std::string gzip(std::string_view bytes) {
  std::size_t out_len = 0;
  void* deflated = tdefl_compress_mem_to_heap(bytes.data(), bytes.size(), &out_len,
                                              TDEFL_DEFAULT_MAX_PROBES * 4 | TDEFL_GREEDY_PARSING_FLAG * 0);
  std::string out("\x1F\x8B\x08\x00\x00\x00\x00\x00\x02\xFF", 10);  // no name, max compression, unknown OS
  if (deflated) {
    out.append(static_cast<const char*>(deflated), out_len);
    std::free(deflated);
  }
  auto le32 = [&](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  };
  le32(crc32(bytes));
  le32(static_cast<std::uint32_t>(bytes.size()));
  return out;
}

std::string base64_encode(std::string_view bytes) {
  static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    std::uint32_t v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                      static_cast<unsigned char>(bytes[i + 2]);
    for (int k = 18; k >= 0; k -= 6) out.push_back(kAlphabet[(v >> k) & 63]);
  }
  if (std::size_t rest = bytes.size() - i) {
    std::uint32_t v = static_cast<unsigned char>(bytes[i]) << 16;
    if (rest == 2) v |= static_cast<unsigned char>(bytes[i + 1]) << 8;
    out.push_back(kAlphabet[(v >> 18) & 63]);
    out.push_back(kAlphabet[(v >> 12) & 63]);
    out.push_back(rest == 2 ? kAlphabet[(v >> 6) & 63] : '=');
    out.push_back('=');
  }
  return out;
}

std::string sha256(std::string_view bytes) {
  static const std::uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
      0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
      0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
      0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string msg(bytes);
  std::uint64_t bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back('\0');
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
  auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (std::size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i])) << 24) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 1])) << 16) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 2])) << 8) |
             static_cast<std::uint32_t>(static_cast<unsigned char>(msg[chunk + 4 * i + 3]));
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      std::uint32_t ch = (e & f) ^ (~e & g);
      std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
      std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t t2 = s0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  std::string out;
  for (std::uint32_t v : h)
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  return out;
}

std::optional<std::string> decode_uri_component(std::string_view text) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] != '%') {
      out.push_back(text[i++]);
      continue;
    }
    // A run of escapes forms one UTF-8 sequence; it must be complete and valid.
    auto byte_at = [&](std::size_t k) -> int {
      if (k + 2 >= text.size() || text[k] != '%') return -1;
      int h = hex(text[k + 1]), l = hex(text[k + 2]);
      return h < 0 || l < 0 ? -1 : h * 16 + l;
    };
    int b0 = byte_at(i);
    if (b0 < 0) return std::nullopt;
    int len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : (b0 & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0) return std::nullopt;
    std::uint32_t cp = len == 1 ? static_cast<std::uint32_t>(b0)
                       : len == 2 ? static_cast<std::uint32_t>(b0 & 0x1F)
                       : len == 3 ? static_cast<std::uint32_t>(b0 & 0x0F)
                                  : static_cast<std::uint32_t>(b0 & 0x07);
    for (int k = 1; k < len; ++k) {
      int b = byte_at(i + static_cast<std::size_t>(3 * k));
      if (b < 0 || (b & 0xC0) != 0x80) return std::nullopt;
      cp = (cp << 6) | static_cast<std::uint32_t>(b & 0x3F);
    }
    // Overlong forms, surrogates and values past U+10FFFF are malformed.
    static const std::uint32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[len] || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return std::nullopt;
    append_utf8(out, cp);
    i += static_cast<std::size_t>(3 * len);
  }
  return out;
}

std::string safe_decode_uri(std::string_view text) {
  auto decoded = decode_uri_component(text);
  return decoded ? *decoded : std::string(text);
}

std::string percent_encode(std::string_view bytes) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(bytes.size());
  for (char ch : bytes) {
    auto c = static_cast<unsigned char>(ch);
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                 c == '.' || c == '_' || c == '~';
    if (plain) {
      out += ch;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
  return out;
}

std::string decode_windows1252(std::string_view bytes) {
  static const std::uint32_t k80[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                        0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
  std::string out;
  for (char c : bytes) {
    auto b = static_cast<unsigned char>(c);
    append_utf8(out, b >= 0x80 && b < 0xA0 ? k80[b - 0x80] : b);
  }
  return out;
}

}  // namespace dd::io
