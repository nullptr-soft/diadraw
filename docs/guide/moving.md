# Moving boxes

Drag a box to move it. It stays where you let go, in every view, whatever its layout. Letting go
writes one line into the view:

```
view main {
  include *
  layout layered direction down
  pin db at 262, 246
}
```

Like a `size` or a `rotate`, the line belongs to that view only, so the same element can sit in
different places in different views. In an automatic layout the other boxes are still arranged
by the algorithm; any box the moved one lands on is pushed aside and the lines touching it are
rerouted. Drag the same box again and its `pin … at` line changes to the new position.

## Placing by the neighbours

Hold Option (Alt on Windows and Linux) while you drag to place the box relative to the others
instead. While you drag, the line it would write is shown above the box:

- If it lines up with another box, within 14 pixels, the drag writes
  `align api db horizontally` (or `vertically`).
- Otherwise it writes `pin db right of api` (or `left of`, `above`, `below`) against the nearest
  box.

Those lines keep their meaning when the layout changes, so they suit diagrams that keep
growing. If the line is already true, for example two boxes that already share a row, the box
stays where the layout had it.

In a `layout fixed` view the drop always writes `pin … at`, Option or not. See
[Drawing by hand in fixed views](fixed-views.md).

## Selecting and moving several boxes

Click a box to select it. Shift-click (or Command-click on macOS, Ctrl-click on Windows and Linux)
adds a box or a line to the selection, or takes it out again. To pick out a region, press on an
empty part of the canvas and drag a band over it: everything wholly inside the band is selected,
boxes and lines alike (hold Shift or Command/Ctrl to add to what is already selected). On the
canvas, Command-A (Ctrl-A in the Linux app and in the web app on Windows and Linux) selects everything in the view, and Escape or a click
on empty canvas clears it. Clicking one of several selected boxes, without dragging, selects it
alone.

Drag any one of the selected boxes and they all move together, keeping their places relative to
each other; the outline of each is shown where it will land. Letting go writes a `pin … at` line
for every box, as one change, so a single Undo puts them all back:

```
view main {
  include *
  layout layered direction down
  pin api at 262, 120
  pin db at 262, 246
}
```

A group always lands where you let go: Option (Alt) does not place it by its neighbours. In a
`layout fixed` view, a line whose two ends both move keeps its shape, because its bend points move
with it. Delete (or Backspace) removes everything selected.

Pressing inside a container selects the container rather than starting a band, so start a band
outside it, or select its boxes with Shift-click.

## Putting a box back

Delete its `pin`, `align` or `order` line from the view, and the layout places it again.
