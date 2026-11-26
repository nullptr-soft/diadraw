# Drawing by hand in fixed views

Most views are laid out for you: you say what is in them and, if you like, nudge the layout with
hints such as `pin db below web`. Sometimes you want the opposite: every box exactly where you put
it, at the size you chose, and lines that bend where you tell them to. That is what
`layout fixed` is for. Diagrams imported from draw.io use it, and so can any view you write:

```
view checkout {
  include *
  layout fixed
  pin web at 40, 40 size 160, 70
  pin db at 320, 200
  route "writes" via 380, 90
}
```

`pin … at X, Y` puts a box's top-left corner at a point, and `size W, H` gives it a width and
height (without one, the box keeps the size its label needs). `route "id" via X, Y …` lists the
points a line bends at, by the [relationship's id](relationships.md#relationship-ids). Elements
without a `pin` line up in a row under the pinned ones.

You can write all of that in the text, but on the canvas you can also just draw.

## Moving

Drag a box to move it. In a fixed view it lands exactly where you drop it, Option or not, and its
`pin … at` line changes to the new position. A size it had stays.

## Resizing

Select one box and four small square handles appear on its corners. Drag one to resize the box:
the opposite corner stays where it is, and the new size is shown above the box while you drag.
Boxes do not get smaller than 10 × 10.

When you let go, the box's `pin` gets the new position and size:

```
  pin web at 40, 40   // storefront
```

becomes

```
  pin web at 40, 40 size 180, 82   // storefront
```

## Bending lines

Select one line and handles appear on it:

- a round handle on each **bend point**: drag it to move that bend;
- a smaller, lighter handle in the **middle of each segment**: drag it to add a bend there;
- drag a bend onto the straight line between its neighbours to **remove** it.

When you let go, the line's `route` lists all of its bend points, in order:

```
  route "writes" via 380, 90 380, 150
```

A `from` or `to` already on the route (where the line leaves and enters its boxes) is kept. A
route with no bend points left and no `from` or `to` is removed, and the line is drawn straight
again.

An unnamed relationship is routed by its derived id, such as `route "web->db"`. That id changes
when you change the relationship's endpoints or delete a parallel one, and the route then stops
applying, so [name the relationship](relationships.md#when-to-name-a-relationship) if you shape
its line by hand: `writes = web -> db`.

## What happens to your text

As with every canvas edit, the change is a small edit to your file and one undo step, shared with
the text editor (⌘Z on either side takes it back):

- a `pin` or `route` that is already there is rewritten **where it stands**; a comment after it
  stays, and in a view of a system the name stays as you wrote it (`pin web`, not
  `pin shop.web`);
- a new `pin` or `route` is added after the view's other lines;
- coordinates are rounded to whole numbers.

## Things to know

- **Bend points only in fixed views.** Views laid out automatically do not show bend handles:
  their lines come from the layout. Boxes can be resized and turned in every view; see
  [Resizing and rotating](resize-and-rotate.md). Add `layout fixed` to the view to draw it by
  hand, or keep steering the layout with hints.
- **One thing at a time.** Handles appear when exactly one box or one line is selected.
- **Orthogonal lines** (`edgeStyle=orthogonalEdgeStyle`) keep their right angles: after you move
  a bend, the line gets the corners it needs to stay orthogonal.
- **Where it is available:** the web app, the macOS app, the Linux (GTK) app and the Windows app.
