# DiaDraw plugin for Claude Code

A Claude Code plugin with one skill, `diadraw`, that lets Claude draw diagrams with DiaDraw. When
you ask for an architecture, C4, flowchart, sequence, UML, BPMN or other diagram, or ask it to
change a `.draw` file, Claude:

1. writes the diagram in DiaDraw's text DSL, starting from a pattern for that kind of diagram;
2. runs `diadraw check` and fixes every error;
3. runs `diadraw render … -o diagram.png`, looks at the picture and fixes what reads badly
   (direction, layout algorithm, hints, labels), usually in two or three rounds;
4. renders the format you want (SVG, PNG, PDF, draw.io or BPMN) and keeps the `.draw` source
   next to it.

It also converts Mermaid, PlantUML, Structurizr, draw.io and BPMN files with `diadraw import`.

## Install

In Claude Code:

```
/plugin marketplace add mykulyak/diadraw
/plugin install diadraw@diadraw
```

The skill then loads on its own whenever a task calls for a diagram; you can also ask for it by
name ("use the diadraw skill to draw …").

To try it from a clone of this repository without installing:
`claude --plugin-dir plugins/diadraw`.

## The `diadraw` CLI

The skill runs the native `diadraw` command-line tool. It looks for it, in order, in the
`DIADRAW` environment variable, on `PATH`, and in `../diadraw-build/release/` or
`../diadraw-build/dev/` next to a DiaDraw checkout. Build it from a clone of this repository
(see [src/README.md](../../src/README.md)):

```bash
cmake --preset release && cmake --build --preset release --target diadraw
```

Then put `../diadraw-build/release/diadraw` on your `PATH`, or set `DIADRAW` to it.

If the DiaDraw MCP server is configured in the same session, the skill uses its check and
render tools instead of the CLI.

## What is in it

| File                                    | Contents                                                    |
| --------------------------------------- | ----------------------------------------------------------- |
| `skills/diadraw/SKILL.md`               | The workflow: find the CLI, write, check, render, look, fix |
| `skills/diadraw/references/dsl.md`      | A one-page summary of the DSL                               |
| `skills/diadraw/references/patterns.md` | Working starting points for each kind of diagram            |

The full reference is [docs/dsl.md](../../docs/dsl.md) and the
[user guide](../../docs/guide/README.md); the skill points Claude there when it needs more.
Every snippet in the skill checks clean and renders; if you change the DSL, keep them in step.
