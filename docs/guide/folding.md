# Folding blocks in the text

Long diagrams are easier to find your way around with some of their blocks folded away. The
text pane shows a small triangle in the margin, left of the text, on every block that spans
lines: the `model` block, each `view`, every element with children (a system with its
containers, a container with its ports, a group) and other `{` … `}` blocks such as `style` and
`theme`. Click the triangle (▾) to fold the block: the lines between its `{` and its `}` are
hidden, the `}` follows right under the `{` line, and the triangle turns (▸). Click it again to
show them. The web app puts a `…` after the folded `{` (click it to unfold too); the macOS and
Linux apps shade the folded `{` line instead.

```
▾ model {
▸   shop = system "Shop" {
    }
    customer -> shop "buys"
  }
```

Folding only changes what the text pane shows: the file, the canvas and undo are not affected,
and copying or saving keeps every line. A folded block stays folded while you edit around it,
and opens again when its braces go, or when the caret moves into it, for example when you
select one of its elements on the canvas.

In the web app the usual CodeMirror keys work too: Ctrl+Shift+[ folds the block at the caret
and Ctrl+Shift+] unfolds it (⌘⌥[ and ⌘⌥] on macOS). In VS Code, DiaDraw's language server
gives the same blocks to the editor's own folding.
