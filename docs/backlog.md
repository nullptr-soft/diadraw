# Backlog

Everything the plan in `docs/` promises that `main` does not do yet, plus a few natural next steps
marked _beyond plan_. This is the single list of open work: other documents link here instead of
keeping their own open-point lists. A copy is kept in a Claude Doc; this file is the source.

Checked against `main` at `a46feb8` (2026-11-26, after PR #87). When an item lands on `main`, take it
off this list; the history of done work is in the git log and the pull requests.

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

- [ ] _Beyond plan:_ find references and code actions. (Formatting, rename and folding are
      done.)

## Apps and distribution

- [ ] **Update the WinUI README.** Its "Not yet built" section says the app has not been run, but
      the Windows thread reported that it runs and its two GUI tests pass. Confirm, then rewrite
      that section.
- [ ] **Windows ZIP and MSIX packages** (written, not built).
- [ ] **Build the WinUI app with the latest changes.** Since #4, many changes reached WinUI code
      or the shared canvas (among them rotate #9, the grid #17 and #19, views #31, splitters #37,
      multi-line names #60, the Help dialog #82, folding #83 and formatting #84), and CI, which
      would build it with the `windows-headless` preset, has not run since 2026-11-20 (see
      below). Build it on Windows and run its GUI tests.
- [ ] **Run the web app with the latest changes.** The web inspector and theme panel (#11, #12)
      and the Add drop-down (#4) were not run, because the web app needs the WASM module and the
      cloud has no emsdk (`npm run dev:web`).
- [ ] **Look over the new macOS panels by hand.** The inspector controls (#7, #8), the theme and
      styles panel (#10) and the End pop-up previews (#21, light and dark mode) were tested by
      script only, because the snapshot hook does not draw these controls.
- [ ] **Tauri on macOS**: needs Rust 1.77.2 or newer (the Mac has 1.68).
- [ ] **Publish the VS Code extension** (`vsce`, a publisher id).
- [ ] **Publish `@diadraw/wasm`** as an npm library (every package is private today).
- [ ] **Decide on the render server** that the README's vision still promises ("a render server
      later"): build it or take it out. (The Markdown plugins are done, #48.)

## Development workflow

- [ ] **Fix GitHub Actions billing.** Since 2026-11-20 every CI job fails without starting
      ("recent account payments have failed or your spending limit needs to be increased"), still
      on `a46feb8`, so neither CI nor the GitHub Pages site (#32) is built.
- [ ] **Let the cloud build the C++ core and the WASM module.** This session's cloud
      environment is still refused downloads from github.com and codeload.github.com (403), so
      `cmake --workflow --preset core` (#74) cannot fetch its dependencies; allow both hosts in the
      environment's network policy. There is no emsdk either, so `npm run typecheck`, `npm test`
      and the web app cannot run.
