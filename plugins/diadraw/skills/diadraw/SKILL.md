---
name: diadraw
description: Draw, edit and render diagrams with DiaDraw's text DSL (.draw files). Use when asked for an architecture, C4, flowchart, sequence, UML, BPMN or other diagram, when editing a .draw file, or when converting Mermaid, PlantUML, Structurizr, draw.io or BPMN into DiaDraw.
---

# Drawing diagrams with DiaDraw

DiaDraw diagrams are plain text: a `.draw` file holds a `model` (the things and the lines
between them, declared once) and one or more `view`s (which things to show and how to lay them
out). You write the text, the `diadraw` CLI checks it and renders it to SVG, PNG or PDF, and you
look at the image and fix what is wrong. Layout is automatic: never compute coordinates unless
the user asks for a hand-placed (`layout fixed`) diagram.

## 1. Find the CLI

If the `diadraw` MCP server's tools are available in this session, use its check and render
tools for steps 3 and 4 instead of the shell; everything else here still applies.

Otherwise find the binary once and reuse the path:

```bash
DIADRAW="${DIADRAW:-$(command -v diadraw || true)}"
for p in ../diadraw-build/release/diadraw ../diadraw-build/dev/diadraw; do
  [ -z "$DIADRAW" ] && [ -x "$p" ] && DIADRAW="$p"
done
"$DIADRAW" --version
```

If none is found and you are inside a clone of `github.com/mykulyak/diadraw`, build it (a few
minutes): `cmake --preset release && cmake --build --preset release --target diadraw`. The
binary lands in `../diadraw-build/release/diadraw`. Otherwise ask the user where DiaDraw is
installed, or to set `DIADRAW` to the binary's path.

## 2. Write the diagram

Start from the closest pattern in [references/patterns.md](references/patterns.md) (C4 and
architecture, flowchart, sequence, UML class, activity, state, use case, BPMN). The syntax is
summarised in [references/dsl.md](references/dsl.md). A minimal file:

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout {
  include *
  layout layered direction right
}
```

Save it next to where the user wants the picture, e.g. `docs/diagrams/checkout.draw`.

## 3. Check

```bash
"$DIADRAW" check docs/diagrams/checkout.draw
```

It prints `file: ok` and exits 0, or prints `file:line:column: error|warning: message` lines and
exits 1 on any error. Fix every error. Fix warnings too unless they are intended: an unknown
element kind or an unknown `layout` name is only a warning, so a typo still renders, but not the
way you meant.

## 4. Render and look

```bash
"$DIADRAW" render docs/diagrams/checkout.draw --view checkout -o /tmp/checkout.png
```

Then open the PNG with the Read tool and look at it. Check that:

- every element and line you meant is there, with readable labels;
- the flow goes in a sensible direction and lines do not cross more than they must;
- nothing important is hidden behind a note, a box or another element;
- `render` printed no `warning: layout could not honour hint …` (that warning appears only on
  `render`, not on `check`).

If it looks wrong, change the text and repeat from step 3. Usual fixes, in order: change
`direction`; switch the algorithm (see the layout table in [references/dsl.md](references/dsl.md));
split a crowded view into two views with `include` lists or `view … of parent`; add a hint
(`pin a below b`, `align a b horizontally`, `order a b c`); shorten labels or break them with
`\n`. Two or three rounds are usually enough; stop when the picture is clear, not perfect.

`render` details:

- The output format follows the extension: `.svg`, `.png`, `.pdf`, `.drawio` (every view as a
  draw.io page) or `.bpmn`. Without `-o` it writes SVG to stdout.
- `--view NAME` picks the view; without it the first view is drawn. An unknown name lists the
  views.
- `--scale N` sets PNG resolution (default 2).
- `render` refuses a file with errors (exit 1, nothing written).

## 5. Deliver

Render the final output the user asked for (SVG for docs and READMEs, PNG for chat or slides,
PDF for print) and keep the `.draw` source beside it: the text is the source of truth and
diffs cleanly in git. Optionally run `"$DIADRAW" format file.draw --write` to normalise layout of
the text. Tell the user the file can also be opened and edited visually in the DiaDraw web or
desktop app.

## Editing an existing diagram

Read the whole `.draw` file first. Change the smallest span you can and keep comments, order and
hand-written hints. Never drop `pin`, `route`, `size` or `rotate` lines in a `layout fixed`
view: they are the user's hand placement. Relationship ids come from their endpoints (`a->b`,
`a->b#2` for a second one) unless named (`pay = a -> b`); if you change an endpoint of a line
that a `route` names, update the `route` too, or name the relationship.

## Converting from other formats

```bash
"$DIADRAW" import diagram.mmd -o diagram.draw            # Mermaid
"$DIADRAW" import diagram.puml -o diagram.draw           # PlantUML
"$DIADRAW" import workspace.dsl -o diagram.draw          # Structurizr
"$DIADRAW" import diagram.drawio -o diagram.draw         # draw.io (positions kept)
"$DIADRAW" import diagram.drawio --geometry drop -o diagram.draw   # draw.io, auto layout
```

`import` reports anything it could not carry over. Check and render the result as above.

## Things that trip agents up

- Identifiers are `[A-Za-z_][A-Za-z0-9_]*`: no dashes or spaces. Put the readable name in the
  label: `order_db = container "Order DB"`.
- Nested elements are referenced with dots (`shop.web`) from outside their parent; inside
  `shop { … }` plain `web` works.
- A quoted string is single-line; write `\n` for a line break. A block string spans lines and
  works wherever a string does, which suits longer text such as a description: `description """`,
  then the lines, indented, then `"""` on its own line. The indentation the lines share is
  dropped.
- A `view … of shop` shows the children of `shop`; a view without `of` shows top-level elements
  and their children.
- Notes, text and boxes belong in a view, not the model (`note "…" below web`).
- Sequence messages go in the view's `sequence { … }` block, not in the model.
- Inline `style "k=v;…"` switches an element to draw.io's look (white fill, black stroke) for
  whatever it does not set; to recolour by kind or tag keep the theme look with a
  `style container { fill "#…" }` or `style tag "…" { … }` block instead.

## More

Full reference, in the DiaDraw repository (`github.com/mykulyak/diadraw`):
`docs/dsl.md` (DSL), `docs/diadraw.grammar` (grammar), `docs/guide/` (user guide: layouts,
relationships, line styles, styles and themes, fixed views), `docs/libraries.md` (shape
libraries) and `corpus/*.draw` (the example diagrams, including UML and BPMN). Read those when
this skill and its references do not cover what you need.
