# Drawing with AI agents

DiaDraw diagrams are plain text, so an AI agent can write them as easily as code. The agent
writes a `.draw` file, checks it, renders it, looks at the picture and fixes what reads badly,
the same loop a person follows in the app. Two pieces make that loop smooth, and they work well
together:

- **The MCP server**, `diadraw mcp`: gives any agent that speaks the
  [Model Context Protocol](https://modelcontextprotocol.io) (Claude Code, Claude Desktop and
  others) tools to check, render and import diagrams. Rendering returns an image, so the agent
  sees the diagram it drew.
- **The Claude Code skill**: teaches Claude Code the language and the check, render, look and
  fix loop. Install it with `/plugin marketplace add mykulyak/diadraw` and then
  `/plugin install diadraw@diadraw` (see
  [plugins/diadraw/README.md](../../plugins/diadraw/README.md)).

## What the agent can do

| Tool        | What it does                                                                                                         |
| ----------- | -------------------------------------------------------------------------------------------------------------------- |
| `check`     | Reports errors and warnings as `line:column: severity: message`, and lists the document's views                      |
| `render`    | Draws one view and returns it as a PNG image (or SVG); can also write an `.svg`, `.png` or `.pdf` file               |
| `format`    | Returns the document in canonical form, or rewrites the file                                                         |
| `import`    | Converts Mermaid, PlantUML, Structurizr, draw.io and BPMN into a DiaDraw document                                    |
| `reference` | Lists element kinds, shapes, themes, layouts and libraries, or one library's kinds and shapes (`library: "c4"`)      |
| `examples`  | Lists the [example diagrams](examples.md) and returns any of them, so the agent can start from one close to its task |

Each tool takes the document either as `source` (its text) or as `path` (a file). Prefer files
in your project: the diagram then lives in version control like everything else. Libraries
resolve offline, as they do for the command line: the built-in ones, draw.io's stencil sets, and
whatever `diadraw lib install` put in the cache.

## Setting it up

The server is part of the `diadraw` command-line tool. The Linux and Windows packages include
it; elsewhere, build it from the repository (see [src/README.md](../../src/README.md)):

```sh
cmake --preset release && cmake --build --preset release
# the binary is ../diadraw-build/release/diadraw
```

Then tell your agent where it is. Use the binary's full path if it is not on your `PATH`.

### Claude Code

```sh
claude mcp add diadraw -- /path/to/diadraw mcp
```

That adds it to the current project for you alone. Add `--scope user` to have it in every
project, or `--scope project` to write it to the project's `.mcp.json` and share it with everyone
who works there. Run `/mcp` in Claude Code to check that it connected.

### Claude Desktop

Open **Settings › Developer › Edit Config** and add DiaDraw to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "diadraw": {
      "command": "/path/to/diadraw",
      "args": ["mcp"]
    }
  }
}
```

Restart Claude Desktop. Claude Desktop starts the server outside any project, so ask it to use
full paths for the files it reads and writes.

### Other agents

Any MCP client can run `diadraw mcp` as a local (stdio) server: it reads one JSON-RPC message
per line on standard input and answers on standard output.

## Asking for a diagram

Ask in plain words and say where the file goes, for example:

> Draw the containers of our checkout service as a C4 diagram in `docs/checkout.draw`, render it
> to `docs/checkout.svg`, and check that the picture reads well.

The agent writes the file, runs `check` until it is clean, then `render`s and looks at the image.
If boxes overlap or the flow runs the wrong way, it changes the view, for instance
`layout layered direction right` or `pin db below api`, and renders again. You can open the same
file in any DiaDraw app afterwards and keep editing it by hand or on the canvas.
