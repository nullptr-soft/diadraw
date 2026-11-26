# Layout benchmark

> `@diadraw/bench` was removed on 2026-11-19, so the commands below no longer run. The numbers
> are kept as a record. The C++ engine against ELK is in
> [src/layout/bench/c4-vs-elk.md](../src/layout/bench/c4-vs-elk.md).

`@diadraw/bench` lays out every view in [`corpus/`](../corpus) plus seeded synthetic flowcharts
of 50, 100 and 200 nodes, and scores each layout:

| metric        | meaning                                                                                                                                   |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| crossings     | proper crossings between segments of different edges                                                                                      |
| bends         | bend points over all edges                                                                                                                |
| area          | bounding box of the drawing, px²                                                                                                          |
| edgeLength    | total length of all edge routes, px                                                                                                       |
| labelOverlaps | edge labels overlapping a node                                                                                                            |
| unmetHints    | layout hints (`pin`, `align`, `order`, `pin … at`) the result does not satisfy                                                            |
| drift         | after a small seeded edit (one new node, one new edge), the mean distance the unchanged nodes moved, after removing the overall shift, px |
| orderFlips    | after that edit, the share of node pairs whose left/right or above/below order flipped, %                                                 |
| timeMs        | median of three layout runs after a warm-up                                                                                               |

Run it with `npm run bench`. Compare against a saved run with
`npm run bench -- --compare results/m1-baseline.json`; `--engine elk-m1` reproduces the M1 engine
(no stability, no constraint pass).

## M4 against M1

`packages/bench/results/m1-baseline.json` is the M1 engine, re-run when the stability columns were
added (every other number matched the original M1 run exactly). `results/m4.json` is the current
engine. Deltas are M4 minus M1.

| case                                | nodes | edges | crossings |    bends |               area |    edgeLength | labelOverlaps | unmetHints |            drift |     orderFlips |         timeMs |
| ----------------------------------- | ----: | ----: | --------: | -------: | -----------------: | ------------: | ------------: | ---------: | ---------------: | -------------: | -------------: |
| c4-01-shop-checkout/checkout        |     2 |     1 |         0 |        0 |     70149 (+34551) |      40 (-45) |             0 |     0 (-1) |                0 |              0 |   29.6 (+19.7) |
| c4-02-banking/context               |     5 |     5 |         0 |        8 |             179292 |           860 |             0 |          0 |     91.4 (-29.8) |    5.6 (-16.6) |   30.3 (+22.1) |
| c4-02-banking/containers            |     4 |     3 |         0 |        2 |             220038 |           629 |             0 |          0 |      16 (-115.6) |      0 (-45.5) |     38.3 (+33) |
| c4-03-event-platform/services       |     7 |     8 |         2 |       10 |             246595 |    1501 (-57) |             0 |     0 (-1) |             11.9 |              0 |   48.9 (+33.8) |
| c4-03-event-platform/everything     |     8 |     9 |         0 |       10 |             246937 |          1609 |             0 |          0 |            108.2 |           18.8 |    12.5 (+6.3) |
| c4-04-freeform-notes/pipeline       |     5 |     4 |         0 |        4 |             172333 |           799 |             0 |     0 (-1) |             21.6 |              0 |     6.8 (+1.7) |
| flow-01-login/login                 |     7 |     7 |         0 |        8 |             189630 |          1187 |             0 |          0 |               64 |           12.8 |     6.7 (+1.4) |
| flow-02-order-fulfilment/fulfilment |     9 |    10 |         0 |   7 (-1) |             323960 |    1894 (+67) |             0 |     0 (-1) |        30 (-9.5) |       0 (-9.7) |     8.1 (+2.1) |
| flow-03-ci-pipeline/pipeline        |    10 |    12 |         0 |       12 |             336876 |          1262 |             0 |          0 |             74.5 |              0 |    15.2 (+6.2) |
| flow-04-support-ticket/triage       |     9 |    11 |         0 |  14 (+2) |    359800 (+91467) |   1811 (+198) |             0 |     0 (-1) |    37.1 (-204.7) |      0 (-32.8) |   26.6 (+12.6) |
| synthetic/flow-50                   |    50 |    58 |        11 |       78 |            2471963 |         31263 |             0 |          0 |   996.4 (-117.8) |    32.5 (-1.9) |   34.9 (+11.9) |
| synthetic/flow-100                  |   100 |   127 |       125 |      210 |            8273360 |         90537 |             0 |          0 |  2024.6 (-563.4) |      30.8 (-2) | 226.9 (+185.4) |
| synthetic/flow-200                  |   200 |   239 |       478 |      396 |           22992437 |        340627 |             0 |          0 |           3088.9 |           20.9 | 197.5 (+117.1) |
| **total**                           |   416 |   494 |       616 | 759 (+1) | 36083370 (+126018) | 474019 (+163) |             0 |     0 (-5) | 6564.6 (-1040.8) | 121.4 (-108.5) | 682.3 (+453.3) |

**Times are not comparable in this table**: the M4 run happened while the machine was under load
(load average around 9). Run back to back under the same load, the M1 engine took 613 ms in
total and the M4 engine 514 ms, with the 200-node flow at 225 ms and 199 ms. Both are inside the
M1 gate of 500 ms for 200 nodes.

### What changed

- **Hints: 5 unmet → 0.** The constraint pass applies the hints ELK cannot express: `pin … at`,
  `align`, pins across layers, and `order` when model order is not enough. Moved nodes push
  overlapping nodes aside across the flow, containers refit, and the touched edges are rerouted by
  the new orthogonal router.
- **Stability: drift −14%, order flips −47%** in total, with no case worse than M1 and no crossings
  added. The big wins are on hand-written diagrams (triage −85% drift, banking containers −88%).
- **Quality otherwise unchanged.** Crossings are identical. The pinned and aligned cases trade a
  little area, length or one or two bends for honouring the user's intent (for example
  `c4-01`, where `pin db below web` turns a horizontal pair into a vertical one).

### What did not work, and what is next

- **ELK's interactive mode alone is not a good basis for stability.** Fed the previous coordinates
  it keeps nodes very still (drift −90% on the 50-node flow) but adds many crossings (256 instead
  of 23). Model order alone barely moves drift. The engine therefore lays out three ways (fresh,
  hybrid, interactive) and keeps the stillest one that adds no crossings and leaves no extra hint
  unmet. That guarantees "never worse than a fresh layout" but leaves stability on large graphs
  (200 nodes: no gain) to future work.
- **Next for layout** (tracked in the [backlog](backlog.md#layout)): our own incremental layering that keeps unchanged nodes in their layers,
  then ELK (or our own passes) for ordering within layers; routing all edges with the
  orthogonal router to compare with ELK's; and growing the corpus towards the planned few hundred
  real diagrams. A Rust/WASM core is still not justified by run time.
