# DiaDraw user guide

This guide is for people drawing diagrams with DiaDraw, whether in the text editor, on the canvas
or both. It explains what the features are for and when to use them. The exact syntax lives in
the [DSL reference](../dsl.md); the guide links to it rather than repeating it.

## Chapters

- [Examples](examples.md): the example diagrams every app ships (C4, flowcharts, UML class,
  sequence, activity, state, use case and component diagrams, and BPMN), and how they are drawn.
- [Relationships](relationships.md): connecting elements, arrows and labels, ports,
  reconnecting a line on the canvas, and when to give a relationship a name.
- [Line ends and line styles](line-styles.md): arrows, diamonds, crow's foot and other ends,
  dotted, double, curved and rounded lines, and picking them in the app or the text.
- [Layouts](layouts.md): the layout algorithms (`layered`, `tree`, `grid`, `radial`, `force`,
  `fixed`, `sequence`), which to pick, and how hints and containers work with them.
- [Editing properties on the canvas](element-properties.md): the inspector for elements and
  relationships, what each field writes, naming a line, and how the change lands in your text.
- [Styles and themes on the canvas](styles-and-themes.md): style blocks for kinds, tags and
  relationships, picking and making themes, and how those edits land in your text.
- [Moving boxes](moving.md): dragging a box to where you want it in any view, the `pin … at`
  line that keeps, Option-drag to place it by its neighbours instead, and selecting several
  boxes to move or delete them together.
- [Resizing and rotating](resize-and-rotate.md): making a box bigger or smaller and turning it,
  in any view, and the `size` and `rotate` lines that keeps.
- [The grid](grid.md): showing a grid on the canvas, and how moves, resizes and bends line up
  with it while it shows.
- [Clearing the canvas](clear-canvas.md): starting the open diagram again from nothing, and how
  Undo brings it back.
- [Drawing by hand in fixed views](fixed-views.md): moving and resizing boxes and bending lines
  on the canvas, and the `pin` and `route` lines that keeps.
- [Drawing with AI agents](agents.md): the MCP server (`diadraw mcp`) and the Claude Code
  skill, which let an agent such as Claude Code write, check, render and fix diagrams.
- [Diagrams in Markdown](markdown.md): writing a diagram as a ` ```diadraw ` block in a Markdown
  page, and drawing it on documentation sites (remark, markdown-it) or in the browser
  (`diadraw.js`).
- [Diagrams in Confluence](confluence.md): the DiaDraw diagram macro for Confluence Cloud, its
  editor, themes, importing Mermaid and PlantUML, saving diagrams as pictures, and publishing `.draw` files
  from a repository to pages.
- [Google Drive and OneDrive](cloud-storage.md): opening and saving diagrams in either service
  from every app, the web app's Storage dialog, what DiaDraw can see there, and how to register
  a build of DiaDraw with Google and Microsoft.
- [DiaDraw from Python](python.md): the `diadraw` package, which checks, formats, renders and
  imports diagrams from a Python script with the same core as the apps.
- [Reporting bugs and giving feedback](feedback.md): Report a Bug and Give Feedback in each
  app, what they fill in for you, and what they never send.

More chapters will follow as the guide grows: models and views, notes and decorations, sequence diagrams, libraries, and importing from other tools.
