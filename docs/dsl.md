# DiaDraw DSL

One grammar for every diagram type: a Structurizr-style `model` block that defines things once,
and `view` blocks that pick, arrange and constrain them. Files use the `.draw` extension and are
the source of truth, so diagrams diff and review cleanly in git.

The grammar is specified in [`diadraw.grammar`](diadraw.grammar), written in Lezer notation as a
reference; nothing compiles it. The core's parser (`src/dsl/src/syntax.cpp`) is a hand-written,
error-tolerant recursive-descent parser that builds the tree that grammar describes, with the
same node names. `parse(source)` (`src/dsl/src/lower.cpp`) lowers the syntax tree into model
operations and returns the model, diagnostics with line and column, and a source map.
`print(model)` writes canonical text; every corpus file survives print → parse unchanged.

This is the reference. For what the features are for and when to use them, see the
[user guide](guide/README.md).

## Example

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

view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web    // written by a canvas drag
}
```

## Freeform diagrams

Diagrams drawn by hand (or imported from draw.io) keep their geometry in the text:

```
model {
  a = process "Billing" style "fill=#dae8fc;stroke=#6c8ebf;rounded=1" prop "owner" "Ann"
  b = decision "Paid?" shape rhombus
  a -> b "check" style "endArrow=open;dashed=1;edgeStyle=orthogonalEdgeStyle"
}

view page1 {
  include *
  layout fixed
  pin a at 40, 40 size 120, 60
  pin b at 240, 30 size 100, 80
  route "a->b" via 200, 70 200, 70 from 1, 0.5 to 0, 0.5
}
```

- `style "k=v;k=v"` on an element or relationship sets its look. Element keys: `fill`, `stroke`,
  `strokeWidth`, `dashed`, `dashPattern` (with `dashed=1`), `rounded`, `opacity` (0–100), `fontColor`, `fontSize`, `fontStyle`
  (bold 1, italic 2, underline 4, strike-through 8), `lineFontStyles` (one `fontStyle` per line of
  the name, such as `0,1` for a plain first line and a bold second), `fontRuns` (runs of the
  name's code points, line breaks included, each `count:fontStyle`, such as `5:0,4:8,5:0` for
  "Text " plain, "Text" struck through and " Text" plain; ignored when the counts do not add up to
  the name, as after an edit; while they apply, `fontStyle` and `lineFontStyles` do not), `align`, `verticalAlign`, `labelPosition`,
  `verticalLabelPosition`, `spacingTop`, `spacingLeft`, `spacingRight`, `spacingBottom` (room kept
  clear around the label), `boundedLbl=1` (the label stays off a cube's depth, a note's fold and a
  folder's tab), and draw.io's shape keys: `size` (a cube's depth, a note's folded corner),
  `direction` of a cube (`east` puts the depth at the top left, `south` top right, `west` bottom
  right, `north` bottom left), `tabWidth`, `tabHeight` and `tabPosition` (`left` or `right`) of a
  folder's square tab, and `jettyWidth` and `jettyHeight` of a module's two jetties. A
  `lifeline` draws its name in a head `size` deep (40 by default) and a dashed line down its
  middle; `participant=umlActor` makes the head a stick figure and
  `lifelineDashed=0` the line solid.
  `anchor=X,Y` makes an element in a container (and in no `group`) an adornment of it, as draw.io's
  relative geometry does: in a `layout fixed` view it sits at that fraction of its parent's box
  (`1,0` is the top right corner), moved by `anchorOffset=DX,DY` pixels, and stays there when the
  parent is moved or resized. Its size comes from `size`; a `pin … at` does not move it. Dragging
  it changes its `anchorOffset`. An adornment is a leaf: it neither sizes its parent nor wraps
  children of its own. Relationship keys: `stroke`, `strokeWidth`, `dashed`, `dashPattern`
  (with `dashed=1`: `1 3` dotted, `8 3 1 3` dash and dot, …), `double` (two lines), `curved`,
  `rounded` (rounded corners), `edgeStyle`, `startArrow`, `endArrow`, `startFill`, `endFill`, and
  where a `layout fixed` view puts the label, as draw.io does: `labelAt=ALONG[,AWAY]` (`-1` the
  start, `0` halfway along the line, `1` its end; `AWAY` pixels off it), `labelOffset=DX,DY`, and
  `align` (`left` puts the label to the right of that point) and `verticalAlign` (`bottom` puts it
  above). These four are read from the relationship's own `style`, not from style blocks. The
  ends take draw.io's names: `classic`, `block`, `open`, `async`, `openAsync`, `doubleBlock`,
  `diamond`, `oval`, `box`, `circlePlus`, `halfCircle`, `baseDash`, `dash`, `cross`, `none`, and the
  crow's foot ends `ERone`, `ERmandOne`, `ERzeroToOne`, `ERmany`, `ERoneToMany`, `ERzeroToMany`;
  `startFill=0`/`endFill=0` makes a shape hollow. A styled element is drawn the draw.io way (white fill, black stroke by default) and
  a styled container is drawn as its own shape instead of a dashed boundary. Unknown keys are kept.
- `prop "key" "value"` stores any property (repeatable).
- `shape` takes a base shape: `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible frame module lifeline`.
- `layout fixed` keeps every `pin … at X, Y size W, H` exactly (unpinned nodes line up below);
  `route "relationship id" via X, Y … from EX, EY to NX, NY` fixes an edge's bend points and where
  it leaves its source and enters its target (relative to the box, 0–1). Without `from`/`to` the
  edge leaves towards its first bend point. On the canvas of a fixed view, moving or resizing a node
  rewrites its `pin … at … size`, and dragging bend points rewrites its `route` (see
  [Drawing by hand in fixed views](guide/fixed-views.md)).

## Styles and themes

`theme` picks the colours a document is drawn with, and `style` blocks give every element of a
kind, every item with a tag, or every relationship a shared look:

```
theme dark

style container {
  fill "#438dd5"
  stroke "#3c7fc0"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true endArrow open }

model { … }

view checkout of shop {
  include *
  theme default        // this view only
}
```

- Themes: `default`, `dark` (dark background) and `mono` (black on white, grey fills, for print).
  A view's `theme` overrides the document's; an unknown name is a warning and draws with
  `default`. Without a `theme` statement every view uses `default`, exactly as before.
- Selectors: `*` (every element), a kind (`container`), `tag "…"`, a kind and a tag
  (`container tag "legacy"`), `relationship`, and `relationship tag "…"`. In a selector,
  `relationship` and `tag` are keywords, so there is no selector for a kind of either name.
- Each line in a block is `key value`. Keys are the ones inline `style "…"` takes (listed above,
  unknown keys are kept). A value is a string, a number or a word; `true` and `false` are stored
  as `1` and `0`, as draw.io writes them.
- Precedence, lowest first: the theme, `*` blocks, kind blocks, tag blocks, kind-and-tag blocks
  (later blocks win within one step), then the item's own inline `style "…"`.
- A `style` block only changes what it sets: the element keeps the theme's other colours and its
  bold label. An inline `style "…"` still switches an item to draw.io's look (white fill, black
  stroke) for what it leaves out. Blocks for a kind that has children restyle its boundary
  (`fill`, `stroke`, `strokeWidth`, `dashed`, `fontColor`).
- `style` and `theme` are top-level statements; `theme` may also appear in a view. Inside
  `model { … }` they stay ordinary names, and `style "…"` on an element is the inline style.
- `print` writes theme declarations first, then the chosen theme, then the blocks in order, one
  `key value` per line.

### Your own themes

A document declares a theme with a body, and picks it like a built-in one:

```
theme brand extends dark {
  background "#101418"
  edge "#9aa5b1"
  fontFamily "IBM Plex Sans, sans-serif"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
  node external { fill "#444444" }
}

theme brand
```

- `extends` names a built-in, another declared theme or a library theme; without it the theme
  starts from `default`. Each theme changes only what it sets.
- Theme keys: `background`, `fontFamily`, `fontSize`, `smallFontSize`, `edge` and `edgeText`
  (relationship lines and labels), `boundary` and `boundaryText` (container and group frames),
  `surface` (activations, hollow arrowheads), `dividerFill`, `labelText` (labels under icons).
- `node <kind> { fill … stroke … text … }` colours a kind; `node default` is every kind without
  its own entry, `node external` is elements tagged `external`.
- Libraries ship themes too (see [libraries](libraries.md#the-manifest)): with
  `use "acme-brand@^1" as acme`, `theme acme.corporate` picks one and
  `theme mine extends acme.corporate { … }` builds on it.
- Unknown keys, unknown `extends` targets, duplicate names, built-in names and `extends` cycles
  are reported; a theme that cannot be resolved draws as `default`.

## Sequence views

A view with `layout sequence` draws its included elements as lifelines, in `include` order, and
the steps of its `sequence` block from top to bottom:

```
view login {
  include user web api
  layout sequence
  sequence {
    autonumber
    user -> web "Log in"          // ->  call, ->> async, --> reply
    activate web
    alt "valid" {
      web --> user "Welcome"
      note over web api "token issued"
    } else "invalid" {
      web --> user "Error"
      note right of web "logged"
    }
    deactivate web
    divider "Later"
    delay "5 minutes"
    ref over user web "Logout"
    loop "every 30 s" { web ->> api "heartbeat" }
  }
}
```

Frames are `alt`, `opt`, `loop`, `par`, `critical`, `break` and `group`, each with optional
`else "label" { … }` sections. Messages live in the view, not in the model: the same participants
can appear in many sequence views, and the model's relationships stay a summary.

## Notes and decorations

Notes, free text and boxes belong to one view, not to the model: they annotate a diagram without
adding elements to it, so other views of the same model stay clean.

```
view checkout of shop {
  include *
  layout layered direction right
  text "Checkout, container level" style "fontSize=18;fontStyle=1"
  note consistency "Orders are eventually consistent" below db
  note "Owned by team Web" left of web
  box pci "PCI scope" around api db
  note "Check the cache TTL"
  note "Draft" at 40, -80 size 160, 60
}
```

| Item                                                 | Placed                                                                          |
| ---------------------------------------------------- | ------------------------------------------------------------------------------- |
| `note [name] "text" [anchor] [style "…"]`            | a sticky note; attached to an element it gets a dashed connector                |
| `text [name] "text" [anchor] [style "…"]`            | free text, a title or caption                                                   |
| `box [name] ["label"] around a b … [style "…"]`      | a dashed, rounded frame around the boxes of the elements, label at the top left |
| `box [name] ["label"] at X, Y size W, H [style "…"]` | a frame at fixed coordinates                                                    |

An anchor is `below`, `above`, `left of` or `right of` an element of the view, or `at X, Y` with an
optional `size W, H`. Without one, text stacks above the diagram and notes stack in a column on its
right. Decorations are placed after the layout, so they never move elements: one that would cover
a node, an edge label or another note steps further out from its element. A note wraps at 200 px.

- The name is optional and must be unique in the view; it lets edits and hosts address the item
  (the SVG carries it as `data-name`, and every decoration has `data-decoration="<index>"` and
  the class `dd-decoration dd-note|dd-text|dd-box`).
- `style` takes the element keys: `fill`, `stroke`, `strokeWidth`, `dashed` (boxes are dashed
  unless `dashed=0`), `dashPattern`, `rounded`, `opacity`, `fontColor`, `fontSize`, `fontStyle`, `align`,
  `verticalAlign`.
- Removing an element removes the decorations attached to it or framing it.
- `note`, `text`, `box` and `around` are contextual keywords: elements can still have those names.
  Inside `sequence { … }`, `note over …` / `note left of …` keep their sequence meaning.

## Lexical rules

- Whitespace is insignificant; statements need no separators.
- Comments: `// to end of line` and `/* block */`. Both are kept in the syntax tree.
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`. Nested elements are referenced with dots: `shop.web`.
- Strings: double-quoted, with `\` escapes (`\n` is a line break), on one line. A **block
  string** spans lines: it opens with `"""` and ends at the next `"""`, with the same escapes.
  The rest of the line after the opening `"""` and the line of the closing one are dropped when
  they are blank, and so is the indentation the other lines share, so the text can be indented
  with the statement around it. Blank lines inside are kept, as empty lines:

  ```
  db = container "Orders DB" description """
    Orders and their lines.

    Archived after two years.
  """
  ```

  Block strings work wherever a string does. `format` prints a `description` that spans lines
  as a block string, and every other string with `\n`.

- Numbers: `-?digits(.digits)?`.
- Keywords are contextual: `use as model view of group port north south east west tech shape tag
description style prop include exclude layout direction spacing right left down up pin below
above at size route via from to align horizontally vertically order sequence activate
deactivate note over divider delay ref autonumber alt opt loop par critical break else theme relationship extends node
text box around` are
  keywords where the grammar expects one, and ordinary names elsewhere
  (`layout = container "Layout"` is fine). Where both readings fit, as in a selector list
  followed by `layout …`, the keyword reading wins.

## Grammar (informal)

```
Document     := (Use | ThemeDef | Theme | StyleBlock | ModelBlock | ViewBlock)*

Use          := "use" String ("as" Name)?          // see docs/libraries.md

Theme        := "theme" QName
ThemeDef     := "theme" Name ("extends" QName)? "{" (Name Value | "node" Name "{" (Name Value)* "}")* "}"
StyleBlock   := "style" StyleSelector "{" (Name (String | Number | Ident))* "}"
StyleSelector:= ("*" | "relationship" | Kind) ("tag" String)? | "tag" String

ModelBlock   := "model" ModelBody
ModelBody    := "{" ModelItem* "}"
ModelItem    := ElementDecl | Relationship | Group | Port

ElementDecl  := Name "=" Kind Label? Property* ModelBody?
Kind         := Ident | Alias "." Ident               // built in, or a library's kind
Relationship := (Name "=")? QName Arrow QName Label? Property*
Arrow        := "->" (solid) | "-->" (dashed) | "<->" (bidirectional)
Group        := "group" Label ModelBody
Port         := "port" Name ("north" | "south" | "east" | "west")?
Property     := "tech" String
              | "shape" QName                      // a base shape, or alias.shape from a library
              | "tag" String
              | "description" String
              | "style" String                     // "key=value;key=value"
              | "prop" String String               // repeatable

ViewBlock    := "view" Name ("of" QName)? "{" ViewItem* "}"
ViewItem     := "include" Selector+
              | "exclude" Selector+
              | "layout" Algorithm ("direction" Dir | "spacing" Number)*
              | "pin" QName (Placement QName | "at" Coord ("size" Coord)?)
              | "route" String ("via" Coord+)? ("from" Coord)? ("to" Coord)?
              | "size" QName Coord                 // width, height in this view
              | "rotate" QName Number              // degrees, clockwise
              | "align" QName QName+ ("horizontally" | "vertically")?
              | "order" QName QName+
              | "sequence" SeqBody
              | "theme" QName
              | ("note" | "text") Name? String Anchor? ("style" String)?
              | "box" Name? String? ("around" QName+ | "at" Coord "size" Coord)
                ("style" String)?
Selector     := "*" | QName
Algorithm    := Name                               // layered (default), tree, grid, radial, force, fixed, sequence
Dir          := "right" | "left" | "down" | "up"
Placement    := "below" | "above" | "left" "of" | "right" "of"
Coord        := Number "," Number
Anchor       := Placement QName | "at" Coord ("size" Coord)?

SeqBody      := "{" SeqItem* "}"
SeqItem      := QName MessageArrow QName Label?
              | ("activate" | "deactivate") QName
              | "note" ("over" QName+ | ("left" | "right") "of" QName) Label
              | "divider" Label?
              | "delay" Label?
              | "ref" "over" QName+ Label
              | "autonumber"
              | FrameKind Label? SeqBody ("else" Label? SeqBody)*
MessageArrow := "->" (call) | "->>" (async) | "-->" (reply)
FrameKind    := "alt" | "opt" | "loop" | "par" | "critical" | "break" | "group"

Label        := String
Kind         := Name
QName        := Name ("." Name)*
```

## Semantics

| Syntax                                | Model                                                               |
| ------------------------------------- | ------------------------------------------------------------------- |
| `web = container "Web app"` in `shop` | element `shop.web`, kind `container`, name `Web app`, parent `shop` |
| no label                              | the name defaults to the identifier                                 |
| `tech`, `shape`, `tag`, `description` | element / relationship fields of the same name                      |
| `style "k=v;…"`, `prop "k" "v"`       | the element's or relationship's style and properties                |
| `a -> b "label"`                      | relationship with id `a->b` (`a->b#2`, `#3`, … for repeats)         |
| `pay = a -> b`                        | relationship with the explicit id `pay`                             |
| `a.http -> b`                         | if `a` has `port http`, the endpoint is that port                   |
| `group "Build" { … }`                 | a group; elements inside get `group` set                            |
| `view v of shop`                      | view scoped to the children of `shop`                               |
| `include *`                           | every element in scope; a view without `include` includes `*`       |
| `layout …`                            | the view's algorithm, direction and spacing (default `layered`)     |
| `pin a below b`                       | `PinHint` with placement `Below`                                    |
| `pin a at 10, 20 size 120, 60`        | `PinAtHint` with position and optional size (freeform opt-in)       |
| `route "a->b" via 1, 2 from … to …`   | `RouteHint`: bend points, exit and entry (0–1 of the box)           |
| `size a 200, 80`                      | `SizeHint`: the element's size in this view, kept by every layout   |
| `rotate a 30`                         | `RotateHint`: turned 30° clockwise about its centre (leaves only)   |
| `align a b horizontally`              | `AlignHint` (axis defaults to horizontal)                           |
| `order a b c`                         | `OrderHint`                                                         |
| `sequence { … }`                      | the view's sequence steps; `autonumber` anywhere turns numbering on |
| `theme dark`                          | `model.theme` (in a view: `view.theme`)                             |
| `theme brand extends dark { … }`      | `model.themes[]`: `{ name, extends?, …keys, nodes? }`               |
| `style container { fill "#438dd5" }`  | `model.styles[]`: `{ target: 'elements', kind, tag?, style }`       |
| `note n "x" right of a`               | decoration `{ kind: 'note', name: 'n', placement: 'right-of' }`     |
| `box "x" around a b`                  | decoration `{ kind: 'box', around: [a, b] }`                        |

Element kinds are open identifiers. Built in: `person`, `system`, `container`, `component`,
`database`, `process`, `decision`, `start`, `end`, `note`, `class`, `object`, `event`, `task`,
`subprocess`, `gateway`, `data`, `annotation`, `group`, `pool`, `lane` and `conversation`. Any
other kind is accepted with a warning; libraries add more (see below). A `person` is drawn as a
stick figure with its name underneath in the theme's `labelText` colour, since the name sits on
the canvas (a style's `fontColor` overrides it, and an inline `style` keeps draw.io's black); its fill colours the head, and the theme's
`node person { text }` colours the name only when a `shape` draws the person as a filled box.

The layout algorithm is one of `layered` (the default), `tree`, `grid`, `radial`, `force`,
`fixed` and `sequence` (see the [user guide](guide/layouts.md) for what each does). Any other name
is kept but runs the layered engine, with a warning on the `layout` line.

## Element kinds from libraries

A library can add element kinds (its manifest's `kinds`, see [libraries.md](libraries.md#element-kinds)).
A document names them through the library's alias, as it names its shapes:

```
use "acme-platform@^1" as acme

model {
  orders = acme.service "Orders" tech "Go"
  events = acme.topic "Order events"
  jobs = acme.queue "Jobs" {
    worker = component "Worker"
  }
  orders -> events "publishes"
}
```

- An element of a library kind draws as the kind's shape (a library drawing or a base shape) in
  the kind's colours, unless it sets its own `shape` or `style`. With children it draws as a
  boundary labelled with the kind's stereotype (`Jobs [queue]`), as C4 kinds are.
- The kind is stored as written, `acme.topic`, so the model and the printed text keep it.
- A kind whose alias no `use` declares is a parse warning. A kind the library does not have is
  a warning from `diadraw check`, `diadraw render` and the editor, like an unknown shape, and the
  element draws as a box in the default colours. Either way the document still lowers.
- Library kinds are always qualified, so they never collide with built-in kinds or with each
  other.

## References

A relationship's id comes from its endpoints unless it is named. Derived ids follow the edge:
moving an endpoint changes the id, and deleting the first of two parallel edges turns `a->b#2`
into `a->b`, so a `route` that names it is lost when the text is edited by hand (reconnecting
on the canvas rewrites such routes to the new ids). Naming the edge, `pay = a -> b`, gives it the
id `pay` for good: it survives endpoint changes and reordering, and `route "pay" …` keeps
applying (see [Relationship ids](guide/relationships.md#relationship-ids) in the user guide for when
to name an edge). The name is used as is, wherever the relationship is declared (it is not prefixed with
the enclosing element's path), and must be unique among relationships; it may repeat an
element's name. Naming one edge does not shift the derived ids of the others. `print` writes an
id that is a name as `name = …`; ids that are not names (from JSON, say) cannot be written and
come back derived.

A dotted reference resolves from the innermost scope outwards: inside `shop { … }`, `web` means
`shop.web`, and `shop.web` works anywhere. View selectors and hints resolve against the view's
scope the same way, so `view checkout of shop { pin db below web }` is valid. A reference whose
last segment is a port of the element before it (`api.http`) targets that port.

## Diagnostics

Parsing never throws. The parser recovers from syntax errors on its own: a statement that does
not parse is marked, and input is skipped up to the end of its block or the next line that starts
a valid statement there, so the rest of the file still lowers. Skipped input is reported as
`Unexpected "…"` and a statement cut short as `Syntax error: incomplete statement`.

Model errors come from the core operations and from reference resolution, at the offending span:
unknown elements, unknown relationships in `route`, duplicate ids, ports outside an element or
declared twice, and bad `use` names or repeated aliases. Warnings: unknown element kinds, library
kinds whose alias no `use` declares, and `shape` or `description` on a relationship (ignored). Lines and columns are 1-based; columns
count UTF-16 code units, as editors do. `diadraw check file.draw` prints them as
`file:line:column: error|warning: message`.

## Round-trip rules

1. **Lossless parse.** The tree keeps comments and whitespace.
2. **Semantic edits become text patches.** A canvas add/rename/reconnect changes the smallest span
   of text at the right place. Reconnecting rewrites only the moved endpoint, written as the
   shortest reference that resolves to the new element from where the relationship is declared,
   plus every `route` whose relationship id changes (a named relationship keeps its id; derived
   ids are derived again, so parallel edges may renumber; see
   [the user guide](guide/relationships.md#reconnecting-a-line)). Editing an element's
   properties in the inspector (kind, name, `tech`, `shape`, `tag`, `description`, `style`,
   `prop`) rewrites just the clauses that changed:
   a changed value is replaced where it is written, a cleared one is deleted, and a new one is
   appended after the declaration's last clause, so comments, clause order and the element's
   body stay as they were (see [the user guide](guide/element-properties.md)). A relationship's
   label, `tech`, arrow, `tag`, `style` and `prop` are edited the same way; a new name is written
   as `name = ` before the line (or deleted with it), and every `route` naming an id that changes,
   including derived ids renumbered by it, is rewritten to the new id. Style blocks and themes
   edited from the canvas work the same way, entry by entry: a changed value is rewritten in
   place, a dropped key's entry deleted, a new key appended to its block, and new blocks and
   theme statements go next to their kind (see [the guide](guide/styles-and-themes.md)).
3. **Geometric edits become hints, not coordinates.** A drag writes `pin`, `align` or `order`.
4. **Freeform items still live in text.** Notes, text and boxes are view-level items (`note`,
   `text`, `box`; see [Notes and decorations](#notes-and-decorations)).
5. **Importers, not compatibility.** PlantUML, Mermaid, Structurizr and draw.io are imported into
   the model; their syntax is not parsed natively.

## Open points

Open work on the DSL and on editing it from the canvas is tracked in the
[backlog](backlog.md#dsl).
