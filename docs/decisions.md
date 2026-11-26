# Design decisions

Small decisions made while building, with the reason. Newest milestone last. Anything here can be
revisited; the entry says what it would take.

Entries before "One C++ core everywhere" were written for the TypeScript implementation; the
packages they name (`@diadraw/core`, `dsl`, `layout`, `render`, `import`, `library`, `markdown`,
`language-server`) have since been removed, and the C++ core carries the same decisions (see
[src/README.md](../src/README.md)).

## M0 · Foundations

- **npm workspaces.** `package.json` lists `packages/*` and `src/apps/*`; internal dependencies are
  `"*"`, which npm links to the workspace. (pnpm 12 until November 2026; npm needs no extra
  tool and runs the one dependency build script, `esbuild` for Vite, without an allow-list.)
- **Packages build to `dist/` with `tsc`; dependents use the built output.** Turborepo orders the
  builds. Simpler than TS project references or source-pointing export conditions; revisit if
  watch-mode across packages becomes painful.
- **Every package was `private` and `UNLICENSED` at first.** DiaDraw's own code is now under the
  MIT License (`LICENSE`); the packages stay `private` until they are published. Third-party code
  and assets keep their licences, listed in `docs/credits.md`, which `tools/gen/credits.mjs`
  generates from `tools/credits/components.json` and the npm lockfiles, so the website's Credits
  page and the apps' About dialogs (which read `src/host/data/credits.tsv`) come from one source.

## M1 · Text to diagram

- **The generated parser is committed** (`packages/dsl/src/parser.ts`), and a test fails when it
  is out of date with `diadraw.grammar`. Run `npm run generate -w @diadraw/dsl` after editing
  the grammar. This keeps installs and builds free of a generator step.
- **Keywords were reserved at first** (`left`, `right`, `order`, `layout`, …); M3 made them
  contextual (see there).
- **Element ids are dotted paths** (`shop.web`): the id of a nested element is its parent's id
  plus its local name. The printer relies on this to print local names.
- **References resolve innermost scope first**, then outwards to the top level, so `web -> db`
  inside `shop { … }` and `shop.web -> shop.db` anywhere both work.
- **Relationship ids come from their endpoints** (`a->b`, then `a->b#2` for parallel edges, with
  `.port` when an endpoint is a port). Ids stay stable while the endpoints do, which canvas edits
  and diffing rely on. `name = a -> b` gives an edge an explicit id instead, for when its
  identity must outlive its endpoints; derived ids are never names, so the two cannot collide.
- **Group ids are paths of group names** (`Clients/Mobile`, prefixed by the containing element
  when inside one). A group never crosses an element boundary.
- **Implied relationships** (as in C4): when a view hides a relationship's endpoint but shows an
  ancestor, the relationship is drawn between the visible ancestors. The first relationship for
  a pair wins; edges between an element and its own ancestor are dropped.
- **Parsing never throws.** Syntax errors come from Lezer's error recovery; model errors (unknown
  references, duplicates) come from the core operations themselves, so the DSL and the canvas
  report the same errors. Unknown element kinds are warnings, since plugins will add kinds.
- **How ELK honours hints.** A pin along the flow (`below` in a top-down view) becomes an
  invisible edge, so the element lands in a later layer. Ordering across the flow (`order`, or
  `right of` in a top-down view) sorts ELK's input and turns on model order: first the soft
  `PREFER_NODES` strategy, then `forceNodeModelOrder` only if a hint was still missed. ELK's
  `inLayerSuccOf` and `positionChoiceConstraint` options had no effect in elkjs 0.10, so they
  are not used.
- **Hints are checked after layout.** Whatever the engine did, each hint is checked against the
  final geometry and reported in `ignoredHints` if it does not hold. The CLI warns and the web
  app shows a count. `pinAt` and `align` are only checked for now (the M4 constraint pass will
  apply them).
- **ELK coordinates.** `elk.json.edgeCoords: ROOT` makes edge and label coordinates absolute, so
  compound nodes cannot shift them. Edge labels need a `text` field or ELK ignores them.
- **ELK in the browser runs in ELK's own worker** (`elk-api` on the main thread, `elk-worker.min.js`
  as a classic worker), created on the first layout. Running `elk.bundled.js` inside our own
  worker does not work: it detects the worker context and installs itself as the worker's
  message handler. In Node the bundled build is imported lazily.
- **Text is measured without a DOM**, using approximate per-glyph widths for a sans font, so the
  CLI, a future render server and the browser get the same layout. Real font metrics can replace
  `textWidth` later.
- **Share links keep the document in the URL hash** (`#v1:` + base64url of deflate-raw), using
  `CompressionStream`. No server is needed; very large documents make long URLs.
- **Benchmark metrics** are crossings, bends, area, edge length, node overlaps, label overlaps,
  unmet hints and median layout time over three runs after a warm-up. The corpus plus seeded
  synthetic flows (50, 100 and 200 nodes) make up the suite. The baseline is in
  `packages/bench/results/m1-baseline.json`.

## M2 · WYSIWYG and round-trip

- **Text stays the source of truth.** A canvas gesture becomes a typed operation;
  `updateSource(source, op)` turns it into text edits; the new text is parsed again and that
  parse is the model everyone sees. The canvas never holds a model of its own.
- **Patches are planned from the source map** (spans of declarations, labels, kinds, targets,
  view bodies, hints and selectors). Deleting a statement removes its line when nothing else is
  on it, a trailing `//` comment included; otherwise only the statement. Inserts go before the
  closing brace of the right body, at that body's indentation.
- **Every patch is verified**: the patched text must parse without new errors and print to the
  same canonical text as the expected model. If not, or if an operation has no local patch (for
  example adding a group, or a view whose only `include` would become empty), the file is
  reprinted from the model and the result says `minimal: false`. The web app tells the user when
  that happens. A property test runs 300 random five-step edit sequences over the corpus and
  requires at least 90% of edits to be local patches (in practice nearly all are).
- **Canonical print equality is the model equality for text.** Derived relationship ids can
  renumber (`a->b#2` becomes `a->b` when the first is deleted). The print writes explicit ids
  (`pay = a -> b`) but not derived ones, and the editor always continues from the re-parsed
  model.
- **One undo history for both panes: CodeMirror's.** Canvas edits are dispatched to the text
  editor as transactions (`userEvent: input.canvas`), so ⌘Z undoes the last change, whichever
  pane made it. The core `OperationLog` remains for hosts without a text editor.
- **Removing an element cleans up views**: hints and selectors that name it are removed and a
  view scoped to it is deleted, so the text never refers to something that is gone. The
  inverse restores them (`replaceView` / `addView`).
- **A drag drops the node where it is let go.** A plain drag writes `pin x at X, Y` in every
  view; auto layouts arrange the rest around it, as they do for `size` and `rotate`. Option/Alt
  asks for intent instead: if the node lines up (within 14 px) with another node, the drag
  writes `align a b horizontally|vertically`; otherwise `pin x below|above|left of|right of y`
  against the nearest node. A new drag replaces the hints that placed that node before.
  (Until November 2026 intent was the default and Alt wrote `pin … at`. The inferred hint was
  often already true, such as an `align` of two nodes the layout had already put in one row, so
  the node snapped back to where it started and moving looked broken.)
- **New elements go into the current view's scope** and get ids like `process1`. Connections
  get the id the text will give them, so the canvas and the text agree right away.
- **Reconnecting carries its id changes.** `reconnectRelationship { id, source, target,
renames }` moves a relationship's ends and renames relationship ids in one step, routes
  included. The model has no notion of derived ids, so the editor computes `renames` from the
  parse: it derives every unnamed id again in declaration order, as reading the text back will.
  The moved edge and any parallel edge whose `#n` shifts are renamed together, so each `route`
  stays with the edge it was drawn for instead of silently moving to the edge that inherits its
  old id. The alternatives were worse: dropping the routes loses hand-made geometry on edges
  that did not move, and naming the edge automatically writes ids the person did not ask for.
  The patch rewrites the moved endpoint (as the shortest reference that resolves from the
  declaration's scope) and the id literal of each affected `route`; the verify step reprints
  when no reference reaches the element.

## M3 · Desktop and integrations

- **Contextual keywords.** Drawing DiaDraw's own architecture needed an element called
  `layout`, so keywords now use Lezer `@extend` (both readings are tried) and every
  keyword-led statement has `@dynamicPrecedence=1`, so the keyword reading wins when both
  parse. The importers still add `_` to keyword-like names, to stay safe in selector lists.
- **A scoped view shows the scope's direct children**, not all descendants, like a C4 container
  view. Relationships of deeper elements are lifted to them (implied relationships).
- **Importers go through core operations** and `print`, so imported files obey the same rules as
  edited ones. Element ids come from the source ids or, for draw.io, from labels.
  - Mermaid: stadium nodes become `start` or `end` depending on their links; undirected links
    become directed relationships (noted in the report); per-subgraph direction is dropped.
  - PlantUML: `node`/`cloud` bodies nest elements; `package`/`folder`/`frame`/`rectangle`
    bodies become groups. Aliases used only in relations become components.
  - Structurizr: context views list the subject and its neighbours (lifted to top-level);
    container and component views are scoped views plus the external neighbours. Only the
    `shape` of element styles carries over. Deployment and dynamic views are reported.
  - draw.io: swimlanes and containers with children become groups. Ellipses and terminators
    become `start`/`end` by their links. Each page becomes a view. The view direction is
    guessed from where connectors point in the drawing. Positions are not imported.
- **Export uses resvg and PDFKit** (prebuilt binaries and pure JS; no system dependencies).
  PDF text uses the standard Helvetica faces, so nothing is embedded.
- **The VS Code extension bundles everything with esbuild** into CommonJS (the extension and
  the language server). The preview renders in the extension host with in-process ELK.
  Publishing (`vsce`, a publisher id) is left for later.
- **Desktop: Tauri 2 scaffolded, native build pending.** This Mac has Rust 1.68 (via asdf); Tauri
  2 needs 1.77.2 or newer, and installing toolchains is Andriy's call. The config, Rust entry
  point, capabilities (dialogs; read, write and watch user files) and icons are in place; the
  `@tauri-apps/cli` npm package is installed, so `npm run dev:native -w @diadraw/desktop`
  should work once Rust is updated. Native scripts are named `dev:native`/`build:native` so
  Turborepo and CI never run them.
- **Files in the web app**: Tauri dialogs and `fs.watch` on desktop; the File System Access API
  in Chromium browsers (save in place); a file input and download elsewhere.

## M4 · Layout

- **Constraint pass after the engine** (`packages/layout/src/constraints.ts`). Hints the engine
  missed are applied directly, in source order (a later hint wins):
  - `pin x at X, Y` moves x there.
  - `pin x below y` (and the other placements) puts x next to y, 40 px away, centred on it.
  - `align` lines nodes up on the first node's centre line.
  - `order` reassigns the nodes' own slots across the flow in the listed order.

  Then nodes the moves landed on are pushed aside across the flow, containers refit around their
  children, and the edges touching moved nodes are rerouted. ELK's own layout is untouched
  everywhere else.

- **Orthogonal router** (`route.ts`): A* over the grid of lines through obstacle edges (inflated
  by a 12 px margin) and the end points, costing length plus 40 px per bend. It tries the two
  most facing sides of each end and keeps the cheapest route. Labels go on the longest segment
  that keeps them clear of nodes. It is used for rerouted edges only; ELK still routes the rest.
- **Stability: three candidates, keep the stillest that is no worse.** With a previous layout the
  engine lays out fresh, "hybrid" (ELK's interactive layering and placement plus its normal
  crossing minimisation, fed in the previous order) and "interactive" (all of ELK's phases
  interactive). It keeps the candidate with the lowest drift among those with no more
  crossings and no more unmet hints than the fresh one. Interactive phases need the same options
  on every compound node when hierarchy handling is on.
- **Drift is measured after removing the overall shift** (median dx, dy), so a diagram that only
  moved as a whole counts as stable.
- **The M1 baseline was re-run** with the two new columns (drift, order flips). Every original
  number matched exactly; times differ by run.
- **The renderer frames everything drawn**, so `pin … at` with negative coordinates stays
  visible.
- **The web app keeps the last layout per view** and passes it to the next layout, so edits on
  either pane keep the diagram steady.

## draw.io import and export

- **Geometry is kept by default.** draw.io drawings are freeform, so each page becomes a view with
  `layout fixed`: every shape gets `pin … at X, Y size W, H` with its absolute position (container
  offsets added up), and every connector with bend points or fixed exit/entry points gets a
  `route` hint. A shape with relative geometry on its container (an icon on a component's corner)
  is an adornment instead: `anchor` and `anchorOffset` in its style and a `size`, so the fixed
  layout keeps it on that spot of its container through every move and resize, and the exporter
  writes it back as relative geometry. A UML message (`edgeStyle=sequenceEdgeStyle`) is routed
  once at import as draw.io routes it, horizontal at the y its fixed end or its one waypoint
  gives, a self-call as a loop out to the right, and kept as a `route` hint whose ends are
  fractions of the activation bars or lifelines; its label's spot on the line becomes `labelAt`. A labelled draw.io edge
  keeps its `align` and `verticalAlign` too; with either, or with a label spot off the middle, a
  fixed view puts its label where draw.io does (measured along the line by length) instead of on
  the longest segment. `--geometry drop` (CLI) or `{ geometry: 'drop' }` imports only the content and
  lets the layered layout redo it, in the direction the drawing's connectors mostly point.
- **The model grew three things for this**, all general: `style` and `prop` on elements and
  relationships, sizes on `pin … at`, and the `route` hint. The fixed engine is a layout
  algorithm like any other, so freeform views still render in the CLI, the web app, Markdown and
  VS Code. `layout()` uses it for `layout fixed` whatever engine the caller passes.
- **Shapes map to a small set of base shapes** the renderer draws (29 of them). A catalogue maps
  the general, flowchart, UML, ER, BPMN and basic names. Whole stencil families (AWS, Azure, GCP,
  Cisco, network, Kubernetes, P&ID, …, and any other `mxgraph.*` stencil) are drawn as icon boxes
  in the stencil's colour with the label where draw.io puts it. The `perimeter` style tells round
  and diamond shapes apart when the name alone does not. A name that matches nothing becomes a
  rectangle and is listed in the report's `unknownShapes`.
- **Passthrough for round trips.** Every imported cell keeps its draw.io id, original style
  string and (for HTML labels) the label HTML as `drawio.*` properties; custom data, tooltips and
  links from `object`/`UserObject` wrappers become plain properties. `exportDrawio` writes those
  back, so draw.io → DiaDraw → draw.io keeps everything DiaDraw does not understand. Elements
  that did not come from draw.io get a style made from their shape and style.
- **Structure:** containers, swimlanes, tables and `group` cells nest their children as child
  elements (groups are drawn invisible). Layers are recorded as a `drawio.layer` property when
  there is more than one. Shapes on hidden layers and inside collapsed containers are imported
  but left out of the page's view (and so not exported again). Labels on connectors (child
  cells) join the connector's label. HTML labels become text with line breaks; `%placeholders%`
  are filled from the cell's data.
- **Loose connector ends** (a `sourcePoint`/`targetPoint` instead of a shape) get an invisible
  zero-size anchor element, so the connector is still drawn where it was. With geometry dropped
  they are reported instead.
- **File forms:** `.drawio`/`.xml` with plain or compressed pages (base64 + raw deflate +
  URI-encoding), several pages, `.drawio.svg` (the `content` attribute, escaped or URL-encoded),
  `.drawio.png` (`tEXt`, `zTXt` or `iTXt` chunk named `mxfile`). Visio `.vsdx` is not cheap to
  support (a zip of a different XML vocabulary) and is reported as unsupported; draw.io shape
  libraries (`<mxlibrary>`) are reported as "not a diagram".
- **Label font styles as style keys.** An HTML label's `<b>`, `<i>`, `<u>` and `<s>`/`<strike>` become
  draw.io's own `fontStyle` bits (1, 2, 4, 8): the element's `fontStyle` when the whole label has
  one, `lineFontStyles` when each line has one, and `fontRuns` (code-point runs over the name)
  when they change within a line. The name stays plain text, so search, editing and every other
  reader of it are unaffected; runs that no longer add up to an edited name stay in the style but
  are ignored when drawing. The renderer writes the runs as `<tspan>`s, and the display list carries them to the native canvases
  and the PDF writer. lunasvg (native PNG export) draws bold and italic but no text-decoration.
- **Never fail.** Every problem becomes an entry in the report: dropped connectors, unknown
  shapes and unknown style keys (with counts), plus `stats` (pages, shapes, connectors,
  relationships, anchors, hidden, stencils).
- **Corpus:** `corpus/drawio` holds 308 real files (the jgraph/drawio templates, CC BY 4.0, and
  a quarter of jgraph/drawio-diagrams under 100 KB, Apache-2.0); see its README. Every file must
  import without throwing, match the shape and connector counts of the source XML, and render
  every page; three are snapshot-tested.
- **Limits:** stencil graphics are not drawn (icon boxes instead); an edge's `curved`, `rounded`
  and `dashPattern` are not imported (so imported pages keep rendering as they did), and curved
  edges are drawn straight through their bend points, though a relationship styled `curved=1`
  in DiaDraw is drawn curved and exported with it; rotation, gradients, shadows, sketch style, images' pixels
  and rich text other than bold, italic, underline and strike-through (colours, fonts and sizes
  of parts of a label) are kept only in the passthrough copy; edge label offsets are not
  applied (labels sit on the longest segment).

## PlantUML import

- **One model, a view per diagram.** Each `@start…`/`@end…` block of a file is imported into the
  same model as its own view. Element keys are prefixed per block, so two diagrams in a file do
  not merge their elements.
- **The preprocessor runs, the C4 library does not.** `!define`, `!definelong`, variables,
  `!function`/`!procedure` (defaults, named arguments), `!if`/`!ifdef`, `!include` (local files
  through a resolver), `!includesub` and the common builtins are executed, because real files
  build diagrams with them. C4-PlantUML macros are _not_ expanded: expanding them yields
  stereotyped rectangles and loses the meaning. Instead the C4 parser reads `Person(…)`,
  `Rel_D(…)` and the rest directly, using parameter names generated from a vendored copy of
  C4-PlantUML (MIT, commit recorded in `stdlib/C4-PlantUML/COMMIT`), so `$tags="…"` style named
  arguments land in the right place.
- **Nothing is fetched.** `!include <C4/…>` and the usual C4-PlantUML URLs resolve to the
  vendored copy. Icon libraries (AWS, Azure, GCP, Kubernetes, tupadr3, Office, …) are recognised
  by stdlib name, URL or relative path; their macro calls become elements with `icon` and
  `iconLibrary` properties and `shape image`. Any other remote include is reported as unresolved.
- **Diagram type is detected from content**, like PlantUML: C4 macros, activity syntax (new and
  legacy), state, class, object, use case, deployment and component keywords are scored, and
  sequence wins only when its own constructs dominate (since `actor` or `database` appear in
  several types). Timing, network (`nwdiag`), gantt, JSON, YAML, salt and other block kinds are
  reported as unsupported; mindmap and WBS become trees.
- **UML in the model:** classifiers are elements of kind `class` or `object`, with
  `stereotype`, `uml.attributes` and `uml.methods` properties (one member per line) drawn as
  compartment boxes. Relationship kinds become arrowheads in the relationship style
  (generalisation hollow triangle, composition filled diamond, aggregation hollow diamond,
  dependency dashed open arrow); multiplicities are `uml.sourceMultiplicity` and
  `uml.targetMultiplicity` properties drawn at the ends. `class A extends B implements C` adds
  the relationships. Packages and namespaces become groups.
- **State and activity diagrams become flow graphs**: start and end dots, decisions, fork and
  join bars (a new `bar` base shape), with composite states as parent elements and partitions,
  swimlanes and groups as groups. Arrow labels, `then (yes)` and `is (no)` become relationship
  labels.
- **Directions become hints.** `-up->`/`-down->` arrows, C4 `Rel_U/D/L/R` and `Lay_*` become
  `pin` hints; `left to right direction` and `LAYOUT_LEFT_RIGHT()` set the view direction;
  `hide`/`remove` of an element leaves it out of the view. Hidden arrows (`-[hidden]->`) become
  pins without a relationship.
- **Styling is counted, not applied**, except C4 `AddElementTag` colours, border and shape,
  which become element styles. `skinparam`, `<style>` blocks, sprites, legends and themes are
  counted in the report (`unknownStyles`).
- **Never fail.** Every line that is not understood is reported with its line number and a
  `construct` name (for example `sequence:arrow`), which the coverage script counts.

## Structurizr import

- **A workspace IR, then one builder.** The DSL parser and the workspace JSON reader both produce
  the same intermediate workspace (elements, relationships, views, styles), and one function
  turns it into a DiaDraw model. Element keys follow Structurizr identifiers (hierarchical
  `a.b.c` keys, flat or hierarchical `!identifiers` for lookups, case-insensitive like
  Structurizr; canonical names such as `Container://S.C` also resolve).
- **The DSL as published, without running code.** Constants and variables, `!include` of files
  and directories, `workspace extends` (DSL or JSON), groups (nested, identified, and `group`
  inside an element), archetypes (named, chained and per-type defaults, for elements and
  relationships), `!element`/`!ref`/`!extend`, `!elements` bulk blocks, `instanceOf`, tags,
  properties, perspectives, url, `!docs`/`!adrs` (kept as properties) and text blocks are
  handled. `!script` and `!plugin` are reported, never run; `!relationships` bulk blocks and
  `-/>` are reported.
- **Nothing is fetched.** Remote `!include`, `workspace extends <url>` and themes are reported as
  unresolved. Local includes go through a resolver supplied by the caller (the CLI reads files
  relative to the including file; the web app has none).
- **Elements map by type**: person → `person`, softwareSystem → `system`, container →
  `container` (or `database` for a cylinder), component → `component`, deployment and
  infrastructure nodes → `system` with `shape cube`, instances → copies of what they instantiate
  with an `instanceOf` property, nested in their node. Instances inherit the relationships of
  what they are instances of within their environment (the DSL's default), marked
  `replicated`. Structurizr shapes map to DiaDraw shapes where one exists.
- **Implied relationships are not stored.** DiaDraw already lifts relationships of hidden
  descendants to their visible ancestors when it resolves a view, which is what Structurizr's
  default implied-relationship strategy produces. `!impliedRelationships false` is therefore not
  honoured: views still show lifted edges.
- **Every view type becomes a view.** systemLandscape and custom views cover the model;
  systemContext, container and component views are scoped to their subject; filtered views
  copy their base view with tag filtering applied; deployment views select their environment.
  `include`/`exclude` expressions cover `*`, identifiers, group identifiers,
  `element.tag==`/`element.type==` and `->x->`.
- **Dynamic views become sequence views.** Structurizr renders dynamic views as collaboration
  diagrams, but DiaDraw's sequence view is the closest view type that keeps the numbered,
  ordered steps: each step is a message, `autonumber` is on, and parallel blocks (or JSON orders
  such as `1.1`) become `par` frames. The relationships stay in the model.
- **Workspace JSON keeps its layout.** Element positions become a `layout fixed` view with
  `pinAt` hints (sizes from the style's width/height or Structurizr's defaults) and relationship
  vertices become `route` hints, unless the view uses automatic layout or the import is run with
  `geometry: 'drop'` (`--geometry drop`). `autoLayout` direction maps to the view direction and
  rank/node separation to spacing.
- **Styles apply by tag**, including the implicit type tags (`Element`, `Person`,
  `Software System`, …), in Structurizr's order: background, colour, stroke, border, opacity,
  font size, shape and icon for elements; colour, thickness, dashed/dotted style, routing and
  opacity for relationships. The raw style is kept as a `structurizr.style` property and every
  unmapped property is counted in the report. Light/dark colour-scheme styles merge with the
  default ones.
- **Never fail.** Anything not understood is reported with its line number and a `construct`
  name; image views, animation steps and themes are reported, not imported.

## Libraries

- **Documents name libraries; hosts resolve them.** `use "name@range" as alias` is part of the
  model (`Model.libraries`), so it prints, round-trips, patches in place and undoes like any
  other edit (`useLibrary`/`unuseLibrary` operations). `shape alias.id` stays a string on the
  element: the model does not depend on a library being present, and an unresolved reference
  draws as the element kind's shape.
- **Offline by construction.** Resolution reads the lockfile, the cache and the built-ins only;
  `@diadraw/library` keeps everything that touches the file system or network in its `./node`
  entry. `lib add` and `lib install` are the only network users, for URLs given to `add` or
  pinned in the lockfile, with sha256 integrity checks. No registry: a library is a folder, a
  `.ddlib` file or a URL, so nothing is published anywhere.
- **One bundle format.** A built library is one JSON object (SVG inline and sanitised, fragments
  inline); `.ddlib` is that JSON gzipped. Integrity hashes cover the canonical JSON, so a
  re-serialised bundle keeps its hash.
- **Semver subset written in-house** (caret, tilde, x-ranges, comparators, hyphen ranges, `||`,
  pre-releases) instead of a dependency: the package must also run in the browser.
- **Composites and fragments.** A composite is drawn, not modelled: one element whose drawing
  places other shapes in a box. A fragment is source text with `${param}` placeholders,
  expanded when inserted, so the document never depends on the fragment afterwards.
- **Licence checks gate shipping.** Libraries are `allowed` (permissive allow-list, or the
  project's own work), `restricted` (allowed licence plus extra terms), `disallowed` or
  `unknown`. Only `allowed` ones become built-in. draw.io's stencils (Apache-2.0 plus a
  restriction on Atlassian products, plus vendor trademark terms) are `restricted`: DiaDraw
  converts them on request (`lib build --drawio`) but does not ship them. (Superseded: they
  now ship as restricted built-ins; see "draw.io stencils as built-in libraries".)
- **Mappings live in libraries.** The PlantUML importer's icon-library recognition (include
  patterns, vendored paths) is read from the built-in `plantuml-*` libraries, and icon macros
  import as `shape <set>.<Macro>` with a `use`. The `c4` library maps the C4-PlantUML macros
  and Structurizr shapes to its shapes for tools; the C4 importers keep emitting C4 kinds
  (`person`, `system`, `container`, …) so imported C4 diagrams keep their box style.
- **Library drawings render as icons**: the drawing at the top of the node, the label
  underneath, sized from the library's width and height (at most 96×64). Base-shape library
  entries draw as that base shape with the label inside.

## draw.io stencils as built-in libraries

- **Shipped, restricted: Andriy's decision (2026-11-03).** All 204 of draw.io's stencil sets
  (9,088 shapes) ship as built-in `drawio-<set>` libraries, although the licence check marks them
  `restricted`: draw.io is Apache-2.0, but its stencils may not be used in, distributed for use
  with, or incorporated into Atlassian products or the Atlassian marketplace without written
  permission, and the vendor sets (AWS, Azure, GCP, Cisco, IBM, …) are subject to the vendors'
  trademark terms. Each manifest carries that text as `licenseNotes`; `assets/NOTICE`
  holds the attribution, the restriction, the vendor note and the Apache-2.0 text;
  `diadraw lib list`, the web palette and the import report say "restricted". DiaDraw itself is
  not an Atlassian product, so it may ship them; anyone building DiaDraw into one must not.
- **Pinned and reproducible.** `tools/gen/drawio_stencils.mjs` regenerates
  `assets/drawio/` from draw.io 32.6.9 (commit `3e707fd2`), checking the SHA-256 of
  `stencils.min.js`; downloading or reading a local checkout gives byte-identical output.
- **Compact and lazy.** Each set is a gzipped bundle (4.31 MB for all sets, the largest
  `drawio-aws4` at 703 KB) plus an 88 KB index of sets and draw.io style names. Nothing is
  loaded up front: hosts install a loader (`setDrawioLoader`: the file system in Node, asset URLs
  in the web app) and `preloadLibraries(model.libraries)` fetches the sets a document uses before
  the synchronous resolver runs. The web app's main chunk grew by 30 KB (6.5 KB gzipped) for the
  URL list, loader and palette; the sets are 205 separate assets, never inlined.
- **Drawn in the cell's colours.** The converter writes placeholder colours (`#fffffe` fill,
  `#000001` stroke, `#374152` glyph fill) and libraries declare them in `colors`; the renderer
  swaps them for the element's fill and stroke, as draw.io applies a cell's style to its
  stencil. Tile icons (AWS `resourceIcon`/`productIcon` with `resIcon`/`prIcon`, Cisco 2019
  `cisco19.rect` with `prIcon`) draw the glyph in the stroke colour on a tile of the fill colour
  (`iconBackground`). Styled elements draw the stencil filling the cell (`aspect=fixed` keeps
  proportions) with the label where the style puts it.
- **The draw.io importer maps stencils to the libraries** (adding the `use`) whenever the index
  is available; `stencils: false` keeps icon boxes. Over the draw.io corpus, 2,638 of 3,583
  stencil shapes now draw with their artwork; the other 945 are shapes draw.io draws in code
  (floor-plan walls and doors, AWS groups, arrows, mock-ups, 3D AWS servers), not stencils.

## BPMN import and export

- **BPMN elements become model elements with BPMN kinds** (`event`, `task`, `subprocess`,
  `gateway`, `data`, `annotation`, `group`, `pool`, `lane`, `conversation`) and shapes from the
  built-in `bpmn` library, whose shape names encode what BPMN draws: event position and marker
  (`intermediate_catch_non_interrupting_event_timer`), activity type and loop/multi-instance/
  compensation marker (`user_task_mi_sequential`), gateway kind. The importer adds
  `use "bpmn@^1"`.
- **Containment is nesting**: pools contain their process's lanes, lanes (innermost) their flow
  nodes, sub-processes their contents. Boundary events are siblings of their activity with
  `bpmn.attachedToRef`, positioned by DI, rather than children (which would make every activity
  with a boundary event a container).
- **Each BPMN diagram plane is a fixed view**: shapes become `pin … at … size`, waypoints become
  `route` hints with exit/entry points relative to the shapes. A collapsed sub-process is a leaf
  (the shape with the + marker) in its parent's diagram and its contents are its own view, as
  bpmn-js does with drill-down planes. Label bounds are not kept: library drawings put labels
  under the shape. Without DI (or with `geometry: 'drop'`) one view is laid out left to right.
- **Round trip through properties.** What DiaDraw has no field for is kept as `bpmn.*`
  properties: the BPMN type and id, standard attributes, event definitions (with ids, refs, timer
  expressions, conditions), loop characteristics, scripts, conditions and default flows, vendor
  attributes (`bpmn.attr.camunda:assignee`) with their namespaces (`bpmn.xmlns.camunda`), and
  extension elements as raw XML. The exporter writes all of it back; elements drawn in DiaDraw
  are typed from their `bpmn.*` shape or their kind. Over the corpus, export then import gives
  the same element and flow types for every file.
- **Flows** map to relationships styled as BPMN draws them: sequence flows with a filled arrow,
  a slash at the start of default flows and a diamond on conditional flows from activities;
  message flows dashed with a hollow circle and hollow arrow; associations dotted; data
  associations dotted with an open arrow. The renderer gained `dashPattern` and a `dash` marker
  for this, horizontal swimlanes (`horizontal=0`: header down the left with the name turned) for
  pools and lanes, and library shapes whose label sits inside (task boxes).
- **Not imported**: associations to flows (a relationship cannot point at a relationship), data
  associations to process properties, DMN and CMMN (not cheap: separate metamodels and
  notations), choreography participant bands (choreography activities import as tasks).

## Sequence view type

- **Steps live in the view.** `layout sequence` plus a `sequence { … }` block: messages, activation,
  notes, frames with `else` sections, dividers, delays, refs and `autonumber`. Participants are
  the view's included elements, in `include` order. Messages are not model relationships, so
  one interaction can be told in several views without duplicating the model.
- **Deterministic layout** (`sequenceLayout`): columns spaced so message labels fit, every step
  a fixed amount below the last, activation bars nested by depth, frames sized to the
  participants they involve. The geometry carries a `sequence` part that the renderer draws
  (lifelines, frames with their tag and guards, activations, messages styled by kind, notes,
  double-line dividers, delays and ref boxes).

## One C++ core everywhere

- **The TypeScript engine is gone.** Every host runs the C++ core: native shells and the CLI
  directly, JavaScript hosts (the web app, VS Code, the Tauri app) through one WebAssembly module,
  `@diadraw/wasm`. JavaScript keeps only glue: the typed wrapper, Node's file system and LSP
  framing for VS Code, and the apps' UI code.
- **An event-based WebAssembly interface.** Pure work stays as plain calls (`parse`, `print`,
  `importDiagram`, …). Long-lived objects report what happens instead of only returning it: a
  `Session` emits `change` and `libraries`, a `LanguageServer` emits `message`. The events come
  from the core (`dd::editor::Session`'s listener, `dd::lsp::Server`'s send callback), so native
  shells can use the same ones. One listener per object crosses the boundary; the wrapper fans
  out to `on(type, listener)` subscribers.
- **The language server has no transport.** `dd::lsp::Server` takes and sends JSON-RPC messages;
  `diadraw lsp` frames it over stdio, and VS Code runs it inside the WebAssembly module, so the
  extension ships no native binary.
- **Library resolution reads through the host.** `dd::library::DiskLibraries` (lockfile, cache,
  draw.io sets) moved from the native host into the core and reads through a `FileSystem` the
  host provides: `std::filesystem` natively, Node's `fs` from WebAssembly (`setFileSystem`).
  Documents resolve the same libraries in the CLI, the language server and VS Code previews.
- **Parity checks retired with the reference.** Their last results are recorded in
  `src/README.md`. What still guards behaviour: the googletest suites, snapshots of the SVG of
  every sample view in `src/wasm`, the highlighter check against the web editor's tokenizer
  (`parity-highlight.mjs`) and import coverage (`import-coverage.mjs`). The generated tables
  (built-in libraries, C4-PlantUML signatures, draw.io stencil sets) are regenerated by the core
  and came out byte for byte the same.
- **Not carried over:** the remark and rehype plugins of `@diadraw/markdown` (nothing in the
  repository used them; the VS Code preview renders through the core instead), and the
  TypeScript layout adapter for ELK.

## Element kinds from libraries

- **Plugins that add element kinds are libraries.** A manifest's `kinds` declares them, next to
  the shapes they draw with, and they travel the same way: `use`, the lockfile, the cache, the
  built-ins. No second extension mechanism, nothing executable, and the WebAssembly module and
  native hosts resolve them identically. A kind is data (a shape and colours), so code-defined
  kinds (custom layout or rendering) would need a different mechanism; nothing asks for one yet.
- **Library kinds are always qualified (`acme.service`).** It keeps parsing independent of
  library resolution (the parser never needs the libraries to know what a name means), and two
  libraries, or a library and a later built-in, cannot claim the same kind. Unqualified aliases
  for kinds could come later as an opt-in per `use`.
- **The model stores the qualified kind as written.** Like `shape alias.x`, the alias is
  document-local; tools that read the model JSON resolve it through `libraries`.
- **Checking a kind is the host's job.** The parser only checks that the alias is declared;
  whether the library has the kind is reported where libraries are resolved (language server,
  CLI), next to the unknown-shape warning (`library::unknown_references`).
- **Renderer: a `KindResolver` beside the `ShapeResolver`.** Kinds and shapes are separate
  namespaces in a library (`c4.person` may be both), so they resolve separately.
  `LibraryArt` carries both; it converts from a bare `ShapeResolver`, so existing session
  providers keep compiling.

## Styles and themes

- **Blocks restyle the theme, inline styles replace it.** A `style` block changes only the keys
  it sets, so a `container` block that sets `fill` keeps the theme's stroke, text colour and bold
  label. An inline `style "…"` keeps its meaning from draw.io imports: it switches the item to
  draw.io's defaults for what it leaves out. That way imported drawings render as before and a
  block never makes a model element look imported.
- **Specificity, not only order.** `*` < kind < tag < kind and tag, then document order within a
  step, then the inline style; close to CSS, and independent of where a block sits in the file.
- **Themes are built in and named.** `default`, `dark` and `mono`; a document picks one with
  `theme`, a view may override it. The theme travels with the resolved view, so every host (CLI,
  WebAssembly, the editor session, native shells) draws it without new render options. Documents
  without `theme` or `style` render byte for byte as before (checked on every corpus view).
- **Values are words, numbers or strings.** `endArrow open`, `strokeWidth 2`, `fill "#438dd5"`;
  colours need quotes because `#` is not a token. `true`/`false` are stored as `1`/`0` so the
  renderer and the draw.io exporter read one form.
- **Themes users declare are data, not code.** `theme brand extends dark { … }` and a library's
  `themes` share one shape (keys plus `node kind { fill stroke text }`), stored in the model and in
  bundles and turned into a renderer theme only when drawing. Library themes resolve through the
  host, like library shapes, so documents stay offline-safe. Built-in names cannot be redeclared:
  a document that says `theme dark` always means the same thing.
- **Font sizes in a theme change layout.** Hosts measure a view with its theme, so a theme with
  a larger `fontSize` gets larger boxes, not overflowing labels.

## Resizing and bend points in fixed views

- **Bend points only in `layout fixed` views.** Elsewhere routes come from the layout. (Resizing
  now works in every view; see "Size and rotation in every view" below.) A fixed view ignores relative hints, so a drag there always
  writes `pin … at` (as Alt does elsewhere) and keeps the size the pin had.
- **A canvas edit rewrites its hint in place.** A new `replaceHint` operation (its inverse is
  another `replaceHint`) lets a resized or moved node's `pin … at` and a rerouted edge's `route`
  change where they stand, keeping the trailing comment and the subject as written (`pin web`
  in a scoped view, an escaped route id); only the coordinates are reprinted. A drop that changes
  the sort of hint (a `pin … below` becoming an alignment) still removes the old one and appends.
- **The canvas reports all bend points.** A drag sends the edge's inner points with one moved,
  added or removed; the session writes them as `via`, keeps `from` and `to`, and removes a route
  left with nothing in it. The shared C++ canvas and the web canvas implement the same handles,
  so every shell behaves alike.

## More layout algorithms

- **Unknown names warn, then run layered.** `layout <name>` keeps any name (a later version may
  know it), but the DSL reports a warning on it listing `kLayoutAlgorithms` (model.hpp), which the
  language server also completes from. Before, every unknown name fell back to layered silently.
- **`tree`, `grid`, `radial` and `force` share one frame** (`src/layout/src/placement.cpp`). Like
  the layered engine, each container is laid out on its own, innermost first, with edges lifted
  to the children that hold their ends, then sized around its children (padding 32 on top, 16
  elsewhere). An engine only places one level's boxes; the frame routes edges (orthogonal for
  tree and grid, straight for radial and force, orthogonal wherever an edge has ports or an
  orthogonal `edgeStyle`), clips them to outlines and runs the constraint pass for missed hints.
  Stability across edits (`previous`) stays a layered feature.
- **Deterministic everywhere.** No randomness: force starts from the grid with a fixed offset per
  node and runs 300 rounds with linear cooling; radial and force round coordinates to hundredths,
  so platform differences in `sin`/`cos` do not reach the SVG.
- **Choices that are deliberately simple.** The tree is a band-per-subtree tidy tree (not a
  contour-merging Reingold–Tilford), roots and children in view and edge order; grid uses
  ⌈√n⌉ per line in view order and ignores edges; radial roots at the best-connected node and
  gives each branch a wedge proportional to its leaves. Direction applies to tree and grid only.
- **Existing views are unchanged.** `layered`, `fixed` and `sequence` take the same code paths;
  every corpus view renders byte for byte as before.

## Size and rotation in every view

- **Per view, as hints.** `size a W, H` and `rotate a D` are view statements (`SizeHint` and
  `RotateHint`), like `pin` and `route`: positions, sizes and routes are geometry, and geometry
  belongs to a view, so the same element can be large in a detail view and small in a context
  view. The canvas writes them; dragging a node never touches them (they do not place it).
- **Auto layouts take a size as given.** `to_layout_graph` uses the `size` instead of the
  measured size, so every engine (layered, tree, grid, radial, force) arranges the view around
  it, and sequence views use it for the participant's head. A container's size still comes from
  its children. In a fixed view a `pin … at … size` wins over a `size`; resizing there keeps
  writing the pin, so a fixed view stays one line per box.
- **A turned node takes the room of its turned shape.** `Geometry.nodes` keeps the unturned box,
  and `Geometry.rotations` holds the turn (degrees clockwise about the box centre, in (-180, 180]).
  Before an auto layout the engine gets the turned shape's bounding box as the node size, and the
  box is centred in the slot afterwards; a `pin … at` on a turned node is moved by as much, so
  the box lands where the pin says. Lines meet the turned outline: `clip_to_outlines` and the
  fixed layout work in the node's own frame, and a fixed route's `from`/`to` turn with the box
  (as draw.io's constraints do).
- **Leaves only.** Containers are not turned (their children would have to turn with them), nor
  are sequence participants; such a `rotate` is reported in `ignored_hints`. Decorations (notes,
  text, boxes) are not turned either.
- **Rendering.** A turned node's `<g class="dd-node">` gets `transform="rotate(D cx cy)"`, so its
  label turns with it, as in draw.io, and the native display lists already apply SVG transforms.
- **Canvas.** The shared C++ canvas resizes in every view (leaves only outside fixed views, since
  layouts size containers) and has a rotate handle off the top-right corner; a turn snaps to
  15° steps within 5°, otherwise whole degrees. Resizing a turned node works in its own frame
  and keeps the opposite corner where it is on screen. Shift keeps the proportions: one factor for
  both sides (the larger the pointer asks for, so the box reaches it), not snapped to the grid,
  since grid steps rarely keep a ratio. The web canvas has the same handles; their geometry is in
  `src/apps/web/src/canvas-geometry.ts`, tested with the C++ canvas's cases, so both write the same.
  It also has the grid, snapping as the native shells do; the setting (shown, step) is kept in the
  browser's storage, as macOS keeps it in user defaults, never in the text. The grid is an SVG
  layer under the drawing, spread over the whole canvas, with the drawing's background moved
  under it.

## Selecting and moving several elements

- **A group move pins every box.** Dragging one of several selected boxes moves them all by the
  grabbed box's offset and writes `pin … at` for each, as one batch (one undo step), whatever the
  layout. Placing a whole group by its neighbours (Option/Alt) has no clear meaning, so a group
  always lands where it is let go. In a fixed view, lines whose two ends both move keep their
  bend points relative to them, and a container without its own `pin … at` is left unpinned when
  all its drawn children move (it wraps them where they land), or pinned at its drawn size when
  some stay.
- **One text pass per kind of change.** `move_operation` plans against the view as it stands:
  rewrites first (they keep every index), then removals last first, then appended hints.
  `update_source` patches each such run against one parse (rewrites of different hints, removals
  in falling order, hints appended to one view as one insertion), so a group move parses the text
  a few times, not once per box. A 1000-box move went from 12 s to under 100 ms (release build),
  whether the boxes were pinned, unpinned or placed by relative hints.
- **A move that rounds to nothing leaves existing pins alone.** A fraction of a unit at a high
  zoom does not rewrite an element's own `pin … at` or a route, so a group that is already pinned
  records no empty undo step; boxes placed by the layout still get their pins.
- **The band takes what it wholly encloses**, nodes by their turned bounds and explicit lines by
  every route point, so pressing inside a container never selects its siblings by accident.
  Shift or Command/Ctrl adds the band to the selection; a click on one of several selected
  boxes, without a drag, selects it alone.
- **Every canvas.** The shared C++ canvas and the web canvas (`src/apps/web/src/canvas.ts`,
  through the WebAssembly session's `move`) select and move several elements alike; WinUI lacks
  Ctrl-A and the scripted test.

## Website and GitHub Pages

- **Starlight on Astro** for the website (`site/`): documentation navigation, search (Pagefind),
  dark mode and code highlighting come with it, and pages are Markdown, like `docs/`.
- **`docs/` stays the one source.** The reference pages are copied from `docs/dsl.md`,
  `docs/diadraw.grammar`, `docs/libraries.md` and `docs/guide/` before every build, with links
  rewritten to the site's pages or to the repository on GitHub; only the getting started,
  tutorial, feedback and contact pages are written for the site.
- **The tutorial is checked by the engine.** Its documents are `.draw` files that the Pages build
  parses (any diagnostic fails it) and renders with the WebAssembly core, and each step opens in
  the app through a share link built at build time, so the tutorial cannot drift from the DSL.
- **One Pages deployment** holds the site at the root and the web app under `app/`, both built
  for the address Pages reports (custom domains included), by `tools/pages/build.mjs` in the
  `pages.yml` workflow; the build fails on any broken local link.
- **The site keeps its own dependencies.** Astro needs Node 22 and the workspaces support Node
  20, so `site/` has its own lockfile and is not an npm workspace.

## Google Drive and OneDrive

- **One dialog for every place a diagram can live.** The web app's browser library and the two
  clouds are tabs of one Storage dialog behind one `StorageLocation` interface; the native apps
  have the same dialog with the cloud tabs (they already keep diagrams in files).
- **The narrowest scopes.** Google's `drive.file` (files DiaDraw created) and OneDrive's
  `Files.ReadWrite.AppFolder` (Apps/DiaDraw): neither needs a security review, and DiaDraw never
  sees other files. Both the web and native apps use the same scopes and folders, so a diagram
  saved in one opens in the others.
- **Browser sign-in without libraries.** A popup and a static `oauth.html` that hands the redirect
  back through `postMessage` and a `BroadcastChannel` (providers' Cross-Origin-Opener-Policy can
  cut the popup off from its opener). Google uses the token flow, since a "Web application"
  client cannot redeem a code without its secret; Microsoft uses the code flow with PKCE as a
  single-page app. Tokens stay in the tab's sessionStorage.
- **Native sign-in in shared C++.** `host/cloud.hpp` follows RFC 8252: the system browser, a
  one-shot listener on 127.0.0.1 and PKCE, then plain REST over the platform's HTTPS client
  (libcurl on macOS and Linux, WinHTTP on Windows), so each shell only adds its dialog. Calls block
  on a background thread and post their result to the UI thread; tokens stay in memory.
- **Client IDs are configuration.** Whoever ships DiaDraw registers it with Google and Microsoft
  and builds the IDs in (`VITE_*` for the web app, CMake cache variables for the native apps); a
  build without them shows the service as not set up.

## Rust bindings

- **Over the C ABI, not the C++ API.** The Rust crates (`src/rust`) call `diadraw.h`, which grew
  to ABI 2 for them: parse, check, format, layout, render and import, each mirroring the
  `@diadraw/wasm` function and the CLI command of the same job. A C boundary needs no C++ binding
  generator, keeps exceptions out of Rust, and serves any other C caller too. ABI 2 lives in its
  own library, `diadraw_capi`, because it needs the host and export libraries that themselves
  depend on `diadraw`.
- **Built from source by Cargo.** `diadraw-sys`'s build script runs CMake on the checkout and
  links the static libraries the build lists in `diadraw_capi_libs-<config>.txt`, so it never
  hard-codes the core's dependencies. Release C++ with the release C runtime DLL on Windows,
  since Rust always links that one.
- **JSON for structured values**, in the TypeScript shapes, as the WASM module does; typed Rust
  structs can come later without changing the ABI.
- **One lock around the core.** The core keeps a few caches without locks, so the safe crate
  serializes every call; the core is fast enough that this costs little.

## Node.js bindings

- **Node-API over the C++ core, not the C ABI.** `@diadraw/node` (`src/node`) is a node-addon-api
  addon that calls the core and `host/` directly, like the WebAssembly bindings do, so it needs
  nothing added to `diadraw.h`; the C ABI grows for hosts that can only call C. Node-API version
  8 keeps one binary working across Node versions and in worker threads.
- **The same values as `@diadraw/wasm`.** Models, operations, diagnostics and import results
  cross as JSON in the shapes of `@diadraw/wasm`'s types, which the package re-exports, so code
  moves between the browser module and the addon; the addon throws where the module would.
- **Libraries resolve like the CLI's.** The addon works on files, so it goes through
  `host/diagram.hpp`: the lockfile next to the document, the cache, the built-ins and draw.io's
  sets, offline. That layer keeps process-wide state, so calls into it take one lock.
- **Built by cmake-js from the repository root** with tests, the CLI and the apps off, into
  `diadraw-build/node`; the static libraries are built position-independent when the addon is on.
  The package builds from the repository for now; prebuilt binaries come with publishing it.

## draw.io editor for Confluence

- **A second Forge app, not a second macro.** `src/apps/confluence-drawio` is its own app, so a
  site can install the draw.io editor without DiaDraw's WebAssembly core (and the `unsafe-eval`
  script policy it needs), and the DiaDraw app does not have to ask for an external frame.
- **draw.io bundled with the app, in a frame.** The editor is draw.io itself (Apache 2.0),
  unchanged, from the `draw.war` of a pinned release (checked by SHA-256), driven through its
  embed mode's JSON message protocol. Bundling keeps the diagram and the code that edits it on
  Atlassian's servers, with no external frame for administrators to approve; it costs about
  29 MB (zipped) per deploy and moving to new draw.io releases by hand. Leaving out the server
  side, sources, maps, the viewer and other integrations' bundles takes a third off. A build with
  `DRAWIO_URL` loads draw.io from embed.diagrams.net or a self-hosted copy instead. The app says
  "draw.io editor" to describe what it opens and does not use draw.io's logo or name as its own,
  since it is not a JGraph product.
- **One editable SVG stored with the macro.** Save asks draw.io for `xmlsvg`: the drawing with the
  whole `.drawio` file (every page) in its `content` attribute. The page shows that SVG as an
  image without loading draw.io, the editor opens the same SVG again, and the SVG download is a
  file draw.io can open. It is stored in the macro's configuration (deflated and Base64-encoded)
  rather than in Forge storage or an attachment, so it is copied, versioned and exported with the
  page like any other macro and the app needs no scopes. The image is pinned to the page's light
  or dark theme, since draw.io draws default colours with CSS `light-dark()`.
