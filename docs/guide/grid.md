# The grid

Turn on the grid to line boxes up by hand. On macOS choose **View ▸ Show Grid** (⌘') or click the
grid button in the toolbar; on Windows and Linux choose **Show Grid** in the menu (Ctrl+' on
Linux); in the web app click **Grid** in the toolbar (⌘' or Ctrl+'). The grid is drawn behind the diagram, every 10 diagram units by default, with a darker
line every fifth step. Zoomed far out, the lighter lines fade away first.

While the grid shows, what you do on the canvas lands on it:

- **Moving a box** puts its top-left corner on the nearest grid point, so the `pin … at` line
  it writes has grid numbers (`pin db at 260, 250` with a 10 grid). See [Moving boxes](moving.md).
- **Resizing a box** in a `layout fixed` view puts the corner you drag on a grid point. In the
  other views, where the layout decides where the box goes and only its `size` is written, and
  on a turned box, the width and height become whole grid steps instead. A Shift resize, which
  keeps the box's proportions, is not snapped. See
  [Resizing and rotating](resize-and-rotate.md).
- **Bending a line** in a fixed view puts the bend point on a grid point.

Option-drag (Alt on Windows and Linux) is not affected: the box is placed by its neighbours, as
the layout sees fit. In a fixed view, where Option-drag still writes `pin … at`, it drops the box
exactly where you let go, off the grid. Turning a box keeps its own 15° steps.

The grid is a setting of the app, not of the drawing: it writes nothing into your text, and it
stays on or off for every document you open. On macOS **View ▸ Grid Size** picks a step of 5,
10, 20 or 40 units (and turns the grid on), as the size list next to **Grid** does in the web app,
which keeps the choice in the browser; the choice is remembered. Turning the grid off
leaves everything where it is; later drags go back to exactly where you let go.
