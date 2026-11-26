#!/usr/bin/env python3
"""Finds calls that move an object in one argument and read it in another, such as
`map.set(e.id, std::move(e))`. Argument evaluation order is unspecified: GCC on x86-64 and MSVC
go right to left, so the move happens first and the read sees a moved-from object. Brace
initializers are fine (they are evaluated left to right). Exits 1 when it finds one.

Usage (from the repo root): python3 tools/check-move-order.py
"""
from glob import glob
import os
import re
import sys

ROOTS = [*glob("src/*/src"), *glob("src/*/include"), *glob("src/apps/*/src")]
MOVE = re.compile(r"std::move\(\s*\**\s*([A-Za-z_][A-Za-z0-9_]*)")
CALL = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s*\(")


def blank_literals(text: str) -> str:
    """Strings, characters and comments as spaces (newlines kept), so their brackets don't count."""
    out, i, n = [], 0, len(text)
    while i < n:
        raw = re.match(r'R"([^(\s]*)\(', text[i:])
        if raw:
            end = text.find(")" + raw.group(1) + '"', i)
            end = n if end < 0 else end + len(raw.group(1)) + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i)
            end = n if end < 0 else end + 2
            out.append(re.sub(r"[^\n]", " ", text[i:end]))
            i = end
        elif text[i] in "\"'":
            quote, j = text[i], i + 1
            while j < n and text[j] != quote and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(quote + " " * (j - i - 1) + quote)
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def arguments(text: str, start: int) -> tuple[list[str], int]:
    depth, j = 1, start
    while j < len(text) and depth:
        depth += {"(": 1, ")": -1}.get(text[j], 0)
        j += 1
    parts, cur, d = [], "", 0
    for ch in text[start : j - 1]:
        if ch in "([{":
            d += 1
        elif ch in ")]}":
            d -= 1
        if ch == "," and d == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    return parts + [cur], j


found = []
for root in ROOTS:
    for folder, _, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".hpp", ".h", ".mm")):
                continue
            path = os.path.join(folder, name)
            text = blank_literals(open(path, encoding="utf-8", errors="ignore").read())
            for call in CALL.finditer(text):
                args, _ = arguments(text, call.end())
                if len(args) < 2 or not any("std::move(" in a for a in args):
                    continue
                for k, arg in enumerate(args):
                    for move in MOVE.finditer(arg):
                        var = re.escape(move.group(1))
                        if any(q != k and re.search(rf"(?<![\w.>]){var}\s*(\.|->|\[)", b) for q, b in enumerate(args)):
                            line = text[: call.start()].count("\n") + 1
                            found.append(f"{path}:{line}: {move.group(1)} is moved and read in one call")
for f in sorted(set(found)):
    print(f)
sys.exit(1 if found else 0)
