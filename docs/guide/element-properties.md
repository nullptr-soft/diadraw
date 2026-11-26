# Editing properties on the canvas

An element is more than a box with a name. Its kind decides how it is drawn, `tech` and
`description` add detail, `shape` picks an outline, tags group it for styling, `style` changes
its look and `prop` stores anything else you want to keep with it:

```
model {
  db = container "Orders DB" tech "Postgres" shape cylinder tag "pii" prop "owner" "Ann"
}
```

You can write all of that in the text, but you do not have to. Select one element on the canvas
and its properties appear in a panel beside it: the inspector. Select one relationship (click its
line) and the inspector shows the relationship instead; see [Relationships](#relationships).

## The inspector

The properties panel stays beside the canvas whatever is selected, so selecting never moves the
diagram. It shows the inspector when exactly one element or one relationship is selected. With
nothing selected it shows the document's [theme and styles](styles-and-themes.md#theme-and-styles-of-the-document),
and with several items selected it says how many. For an element, the inspector's title is the
element's id.

| Field         | Writes                        | How to fill it in                                                                    |
| ------------- | ----------------------------- | ------------------------------------------------------------------------------------ |
| Id            | the identifier before `=`     | a name such as `orders`; every reference to the element follows (see below)          |
| Kind          | the kind after `=`            | `container`, `database`, `person`, … or a library's `alias.kind`                     |
| Name          | the label in quotes           | any text; left empty, the name is the element's identifier                           |
| Tech          | `tech "…"`                    | any text                                                                             |
| Shape         | `shape …`                     | a base shape such as `cylinder` or `hexagon`, or a library's shape                   |
| Tags          | one `tag "…"` per tag         | comma-separated: `external, legacy`                                                  |
| Description   | `description "…"`             | any text                                                                             |
| Style         | `style "…"`                   | `key=value` pairs separated by `;`: `fill=#dae8fc;stroke=#6c8ebf`                    |
| Properties    | one `prop "…" "…"` per key    | one `key=value` per line                                                             |
| Shared styles | the `style` blocks that apply | one `selector: key=value;…` per line (see [styles and themes](styles-and-themes.md)) |

In the web app, Kind and Shape suggest the built-in names as you type. Style takes the same keys as inline
`style "…"` in the text; the [DSL reference](../dsl.md#freeform-diagrams) lists them.

### Controls in the macOS, web and Linux apps

The apps edit most fields with controls instead of text; each control writes the same text the
field above describes.

- **Kind, Shape and Tech** are lists to pick from that still take a new value typed in. Kind lists
  the built-in kinds and the kinds of the document's libraries, Shape the base shapes and the
  libraries' shapes (empty means the kind's shape), Tech the technologies the document already uses.
- **Tags** is a token field: type a tag and a comma, and it suggests the tags the document already
  uses.
- **Description** grows with its text; Return starts a new line, and leaving it applies it.
- **Style** is a visual editor. Elements get colour buttons for the fill, line and text, line width,
  line style (solid, dashed, dotted, …), rounded corners, opacity, font size, bold, italic and underline, and the label's alignment
  and position. Relationships get the line's colour, width, line style (solid, dashed, dotted,
  dash and dot, …), a double line, right-angle routing, curves and rounded corners, the start and
  end (arrows, diamonds, circles, bars, crow's foot ends, each shown with a preview) and whether
  they are filled, and the label's colour and size. Each control
  writes one key and leaves the others as they are; setting a control back to its default (or the
  ⓧ beside a colour) removes its key. Keys without a control are kept in **Other**, as
  `key=value;…`, and **Clear style** removes the whole style.
- **Colour buttons** open a palette of preset colours: greys from white to black, then light,
  bright and dark shades of ten hues. Picking the same swatches keeps colours consistent across
  elements and diagrams. **Custom…** opens the system colour picker for any other colour, and
  **Default** removes the key. Every app (macOS, Linux, web) offers the same swatches.
- **Properties** is a table of keys and values: edit a cell, add a row with +, or select one and
  remove it with −.

The web app differs in a few details: Tags are chips with × to remove one (Enter or a comma adds
the tag typed, Backspace in the empty field removes the last), Style can be folded with the
triangle beside its title, showing the style text instead, each property row has its own −, and a
relationship's Arrow is three buttons that draw the line. A relationship's Name field shows the
id DiaDraw derives from its ends, greyed, until you type a name.

The Linux (GTK) app has the same controls, with a few differences in how they look: Kind, Shape
and Tech are entries with a ▾ button that lists the choices, grouped by library. Tags are chips
with × to remove one, and an entry below them that adds a tag on Enter or a comma (its ▾ lists the
document's tags). Each property row has its own −. **Custom…** in a colour button's palette opens
GNOME's colour chooser.

### Applying a change

A field is applied when you press Enter or move to another field after changing it. Escape puts
the field back as it was. Clearing Tech, Shape, Description or Style removes that property;
clearing Tags or Properties removes them all.

In the description field, Enter starts a new line: leave the field to apply it (or press ⌘Enter /
Ctrl+Enter in the web app). In the properties table, Enter applies the cell.

The canvas draws the description under the name and technology, in smaller type, and makes the
box taller to fit it (and wider for long lines, up to 240 px; a diamond or an ellipse up to 320 px). Longer lines wrap; line breaks
and blank lines are kept. Circles, labels drawn outside the shape (actors, `labelPosition`,
`verticalLabelPosition`), drawings with their label inside or a `style` of their own, and
elements drawn as boundaries around their children show the name only.

A description that spans lines is written back as a block string, one line of text per line,
indented under the element:

```
model {
  db = container "Orders DB" tech "Postgres" description """
    Orders and their lines.

    Archived after two years.
  """
}
```

You can type it that way in the text too; `"""` opens and closes it, and the indentation the
lines share is not part of the description (see the [DSL reference](../dsl.md#lexical-rules)).

Each change is one undo step, shared with the text editor: ⌘Z on either side takes it back.

## Relationships

A relationship's inspector is titled with its line as you would write it, such as
`web -> db` or `checkout = web -> db`, and has these fields:

| Field      | Writes                     | How to fill it in                                               |
| ---------- | -------------------------- | --------------------------------------------------------------- |
| Label      | the label in quotes        | any text; left empty, the line has no label                     |
| Tech       | `tech "…"`                 | any text                                                        |
| Arrow      | `->`, `-->` or `<->`       | pick one: solid, dashed, or arrowheads at both ends             |
| Name       | `name =` before the line   | an identifier such as `checkout`; left empty, the id is derived |
| Tags       | one `tag "…"` per tag      | comma-separated: `async, legacy`                                |
| Style      | `style "…"`                | `key=value` pairs separated by `;`: `stroke=#c00;dashed=1`      |
| Properties | one `prop "…" "…"` per key | one `key=value` per line                                        |

Picking an arrow applies it at once; the other fields apply as for elements. The ends of a line are
not in the inspector: to reconnect it, edit the text.

### Naming a relationship from the inspector

The Name field is the relationship's id when you gave it one (see
[Named relationships](relationships.md#named-relationships)), and empty when the id is derived from
the ends. Typing a name writes `name = ` in front of the line; clearing it deletes that again. The
line stays selected under its new id, and every `route "…"` that pointed at the old id is changed to
point at the new one, so hand-made bend points are kept. Naming the line in

```
model {
  …
  web -> db "writes"
}

view main {
  route "web->db" via 200, 80
}
```

`calls` changes two places:

```
  calls = web -> db "writes"
  …
  route "calls" via 200, 80
```

Naming or unnaming a line can change the derived ids of other lines between the same two elements,
since `#2`, `#3` count the unnamed lines before them. Their routes are updated too, so nothing that
was routed by hand loses its route. A name that is not an identifier, or that another relationship
already has, is refused and the status line says why.

## What happens to your text

The text stays the source of truth, so every change in the inspector becomes a change in the
file, and the inspector writes as little as it can:

- a changed value is replaced where you wrote it, so `shape rect tech "React"` stays in that
  order when you change the technology, and a new arrow replaces just the arrow;
- a cleared property is deleted together with the space before it;
- a new property is added after the last one on the line, before any `{ … }` body or comment (a
  relationship's new label goes right after its target, where labels are written);
- tags you keep stay where they are, and new tags go at the end.

Comments, alignment, the element's body and everything else in the file are left alone. If you
change a technology from the inspector:

```
    web = container "Web app" shape rect tech "React" tag "edge"  // storefront
```

becomes

```
    web = container "Web app" shape rect tech "Vue" tag "edge"  // storefront
```

## Things to know

- **One item at a time.** Select a single element or relationship to edit it; with several
  selected, the panel only says how many are selected. Double-clicking a line still edits its
  label in place.
- **Renaming an element** changes its identifier and every reference to it: relationships,
  views, `pin`s, `route`s, notes and sequences, written where they are, so comments and layout
  stay. Its children move with it, and an element without a label keeps showing its id. Use the
  Id field, or **Edit ▸ Rename Element…** (F2) on the selected element or, in the text, on the
  name under the caret; in VS Code, Rename Symbol (F2) does the same. A name another element
  already has, or one that would make a reference point somewhere else, is refused and the status
  line says why. Moving an element into another parent or adding ports is still done in the text.
- **Relationships ignore `shape` and `description`.** The inspector does not offer them, and the
  text reports them with a warning.
- **Kinds and shapes are names.** A kind or shape with spaces or other punctuation is refused,
  and the status line says why; nothing is written.
- **Style keys** cannot contain `=` or `;`, and values cannot contain `;`, as in the text.
- **A value that would be refused is outlined in red** while you type it, and the field's tooltip
  says why: an Id that is not a name or is taken, a Kind or Shape that is not a name, a Style key
  with `=` or `;`, a Shared styles line that cannot be read or does not apply to the element, and
  a relationship's Name that is not a name or is taken. The outline goes once the value is valid.
- **A whole kind's look** belongs in a `style` block rather than in each element's Style field:
  the inspector's Shared styles field edits the blocks that apply to the element (see
  [Styles and themes on the canvas](styles-and-themes.md)).
- **Where it is available:** the web app, the macOS app and the Linux (GTK) app. The Windows
  app does not have the inspector yet.
