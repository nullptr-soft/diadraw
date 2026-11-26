# Shape libraries

A library is a named, versioned set of **shapes** (drawings), **composites** (shapes drawn from
other shapes), **kinds** (element kinds the DSL does not build in), and **fragments** (pieces of
DiaDraw source to insert), plus **mappings** from the names other tools use. Libraries are how
DiaDraw is extended: a plugin is a library. Documents name the libraries they use; hosts resolve them. Rendering and
importing never touch the network.

```
use "c4@^1"
use "aws-icons@^2.1" as aws

model {
  user = person "User" shape c4.person
  fn = component "Handler" tech "Node" shape aws.lambda
  user -> fn
}
```

- `use "<name>[@<range>]" [as <alias>]` goes before the model. Without `as`, the alias is the
  last segment of the name with other characters turned into `_` (`@acme/cloud-shapes` →
  `cloud_shapes`). Aliases must be unique in a document.
- `shape <alias>.<shape>` refers to a shape or composite of the library. A reference the host
  cannot resolve draws as the element kind's usual shape, with a warning in `diadraw check`,
  `diadraw render` and the editor.
- `<id> = <alias>.<kind>` declares an element of a kind the library adds (see
  [Element kinds](#element-kinds)). An unknown kind draws as a box, with the same warning.
- Ranges are semver: `^1`, `~1.2`, `1.x`, `>=1.2 <2`, `1.2 - 1.6`, `^1 || ^2`, exact versions.

## Resolution

A `use` resolves, in order, to

1. the version `diadraw-lock.json` pins for that exact `name@range` (the nearest lockfile at or
   above the document), if that version is in the cache;
2. the highest version in the cache that satisfies the range;
3. the highest built-in version that satisfies the range.

The cache is `$DIADRAW_CACHE`, else `$XDG_CACHE_HOME/diadraw/libraries`, else
`~/.cache/diadraw/libraries`, holding one JSON bundle per version. Nothing here fetches: a
library that is in none of these places is reported as not installed. The web app has the
built-in libraries only.

## Commands

```bash
diadraw lib init shapes --name team-shapes   # manifest, an example shape and fragment, README
diadraw lib build shapes                     # check and build → shapes/dist/library.json
diadraw lib pack shapes                      # → shapes/dist/team-shapes-0.1.0.ddlib and its integrity
diadraw lib add shapes/dist/team-shapes-0.1.0.ddlib   # install into the cache, pin in diadraw-lock.json
diadraw lib add https://example.com/libs/aws-icons-2.1.0.ddlib
diadraw lib add c4@^1                        # pin a built-in (or cached) library
diadraw lib install                          # make the cache hold everything the lockfile pins
diadraw lib list                             # built-in and installed libraries
diadraw lib build --drawio stencils.min.js --set aws4,flowchart -o libs
```

`add` accepts a library folder, a `.ddlib` or bundle `.json`, an `http(s)` URL, or
`name[@range]` of a built-in or cached library. It prints the `use` line to paste. `add` and
`install` are the only commands that use the network, and only for URLs given to `add` or
written in the lockfile. `install` checks each bundle against the lockfile's `integrity`
(`sha256-…` of the canonical bundle JSON) and pinned version.

The lockfile, `diadraw-lock.json`, is meant to be committed:

```json
{
  "lockfileVersion": 1,
  "libraries": {
    "c4@^1": { "version": "1.0.0", "resolved": "builtin", "integrity": "sha256-…" },
    "team-shapes@^0.1.0": {
      "version": "0.1.0",
      "resolved": "libs/team-shapes-0.1.0.ddlib",
      "integrity": "sha256-…"
    }
  }
}
```

`resolved` is a path relative to the lockfile, a URL, `builtin`, or `cache:<name>@<version>` for
a library that was only ever in the local cache (which `install` cannot restore elsewhere).

## The manifest

A library's source folder holds `diadraw-library.json`:

```json
{
  "name": "team-shapes",
  "version": "0.1.0",
  "description": "Shapes for our services",
  "license": "MIT",
  "source": { "kind": "original" },
  "shapes": {
    "gateway": {
      "label": "API gateway",
      "category": "Edge",
      "kind": "component",
      "svg": "shapes/gateway.svg",
      "width": 48,
      "height": 48,
      "keywords": ["api", "ingress"]
    },
    "store": { "label": "Store", "base": "cylinder", "kind": "database" }
  },
  "kinds": {
    "edge": {
      "label": "Edge service",
      "description": "Anything that terminates client traffic",
      "shape": "gateway",
      "stereotype": "edge"
    },
    "topic": { "label": "Topic", "base": "hexagon", "fill": "#fde68a", "stroke": "#b45309" }
  },
  "composites": {
    "gateway_with_store": {
      "width": 120,
      "height": 48,
      "parts": [
        { "shape": "gateway", "x": 0, "y": 0, "width": 48, "height": 48 },
        { "shape": "store", "x": 72, "y": 0, "width": 48, "height": 48 }
      ]
    }
  },
  "fragments": {
    "edge": {
      "label": "Gateway in front of a service",
      "params": [{ "name": "service", "default": "api" }],
      "text": "fragments/edge.draw"
    }
  },
  "mappings": { "drawio": { "mxgraph.team.gateway": "gateway" } },
  "themes": {
    "corporate": {
      "extends": "default",
      "background": "#fafafa",
      "edge": "#4a4a4a",
      "nodes": { "system": { "fill": "#aa0000", "stroke": "#7a0000", "text": "#ffffff" } }
    }
  },
  "dependencies": { "c4": "^1" }
}
```

| Field             | Meaning                                                                                                                                                                                                  |
| ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`, `version` | npm-style name (lowercase, `-._`, optional `@scope/`), semver version                                                                                                                                    |
| `license`         | SPDX expression for the drawings; `licenseNotes` for terms beyond it                                                                                                                                     |
| `source`          | where the drawings come from: `original`, `drawio-stencils`, `plantuml-stdlib`, `svg`, `other`, with `url`, `commit`, `sets`                                                                             |
| `shapes`          | id (an identifier) → `svg` (file path or inline markup) or `base` (a renderer base shape), `label`, `category`, `kind` for new elements, size, `keywords`, `style`                                       |
| `composites`      | a shape drawn from `parts`, each a shape of this library (or `dep.shape` of a dependency) placed in the composite's box                                                                                  |
| `kinds`           | id (an identifier) → element kind: `shape` (a shape or composite of this library, or `dep.shape`) or `base`, `fill`, `stroke`, `fontColor`, `stereotype`, `label`, `description`, `category`, `keywords` |
| `fragments`       | DiaDraw source (a `.draw` file or inline) that goes inside `model { … }`; `${param}` takes a parameter, `${alias}` the alias the library is used under                                                   |
| `open`            | accept any shape name, drawn as `base` (for icon sets whose names are not listed)                                                                                                                        |
| `mappings`        | per format (`plantuml`, `plantuml-include`, `plantuml-local`, `structurizr`, `drawio`, `bpmn`), foreign name → shape id                                                                                  |
| `themes`          | id → a theme as in `theme … { … }` ([dsl.md](dsl.md#your-own-themes)): `extends` (a built-in, a theme of this library, or `dep.theme`), theme keys, `nodes`                                              |
| `dependencies`    | libraries composites build on, `name` → range                                                                                                                                                            |

A shape's or composite's `kind` (the kind of the element the palette adds for it) names one of
the library's own kinds when it has one by that name, else a built-in kind.

## Element kinds

The DSL builds in a few element kinds (`person`, `system`, `container`, `component`,
`database`, `process`, `decision`, `start`, `end`, `note`, and the UML and BPMN ones its
importers use). A library adds more under `kinds`, and documents use them through the alias:

```
use "team-shapes@^0.1" as team

model {
  lb = team.edge "Load balancer"
  orders = team.topic "Orders"
  lb -> orders
}
```

A kind gives its elements their look; everything else (labels, `tech`, children, ports,
relationships, views) works as for any element.

- **Shape.** `shape` draws them with a drawing of the library (sized and labelled like a `shape`
  reference); `base` with a renderer base shape (`hexagon`, `cylinder`, …). An element's own
  `shape` wins.
- **Colours.** `fill`, `stroke` and `fontColor` replace the theme's default node colours for the
  kind; an element's own `style` wins, and `external` elements keep the external colours.
- **Stereotype.** An element of the kind that has children draws as a boundary labelled
  `Name [stereotype]`.
- **Where they show up.** The language server completes `alias.` kinds after `=` and warns
  about kinds the library does not have (at the kind); `diadraw check` and `render` warn the same
  way; the web palette lists them under _Kinds_, and adding one declares an element of that
  kind.

Kinds stay qualified in the model (`team.topic`), so a document keeps them when it is printed.

`lib build` reads the SVG files, sanitises them (no scripts, event handlers, foreign objects or
external references; ids are prefixed per shape so icons on one page do not clash), inlines
everything into one bundle (`format: "diadraw-library"`, `formatVersion: 1`), and checks that
every fragment parses. A `.ddlib` is that bundle, gzipped.

## Licences

Every library carries a licence, and `lib build`, `lib add` and the built-in generator check it:

| Status       | When                                                                                                                                                                                    | Effect                                   |
| ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- |
| `allowed`    | every term of one alternative is on the allow-list (MIT, MIT-0, Apache-2.0, BSD-2/3-Clause, ISC, 0BSD, CC0-1.0, CC-BY-4.0, Unlicense), no extra terms; or original work of this project | can ship with DiaDraw                    |
| `restricted` | allowed licences with `licenseNotes`                                                                                                                                                    | warning; fine for local use, not shipped |
| `disallowed` | non-commercial, no-derivatives, copyleft (CC-BY-NC/ND, GPL, AGPL, LGPL, SSPL, BUSL) in every alternative                                                                                | `lib build` fails                        |
| `unknown`    | anything else                                                                                                                                                                           | warning                                  |

Only `allowed` libraries are built from `assets/builtin/` into the built-ins. The
one exception is draw.io's stencil sets, which ship `restricted` by decision (see below and
`docs/decisions.md`).

## Built-in libraries

Sources are in `assets/builtin/`; `node tools/gen/builtin_libraries.mjs` (after
`npm run build -w @diadraw/wasm`) builds them with the core and writes
`src/library/src/builtin_libraries.inc`, which the core compiles in.

- **`c4`**: person, software system, container, component, database, queue, web browser and
  mobile app, each also as an external variant where C4 has one; a "service with database"
  composite; "system context" and "web, API and database" fragments. Mappings: the
  C4-PlantUML macros (`Person`, `System_Ext`, `ContainerDb`, `ComponentQueue`, …) and the
  Structurizr shapes (`WebBrowser`, `Cylinder`, `Pipe`, …). Original artwork.
- **`drawio-*`**: draw.io's 204 stencil sets, restricted; see [draw.io stencils](#drawio-stencils).
- **`plantuml-aws`, `plantuml-azure`, `plantuml-gcp`, `plantuml-kubernetes`,
  `plantuml-tupadr3`, `plantuml-office`, `plantuml-cloudinsight`, `plantuml-logos`,
  `plantuml-material`, `plantuml-elastic`, `plantuml-osa`, `plantuml-cloudogu`, `plantuml-ibm`,
  `plantuml-archimate`**: the PlantUML standard library's icon sets as open libraries. Their
  `plantuml-include` and `plantuml-local` mappings are how the PlantUML importer recognises
  `!include <awslib/…>`, the usual URLs and vendored copies; an icon macro such as
  `Lambda(fn, "Handler")` imports as `shape aws.Lambda` with `use "plantuml-aws@^1" as aws`.
  The icons themselves are not included (their licences vary), so these shapes draw as icon
  boxes.

- **`bpmn`**: BPMN 2.0 shapes in original artwork: every event position (start,
  non-interrupting start, event sub-process start, intermediate catch, non-interrupting boundary,
  intermediate throw, end) with each marker BPMN allows there; tasks of every type, call
  activities, sub-processes, event sub-processes, transactions and ad hoc sub-processes, each
  plain and with a loop, parallel or sequential multi-instance or compensation marker; all
  gateways; data objects, collections, inputs, outputs and stores; messages, text annotations,
  groups, conversations and choreography tasks. Activity shapes keep their label inside. A
  "task with a timer" composite and "approval loop" and "task with a timeout" fragments. Its
  `bpmn` mapping gives the shape for each BPMN element type. Generated by
  `tools/gen/bpmn_library.mjs`.

## draw.io stencils

All of draw.io's stencil sets ship as built-in libraries named `drawio-<set>` (`drawio-aws4`,
`drawio-azure`, `drawio-cisco19`, `drawio-flowchart`, …): 204 sets, 9,088 shapes, from draw.io
32.6.9. Use them like any library:

```
use "drawio-aws4@^32" as aws4

model {
  fn = component "Handler" shape aws4.lambda_function
}
```

**They are restricted.** draw.io is Apache-2.0, but its stencils may not be used in, distributed
for use with, or incorporated into Atlassian products or the Atlassian marketplace without
written permission, and vendor sets (AWS, Azure, GCP, Cisco, IBM, …) are subject to the vendors'
trademark terms. See `assets/NOTICE`. `diadraw lib list` and the web palette mark
every set as restricted.

They are stored gzipped in `assets/drawio/` and loaded lazily: a set is read (native hosts,
and WebAssembly hosts given a file system with `setFileSystem(fs, { drawio })`) or fetched and
added with `addLibrary` (the web app) only when a document uses it or the palette opens it.

The draw.io importer draws stencil shapes with these libraries, in the cell's fill and stroke
colours (resource-icon tiles included). The conversion follows mxStencil: paths, rectangles,
rounded rectangles and ellipses with fill, stroke, colours, dashes, alpha and save/restore; text,
images and nested stencils are not drawn, and shapes draw.io draws in code are not stencils.

Regenerating, from the pinned version (`DRAWIO` in the script):

```bash
npm run build -w @diadraw/wasm
node tools/gen/drawio_stencils.mjs                   # downloads the pinned files
node tools/gen/drawio_stencils.mjs --from ../drawio  # or a local checkout
```

`diadraw lib build --drawio <stencils.min.js|set.xml> -o <dir>` still converts other versions
or single sets into separate libraries.

## Where libraries show up

- **CLI**: `render` and `check` resolve the document's libraries; `render` draws library shapes.
- **Language server**: completes library names in `use "…"`, shapes after `shape` (all of them
  when there are at most 400, else the aliases first) and `alias.` prefixes, library kinds after
  `=`, and offers fragments as snippets inside `model { … }` with their parameters as
  placeholders. Missing libraries, unknown shapes and unknown kinds are warnings.
- **Web editor**: the **Shapes** button opens a palette of the built-in libraries' shapes,
  composites, kinds and fragments, searchable by name and keyword. Clicking a shape adds an element
  with that shape (and the library's `use` if the document has none) as one undoable edit;
  clicking a fragment inserts its text at the end of the model.
- **Themes**: `theme alias.name` picks a library theme; `RenderOptions.themes`
  (`LibrarySet::theme_resolver()`) resolves it, and the CLI and language server warn about a
  theme the library does not have.
- **Renderer**: `RenderOptions.shapes` resolves references to drawings, which are drawn with the
  label underneath; `RenderOptions.kinds` resolves library kinds to a shape and colours;
  `create_measure(theme, shapes, kinds)` sizes them to match. `LibrarySet::renderer()` gives
  both.
