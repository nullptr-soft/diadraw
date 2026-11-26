# Layouts

A view's `layout` line picks the algorithm that places its elements and draws its lines. You
never give coordinates for the automatic ones: you pick the algorithm that suits the picture,
point it in a direction, and nudge it with hints when it guesses wrong.

```
view checkout of shop {
  include *
  layout tree direction right spacing 40
}
```

`direction` (`down`, `right`, `up`, `left`) and `spacing` (pixels between boxes) are optional.
Without a `layout` line a view uses `layered`, flowing down.

## Choosing an algorithm

| Algorithm  | Places elements                                      | Lines           | Good for                                        |
| ---------- | ---------------------------------------------------- | --------------- | ----------------------------------------------- |
| `layered`  | in layers along the flow, with few crossings         | right angles    | architecture, flowcharts, anything with a flow  |
| `tree`     | as a tidy tree, parents centred over children        | org-chart style | org charts, hierarchies, decision trees, menus  |
| `grid`     | in rows or columns, in the order you list them       | right angles    | catalogs, inventories, "all our services" pages |
| `radial`   | on rings around the best-connected element           | straight        | hub and spoke, a system and its neighbours      |
| `force`    | where connected elements pull together, others repel | straight        | networks and maps with no main direction        |
| `fixed`    | exactly where `pin … at` puts them                   | as routed       | hand-drawn and imported diagrams                |
| `sequence` | as lifelines, with the steps of `sequence { … }`     | messages        | interactions over time (see the DSL reference)  |

When in doubt, start with `layered`: it handles any graph and respects the direction of your
arrows. Switch when the picture you want has a shape of its own.

### layered

The default. Elements sit in layers so that most lines point in the view's direction, and the
order inside each layer is chosen to avoid crossings. Lines bend at right angles around boxes.

### tree

Each element hangs under the element whose line reaches it first, so `a -> b` makes `b` a child
of `a`. Elements nothing points to are the roots, laid side by side in the order you listed them;
children follow the order of their lines. A line that is not part of the tree (a second parent, a
line back up) is still drawn, routed around the boxes.

Tree lines go from the parent to a bar just past its row, across, and on to the child, like an
org chart. A line's label sits on its last stretch, next to the child, and the gap between rows
grows to fit it. `direction right` turns the tree on its side; `up` and `left` flip it.

### grid

Elements fill a grid in the order the view lists them (the `include` order, or model order for
`include *`), about as many rows as columns: nine elements make three rows of three, ten make
rows of four (four, four and two). Each column is as wide as its widest box and each row as tall
as its tallest, with boxes centred in their cells.

`direction down` (the default) fills row by row, rows going down; `right` fills column by
column, columns going right. `up` and `left` start from the other side. Lines are routed between
the cells, so a grid suits views with few lines; for many lines pick `layered`.

### radial

The element with the most lines goes in the middle; the elements it connects to sit on a ring
around it, theirs on the next ring out, and so on. Each branch gets a slice of the circle in
proportion to how many elements hang from it, and rings are spaced so that boxes and labels fit.
Parts of the view that are not connected to each other sit side by side. Direction does not apply.

### force

A simulation: every pair of elements pushes apart, every line pulls its ends together, and the
movement settles over a fixed number of rounds. Groups of tightly connected elements end up close
together, which shows clusters you might not see in a layered view. The result is the same every
time for the same view, but adding or removing one element can move the others noticeably; use
hints, or switch to `fixed` once you like the picture, if you need it to stay put. Direction does
not apply.

## Containers

All the automatic algorithms lay out the inside of each container (a system's containers, a
group's members) first, with the same algorithm, then place the container as one box among its
neighbours. Lines between elements in different containers are routed around the boxes in
between.

## Hints still apply

`pin a below b`, `align a b horizontally` and `order a b c` work with every automatic algorithm.
Where the algorithm's own placement does not satisfy a hint, a correction pass moves the elements
afterwards and reroutes the lines it touched. `diadraw render` warns about any hint that still
could not be met. Dragging an element on the canvas writes `pin … at` (see
[Moving boxes](moving.md)); with Option or Alt held it writes these relative hints instead, so
the same applies to those canvas edits.

## Names DiaDraw does not know

A misspelled or unsupported name, such as `layout circular` or `layout layred`, draws the view
with `layered` and shows a warning on the `layout` line that lists the names it knows. The name
stays in your file, so nothing is lost if a later version adds it.
