# Resizing and rotating

Any box on the canvas can be made bigger or smaller and turned, in every view, whatever its
layout. The change is written into the view as a line of text, so it stays with that view and
nowhere else: the same element can be large in a detail view and small in an overview.

```
view checkout of shop {
  include *
  size web 220, 90
  rotate db -15
}
```

## Resizing

Select one box and small square handles appear on its corners. Drag one: the opposite corner
stays where it is and the new size shows above the box while you drag. Boxes do not get smaller
than 10 × 10.

Hold **Shift** while dragging to keep the box's proportions: width and height grow or shrink
together, by whichever the pointer asks for more. Sizes are rounded to whole numbers, so the ratio
is kept to within a unit, and with the grid shown a Shift resize does not snap to it (grid steps
rarely keep a ratio).

- In a view **laid out automatically** (`layered`, `tree`, `grid`, `radial`, `force`), letting go
  writes `size web 220, 90`. The layout keeps that size and arranges everything else around it,
  so the box may move as the layout makes room.
- In a **sequence** view the size applies to the participant's head.
- In a **fixed** view the size goes into the box's `pin … at … size` line, as before (see
  [Drawing by hand in fixed views](fixed-views.md)).
- Containers (boxes with other boxes inside) are sized around their children by the layout, so
  only fixed views resize them.

## Sizing a box to fit its content

To undo a resize and give a box back the size its name and shape need, **double-click one of its
corner handles**, or choose **Size to Fit Content**: in the Edit menu on the Mac (⌥⌘=) and in
Windows, and in the main menu of the Linux app (Ctrl+Alt+=). In the web app it is the **Fit**
button in the toolbar (⌥⌘= on the Mac, Ctrl+Alt+= elsewhere). The command works on every
selected box at once and is one step to undo.

Fitting takes the size out of the text rather than writing a new one, so the box follows its
content from then on: rename it and it grows or shrinks to match.

- In a view laid out automatically and in a sequence view, the `size web …` line goes.
- In a fixed view the box keeps its place: `pin web at 40, 60 size 220, 90` becomes
  `pin web at 40, 60`.
- A container in a fixed view loses its `pin` line altogether, even one without a size, so it
  wraps its children again.
- A box that has no size of its own already fits, and fitting it changes nothing.

## Rotating

A selected box also has a round handle just off its top-right corner. Drag it around the box to
turn it: the box turns as far as the pointer goes around the box's centre. The turn snaps to
multiples of 15° when it is within 5° of one, and to whole degrees otherwise; the angle shows
above the box while you drag. Letting go writes `rotate db -15` (degrees, clockwise; negative is
anticlockwise). Turning it back upright removes the line.

The label turns with the shape. Lines attach to the turned outline, and automatic layouts leave
room for the turned shape, so a long box turned on end takes the space of a tall one.

Containers and sequence participants are not turned. A `rotate` on one is shown as a layout hint
that could not be honoured.

## What happens to your text

- A `size` or `rotate` that is already there is rewritten **where it stands**, keeping a comment
  after it and the name as you wrote it; a new one is added after the view's other lines.
- Moving a box never changes its `size` or `rotate`.
- Sizes are rounded to whole numbers; turns to a tenth of a degree, between -180 and 180.

## Where it is available

The macOS app, the Linux (GTK) app, the Windows app and the web app have the resize and rotate
handles in every view.
