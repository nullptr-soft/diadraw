# Line ends and line styles

A relationship's arrow (`->`, `-->`, `<->`) gives the usual look: a solid or dashed line with a
filled arrowhead. When a diagram follows a notation, such as UML, an entity-relationship model
or a data flow, the ends and the line itself carry meaning. Give the relationship a style to pick
them, on one line or for every line with a tag.

## Picking them in the app

Select a line. The **Style** section of the properties panel has a control for each:

- **Start** and **End** pick the ends. On macOS and Linux each entry shows a small picture of
  the line. On macOS and the web the entries are grouped into Arrows, Shapes, Lines and Crow's
  foot.
- **Filled start** and **Filled end** draw a triangle, half arrow, diamond, circle or square
  hollow when unticked.
- **Line style** picks how the line is drawn: solid, dashed, dotted, short or long dashes, dash
  and dot, or dash and two dots. On macOS and Linux each entry shows a small picture of the line.
- **Double line** draws two thin lines instead of one; **Line width** makes the line thicker.
- **Routing** draws the line at right angles, **Curved** bends it smoothly through its bend
  points and **Rounded corners** rounds off its corners.

Each control writes one key into the relationship's `style "…"` and leaves the rest of your text
as it was. Setting a control back to its default removes its key again.

## Writing them in the text

The same keys go in `style "key=value;…"` after the relationship:

```
model {
  customer = process "Customer"
  order = process "Order"
  customer -> order "places" style "startArrow=ERmandOne;endArrow=ERzeroToMany"
  order -> customer "notifies" style "dashed=1;dashPattern=1 3;endArrow=open;curved=1"
}
```

To style every line with a tag, use a style block instead (see
[Styles and themes](styles-and-themes.md)):

```
style relationship tag "async" { dashed true dashPattern "1 3" endArrow open }
```

### Ends

`startArrow` and `endArrow` take these values. They are draw.io's names, so a diagram imported
from draw.io keeps its ends.

| Value                                   | Drawn as                    | Typical use                                    |
| --------------------------------------- | --------------------------- | ---------------------------------------------- |
| `classic`                               | arrow                       | a call or a dependency (the default end)       |
| `block`                                 | triangle                    | UML inheritance (hollow, `endFill=0`)          |
| `open`                                  | open arrow                  | UML dependency, a message                      |
| `async`, `openAsync`                    | half arrow, open half arrow | an asynchronous message                        |
| `doubleBlock`                           | double triangle             | a fast or prioritised flow                     |
| `diamond`                               | diamond                     | UML composition (filled), aggregation (hollow) |
| `oval`                                  | circle                      | a connection point                             |
| `box`                                   | square                      | a port or a qualifier                          |
| `circlePlus`                            | circle with a plus          | UML containment                                |
| `halfCircle`                            | socket                      | a required interface                           |
| `baseDash`, `dash`, `cross`             | bar, slash, cross           | a stop, a break, a forbidden flow              |
| `ERone`, `ERmandOne`, `ERzeroToOne`     | crow's foot: one            | one, exactly one, zero or one                  |
| `ERmany`, `ERoneToMany`, `ERzeroToMany` | crow's foot: many           | many, one or many, zero or many                |
| `none`                                  | nothing                     | an undirected line                             |

Without `startArrow`, a line has no start end unless it is written with `<->`. Without
`endArrow`, it ends in an arrow. `startFill=0` and `endFill=0` make the shapes hollow.

### The line

| Key                             | Effect                                                                     |
| ------------------------------- | -------------------------------------------------------------------------- |
| `dashed=1`                      | dashes the line (as `-->` does)                                            |
| `dashPattern=…`                 | the dashes: `1 3` dotted, `3 3` short, `12 6` long, `8 3 1 3` dash and dot |
| `double=1`                      | two lines with the background between them                                 |
| `strokeWidth=…`                 | the width, 1.25 by default                                                 |
| `stroke=#…`                     | the colour                                                                 |
| `edgeStyle=orthogonalEdgeStyle` | right-angle routing                                                        |
| `curved=1`                      | smooth curves through the bend points                                      |
| `rounded=1`                     | rounded corners                                                            |

A `dashPattern` only shows on a dashed line; the app's Line style control sets both.

## Things to know

- A straight line has no corners, so `curved` and `rounded` change nothing on it. They show
  once the line bends: in a layout that routes it around boxes, at right angles, or along the
  bend points of a `route`.
- The gap in a double line is painted in the page's background colour, so where a double line
  crosses a filled group or container the gap shows the page colour.
- On export to draw.io, the ends, `dashPattern`, `curved` and `rounded` go with the line;
  `double` does not, as draw.io draws double lines as a different kind of edge.
- When importing from draw.io, the ends come over, but an edge's curves, rounded corners and
  dash pattern do not yet, so imported pages look as they did before this was added.
