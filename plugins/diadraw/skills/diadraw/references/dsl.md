# DiaDraw DSL cheat sheet

A summary of `docs/dsl.md` in the DiaDraw repository, which is the full reference.

## File structure

```
use "c4@^1" as c4                  // optional: shape libraries
theme dark                          // optional: default | dark | mono | your own
style container { fill "#438dd5" }  // optional: shared looks
model { … }                         // elements and relationships, declared once
view name [of parent] { … }         // one or more views of the model
```

Comments are `//` and `/* … */`. Whitespace is free; statements need no separators.

## Elements

```
name = kind "Label" [tech "…"] [shape …] [tag "…"] [description "…"] [style "k=v;…"] [prop "k" "v"]… [{ children }]
```

- Built-in kinds: `person system container component database process decision start end note
class object event task subprocess gateway data annotation group pool lane conversation`.
  Other kinds are accepted with a warning.
- Without a label the name is the identifier.
- `tag` is repeatable and drives `style tag "…"` blocks; `external` gets a grey look.
- Children go in `{ … }` and draw inside their parent as a boundary.
- `group "Label" { … }` inside the model draws a dotted frame around its members without adding
  a level to their names.

Base shapes for `shape`: `rect rounded ellipse circle rhombus triangle hexagon octagon cylinder
parallelogram trapezoid document cloud actor person note card step arrow process stadium cube
swimlane folder queue interface text image invisible bar frame module lifeline`.

## Relationships

```
[name =] from -> to ["Label"] [tech "…"] [tag "…"] [style "k=v;…"] [prop "k" "v"]…
```

| Arrow | Drawn as                  |
| ----- | ------------------------- |
| `->`  | solid line, one arrowhead |
| `-->` | dashed line               |
| `<->` | arrowheads at both ends   |

Ends are `a`, `parent.child` or a port `a.http` (declared as `port http east` in `a`). The id is
`a->b` (`a->b#2` for a repeat) unless named.

## Views

```
view name [of parent] {
  include * | a b c …              // default: include *
  exclude a …
  layout layered|tree|grid|radial|force|fixed|sequence [direction down|right|up|left] [spacing N]
  pin a below|above|left of|right of b
  pin a at X, Y [size W, H]        // fixed views
  align a b … [horizontally|vertically]
  order a b c …
  size a W, H
  rotate a DEG
  route "rel-id" via X, Y … [from EX, EY] [to NX, NY]   // fixed views
  theme dark
  note [name] "text" [below|above|left of|right of el | at X, Y [size W, H]] [style "…"]
  text [name] "text" [anchor] [style "…"]
  box [name] ["label"] around a b … [style "…"]
  sequence { … }                    // with layout sequence
}
```

### Picking a layout

| Algorithm  | Good for                                                       |
| ---------- | -------------------------------------------------------------- |
| `layered`  | default; architecture, flowcharts, anything with a flow        |
| `tree`     | org charts, hierarchies, decision trees (`a -> b`: b under a)  |
| `grid`     | catalogs and inventories with few lines, in `include` order    |
| `radial`   | hub and spoke: one central element and its neighbours          |
| `force`    | networks with no main direction                                |
| `fixed`    | hand placement with `pin … at` and `route`; BPMN-style layouts |
| `sequence` | interactions over time, with a `sequence { … }` block          |

`direction right` suits wide pipelines and C4 containers; `down` suits flowcharts and trees.

## Sequence block

```
sequence {
  autonumber
  a -> b "call"          // ->  call, ->> async, --> reply
  activate b
  b --> a "reply"
  deactivate b
  alt "ok" { … } else "error" { … }   // also opt loop par critical break group
  note over a b "text"   // or: note left of a "text", note right of a "text"
  divider "Later"
  delay "5 minutes"
  ref over a b "Logout"
}
```

Participants are the view's `include` list, in that order.

## Styles

Inline: `style "fill=#dae8fc;stroke=#6c8ebf;rounded=1"`. Element keys: `fill stroke strokeWidth
dashed dashPattern rounded opacity fontColor fontSize fontStyle` (bold 1, italic 2, underline 4,
strike-through 8) `lineFontStyles fontRuns align
verticalAlign`. Relationship keys: `stroke strokeWidth dashed dashPattern double curved rounded
startArrow endArrow startFill endFill`. Line ends: `classic block open async diamond oval box
cross none ERone ERmandOne ERzeroToOne ERmany ERoneToMany ERzeroToMany` and more; `endFill=0`
makes an end hollow.

Blocks (keep the theme's look, change only what they set):

```
style * { fontSize 13 }
style container { fill "#438dd5" stroke "#3c7fc0" }
style tag "legacy" { fill "#999999" dashed true }
style container tag "legacy" { … }
style relationship { endArrow open }
style relationship tag "async" { dashed true endArrow open }
```

Own theme: `theme brand extends dark { background "#101418" node person { fill "#08427b" } }`
then `theme brand`.

## Libraries

`use "c4@^1" as c4` then `shape c4.database`; `use "bpmn@^1" as bpmn` then
`shape bpmn.user_task`. Built in: `c4`, `bpmn`, the PlantUML icon sets (`plantuml-aws`,
`plantuml-azure`, `plantuml-gcp`, `plantuml-kubernetes`, …) and draw.io's stencil sets
(`drawio-<set>`). See `docs/libraries.md`.
