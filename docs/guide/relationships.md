# Relationships

A relationship is a line between two elements: a call, a data flow, a dependency. Like elements,
relationships are declared once in the `model` block, and every view that shows both ends draws
them.

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app"
    db  = container "Orders DB"
    web -> db "writes" tech "SQL"
  }
  customer -> shop.web "places order"
}
```

## Arrows

| Arrow | Drawn as                  | Typical use                      |
| ----- | ------------------------- | -------------------------------- |
| `->`  | solid line, one arrowhead | a call, a request, a dependency  |
| `-->` | dashed line               | an asynchronous or indirect flow |
| `<->` | arrowheads at both ends   | a two-way exchange               |

After the target come an optional label and any of `tech`, `tag`, `style` and `prop`, as on
elements. The label and the technology are what the diagram shows on the line.

## Line ends and line styles

The arrow gives a line its usual look. For anything else, such as an open arrow, a diamond, crow's
foot ends, a dotted or double line, or a curve, give the relationship a style. See
[Line ends and line styles](line-styles.md).

## Endpoints and ports

An endpoint is an element, written as you would refer to it from where the relationship is
declared: `web` inside `shop { … }`, `shop.web` anywhere. If the element declares a port
(`api = container { port http north }`), `api.http` attaches the line to that port.

## Reconnecting a line

To point a line at a different element, click the line to select it. A small circle appears on
each of its ends. Drag one of them onto another element and let go; the element under the
pointer is highlighted while you drag. Drop it on empty space, or back on an element the line
already touches, and nothing changes.

The text changes only where that end is written:

```
web -> legacy "REST" tech "HTTPS"  // being migrated
```

becomes, after dragging the arrowhead from Legacy API to API,

```
web -> api "REST" tech "HTTPS"  // being migrated
```

The label, `tech`, tags, comments and the arrow stay as they were. The new end is written the
shortest way that reaches the element from where the line is declared: `api` inside
`shop { … }`, `shop.api` outside it. In the rare case that no name works from there (inside
`shop`, a top-level `db` is hidden by `shop.db`), the file is reformatted instead, and the
status line says so.

Some things to know:

- **Only elements, not ports.** Dropping an end on an element attaches it to the element itself.
  To attach to a port (`api.http`), edit the text. Dropping an end that is on a port back on the
  same element keeps the port.
- **Containers count.** Dropping on a container's frame, away from the elements inside it,
  connects the line to the container.
- **No self-loops.** You cannot drop an end on the element the other end is on.
- **Lines a view draws for you cannot be reconnected.** When a view hides an element, it draws
  its lines to the nearest visible ancestor instead (an implied relationship). Those lines are not
  in your text as drawn, so they have no handles; reconnect the real line in a view that shows
  both ends, or in the text.
- **Undo** (⌘Z, or Ctrl+Z) puts the end back, in the text and on the canvas, like any other
  canvas edit.

What happens to the line's id, and to anything that refers to it, depends on whether it is
named; the next section explains ids, and [what reconnecting does to them](#what-reconnecting-does-to-ids)
follows it.

## Relationship ids

Every relationship has an id. You never see it on the diagram, but other parts of DiaDraw use it
to point at one particular line:

- **Hand-made routing.** When you [drag a line's bend points](fixed-views.md#bending-lines) on
  the canvas, or import a draw.io diagram, the shape of the line is saved in the view as `route "<id>" via …`.
- **Canvas edits.** Selecting, relabelling and deleting a line work by its id, and so do the
  small text changes the canvas writes back into your file.
- **Exported SVG.** Each line is drawn as `<g class="dd-edge" data-id="<id>">`, so a web page,
  a test or a stylesheet can find it, for example to highlight `[data-id="checkout"]`.
- **Reviews.** With a stable id, a diff shows "this line now goes somewhere else" rather than
  one line removed and another added.

### Derived ids

Unless you name it, a relationship's id comes from its endpoints: `web -> db` is `web->db`,
`web -> api.http` is `web->api.http`, and a second line between the same two elements is
`web->db#2`, then `#3`, and so on.

That is convenient, but the id follows the endpoints. It changes when you:

- **reconnect the line**: `web -> legacy` becomes `web -> api`, so its id goes from
  `web->legacy` to `web->api`;
- **delete an earlier parallel line**: with `order -> billing` and a second
  `order -> billing`, deleting the first renumbers the second from `order->billing#2` to
  `order->billing`.

When you change the endpoints by editing the text, anything that pointed at the old id stops
working: a `route` that named it is reported as `Unknown relationship`, and the hand-made bend
points are lost. Reconnecting on the canvas takes care of this for you (see below).

### Named relationships

Put a name and `=` in front of a relationship to give it an id of its own:

```
model {
  web    = container "Web"
  api    = container "API"
  legacy = container "Legacy API"

  calls = web -> legacy "REST" tech "HTTPS"
}

view main {
  route "calls" via 200, 80
}
```

Now the id is `calls` for good. Change the line to `calls = web -> api` and the route still
applies. You can also name a line from the canvas: select it and type the name in the
inspector's Name field, which updates its routes for you (see
[Naming a relationship from the inspector](element-properties.md#naming-a-relationship-from-the-inspector)). Named and unnamed lines can sit side by side:

```
order -> billing "charge"
refund = order -> billing "refund" tag "async"
order -> billing "audit"
```

These have the ids `order->billing`, `refund` and `order->billing#2`. Naming one line does not
renumber the others.

Rules for names:

- A name is an identifier (letters, digits and `_`, not starting with a digit).
- It is used exactly as written, even inside an element: `writes = web -> db` inside
  `shop { … }` has the id `writes`, not `shop.writes`.
- It must be unique among relationships; a second `x = …` is an error. It may repeat an
  element's name, since elements and relationships have separate ids.
- Words like `group`, `port` or `layout` are fine as names; the arrow tells DiaDraw that the
  line is a relationship.

### When to name a relationship

Name a relationship when something outside the line itself depends on it:

- you have routed it by hand, or expect to;
- it is one of several lines between the same two elements and you may delete or reorder them;
- you expect to reconnect it as the design changes, for example while migrating from one
  service to another;
- a page, test or stylesheet refers to it through the exported SVG.

If none of these apply, leave it unnamed: the derived id is fine, and the text stays shorter.

### What reconnecting does to ids

When you reconnect a line on the canvas:

- **A named line keeps its name.** `calls = web -> legacy` becomes `calls = web -> api`, and
  `route "calls" …` still applies.
- **An unnamed line takes the id of its new endpoints**, `web->legacy` becomes `web->api`, and
  every `route` that named the old id is rewritten to the new one, so the bend points stay with
  the line.
- **Parallel lines are renumbered, and their routes follow them.** With two unnamed
  `order -> billing` lines, reconnecting the first one to `ledger` turns the second from
  `order->billing#2` into `order->billing`. A route written for the first line is rewritten to
  `order->ledger` and the second line's route to `order->billing`, so neither route ends up on
  the other line. The same happens the other way: moving a line onto a pair that already has a
  later line between them makes that later line `#2`.
- **The line stays selected**, under its new id.

A route keeps its bend points when its line is reconnected. They were drawn for the old ends, so
the line may now take a detour; drag the bend points again, or delete the `route` line from the
view to let the layout route it.

Routes are the only place your text names an unnamed line, so they are the only thing rewritten.
A page, test or stylesheet that finds the line in exported SVG by `data-id` is outside the file
and is not updated: name the line if something like that depends on it.

### Ids that cannot be written as names

A model can also be loaded from JSON or built by a program, and its relationship ids may not be
valid names (`edge-1`, for example). When such a model is printed as text, those lines are
written without a name, so reading the text back gives them derived ids.

## See also

- [DSL reference: semantics](../dsl.md#semantics) for the exact rules.
- [Freeform diagrams](../dsl.md#freeform-diagrams) for `route` and the other layout hints.
- [Drawing by hand in fixed views](fixed-views.md) for bending lines on the canvas.
