# Formatting the text

**Format Document** tidies the text of the diagram you have open: it indents every line by how
deep it sits in `{ … }` blocks and evens out the spaces between words, so hand-typed or pasted text
reads like the rest. On macOS choose **Edit ▸ Format Document**; on Windows choose
**Edit ▸ Format Document**; on Linux choose **Format Document** in the menu; in the web app click
**Format** above the text. Everywhere the shortcut is ⇧⌥F (Shift+Alt+F), as in VS Code, where
**Format Document** works on `.draw` files too.

Formatting changes only whitespace:

- each line is indented by the indent size for every `{` it is inside, and a line that continues
  a statement from the line above one step more;
- words are one space apart, with one space around `=` and arrows, after a comma and before a
  `//` comment, and none before a comma;
- spaces at the ends of lines go (except after a string that is not closed yet), two or more
  blank lines become one, and blank lines right after a `{` or right before a `}` go.

Nothing else changes: statements stay in the order you wrote them, comments stay where they are,
and lines are never joined or split. Text that has mistakes in it is formatted too. The lines
inside a `"""` block string move along with the line it starts on, so the description reads
the same afterwards.

```
model {
customer=person "Customer"
   shop = system "Shop" {
  web = container "Web app"   tech "React"
      }
customer->shop.web "places order"   // the main flow
}
```

becomes

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
  }
  customer -> shop.web "places order" // the main flow
}
```

Formatting is one change, so a single **Undo** (⌘Z, or Ctrl+Z) takes it back.

## Indent size

The one setting is the indent size: how many spaces each level of indentation takes, 2 unless you
choose another. On macOS and Windows choose it in **Edit ▸ Indent Size**, on Linux in
**Indent Size** in the menu, and in the web app in **Indent** beside **Format**. The app remembers
it for every diagram. In VS Code, formatting follows the editor's own tab size.

`diadraw format` on the command line is a different tool: it rewrites the file in DiaDraw's one
canonical layout from the diagram itself, so it puts statements in its own order and drops
comments.
