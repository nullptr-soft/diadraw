# Backlog

Everything the plan in `docs/` promises that `main` does not do yet, plus a few natural next steps
marked _beyond plan_. This is the single list of open work: other documents link here instead of
keeping their own open-point lists. A copy is kept in a Claude Doc; this file is the source.

Checked against `main` at `bbb1ce5` (2026-11-21, after PR #21). When an item lands on `main`, move it to
"Recently done".

## DSL

- [ ] **Multi-file models** (`import`). Today `use` only loads libraries.
- [ ] **Lines and arrows between decorations** (notes, text, boxes).
- [ ] **Root-anchored references**, so an edit can always name an element. When a reconnected
      end's target is hidden by a nested name (inside `shop { … }`, a top-level `db` is hidden by
      `shop.db`), the file is reprinted instead of patched. A root-anchored form would keep that
      edit a minimal patch (from the reconnect work, `c840ea5`).

## Canvas editing

- [ ] **Drop an edge end onto a port.** Reconnecting on the canvas attaches the end to the element;
      ports can still be set only in the text.
- [ ] **Reparent**: move an element into another container.
- [ ] **Properties panel in the Windows (WinUI) app**, which does not have one yet, so it also has
      none of the inspector controls or the theme and styles panel the other apps gained in #7–#15.
- [ ] **Drag decorations**: moving a note, text or box on the canvas does not write its new `at`
      back to the text.
- [ ] **Sequence views on the canvas** (messages, frames, activations). Today they are text-only.
- [ ] **`order` hints from drags**. Today drags write only `pin` and `align`.
- [ ] **Real-time collaboration** on the operation log (planned for later).

## Layout

- [ ] **Incremental layering** that keeps unchanged nodes in their layers, for stability on large
      graphs. Today the previous layout only seeds the order (200 nodes: no gain, see
      [benchmarks.md](benchmarks.md)).
- [ ] **Fewer bends**: the one benchmark metric still worse than ELK (+0.8%).
- [ ] **Real font metrics** instead of approximate glyph widths.
- [ ] **A runnable benchmark** again (`@diadraw/bench` was removed; only saved results remain),
      and a larger corpus (planned: a few hundred real diagrams).
- [ ] **Route all edges with the orthogonal router** and compare with ELK's routing
      (from [benchmarks.md](benchmarks.md)).

## Rendering and export

- [ ] **PDF writer**: gradients, filters and images in library art.
- [ ] **PDF export in the web app.**
- [ ] **Export the `double` line style to draw.io** (#20 draws it but does not export it).
- [ ] **The 945 draw.io shapes drawn in code** (floor-plan walls and doors, AWS groups, arrows,
      mock-ups, 3D servers).

## Import

- [ ] **Mermaid beyond flowcharts**: sequence, class, state, ER and the other diagram types.
- [ ] **Mermaid per-subgraph direction.**
- [ ] **BPMN**: associations to flows, data associations to process properties, choreography
      participant bands.
- [ ] **DMN and CMMN** (separate metamodels; judged expensive).
- [ ] **draw.io edge styles on import**: an edge's `curved`, `rounded` and `dashPattern` (and
      draw.io's double line, `shape=link`) are dropped, so imported pages draw curves straight,
      corners sharp and every dash plain. DiaDraw draws and exports these keys since the line-ends
      work (#20); importing them changes how existing imported pages render, so it needs a decision
      and new corpus snapshots.

## Language server

The planned set (diagnostics, outline, go to definition, hover, completion) is done.

- [ ] _Beyond plan:_ find references, rename, formatting (the CLI already has `format`), code
      actions.

## Apps and distribution

- [ ] **Update the WinUI README.** Its "Not yet built" section says the app has not been run, but
      the Windows thread reported that it runs and its two GUI tests pass. Confirm, then rewrite
      that section.
- [ ] **Windows ZIP and MSIX packages** (written, not built).
- [ ] **Build the WinUI app with the latest changes.** Rotate (#9), drop-where-let-go (#13), the
      implied view (#16), the grid (#17, #19) and the Add drop-down (#4) changed WinUI code or
      the shared canvas, and none of them was compiled on Windows. Check Show Grid there too.
- [ ] **Run the web app with the latest changes.** The web inspector and theme panel (#11, #12)
      and the Add drop-down (#4) were not run, because the web app needs the WASM module and the
      cloud has no emsdk (`npm run dev:web`).
- [ ] **Look over the new macOS panels by hand.** The inspector controls (#7, #8), the theme and
      styles panel (#10) and the End pop-up previews (#21, light and dark mode) were tested by
      script only, because the snapshot hook does not draw these controls.
- [ ] **Tauri on macOS**: needs Rust 1.77.2 or newer (the Mac has 1.68).
- [ ] **Publish the VS Code extension** (`vsce`, a publisher id).
- [ ] **Publish `@diadraw/wasm`** as an npm library (every package is private today).
- [ ] **Decide on the README vision items** that the C++ port dropped: Markdown/docs plugins and a
      render server. Build them or take them out of the README.

## Development workflow

- [ ] **Fix GitHub Actions billing.** Since 2026-11-20 every CI job fails without starting
      ("recent account payments have failed or your spending limit needs to be increased").
- [ ] **Let the cloud build the C++ core and the WASM module.** Cloud sessions were refused
      downloads from github.com (403), so the C++ build could not run there (#13, #16), and there
      is no emsdk, so `npm run typecheck`, `npm test` and the web app cannot run either.

## Recently done

| Item                                                                                                                     | Commit or PR on `main`          |
| ------------------------------------------------------------------------------------------------------------------------ | ------------------------------- |
| More line ends and line styles on relationships, exported to draw.io, with a user guide chapter                          | #20, #21                        |
| C++ review commands and reviewer agents                                                                                  | #18                             |
| A grid on the canvas (macOS, GTK, WinUI), with snapping                                                                  | #17, #19                        |
| A canvas edit writes the implied view when the text has none                                                             | #16                             |
| A dragged box drops where it is let go, in every view                                                                    | #13                             |
| Inspector fields and the theme and styles panel use controls (pickers, colour wells, style blocks) on macOS, web and GTK | #7, #8, #10, #11, #12, #14, #15 |
| Resize and rotate any element in any view (shared C++ canvas)                                                            | #9                              |
| The macOS app comes to the front on launch                                                                               | #6                              |
| The properties panel stays visible whatever is selected (macOS, GTK, web)                                                | #5                              |
| Add elements from one Add drop-down in every app                                                                         | #4                              |
| The macOS app builds and passes its GUI tests with the styles panel, relationship inspector and reconnect                | built on the Mac for #7–#21     |
| Claude threads work through GitHub pull requests (`CLAUDE.md`)                                                           | `3916b4b` (PR #2)               |
| Reconnect an edge to another element from the canvas                                                                     | `c840ea5`                       |
| Tree, grid, radial and force layouts; unknown layout names warn                                                          | `335f722`                       |
| Edit relationships from the properties panel                                                                             | `26fd0ea`                       |
| Resize nodes and drag bend points in fixed views                                                                         | `91dbc8a`                       |
| Edit style blocks and themes from the canvas                                                                             | `c0ddfb5`                       |
| All apps build and test on Windows; the WinUI app runs (its two GUI tests pass); `.gitattributes` fixes line endings     | `a45ded4` (PR #1)               |
| Edit element properties from the canvas (properties panel), with a user guide chapter                                    | `75c8219`                       |
| Explicit relationship ids (`name = a -> b`), and a user guide chapter on relationships                                   | `834ce77`, `ed83871`            |
| View-level notes, text and boxes                                                                                         | `a44a4ad`                       |
| User-defined themes in documents and libraries                                                                           | `fb73a3e`                       |
| Style blocks and built-in themes                                                                                         | `914518a`                       |
| Libraries that add element kinds                                                                                         | `124cf4d`                       |
| Architecture diagram re-rendered without Lezer and ELK                                                                   | `4377904`, `d972ec0`            |
| `dsl.md`, README and `decisions.md` brought in line with the code                                                        | `71882ff`                       |
