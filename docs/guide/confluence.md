# Diagrams in Confluence

DiaDraw for Confluence Cloud adds a **DiaDraw diagram** macro to Confluence pages. You write the
diagram's text in the macro's editor, and the page shows the diagram, drawn in the reader's
browser by the same core as the apps. The text is saved with the macro, so anyone who can edit
the page can open the diagram again and change it.

The app is an Atlassian Forge app. Nothing is drawn on a DiaDraw server: the diagram's text stays
in your Confluence site.

## Adding a diagram

1. Edit a page, type `/diadraw` and pick **DiaDraw diagram**.
2. The editor opens with a small example. Replace it with your diagram: the text is on the left
   and the drawing on the right updates as you type. Errors are listed under the text with their
   line and column; click one to jump to it.
3. Pick the **View** to show when the text has more than one, and a **Theme**.
4. Press **Save** (or Ctrl+S, ⌘S on a Mac). **Cancel** leaves the macro as it was.

To change the diagram later, select the macro in the page editor and press its **Edit** button.

```diadraw render
model {
  author = person "Author"
  confluence = system "Confluence page" {
    macro = container "DiaDraw macro" tech "Forge app"
    core = container "DiaDraw core" tech "WebAssembly"
  }
  author -> confluence.macro "writes the text"
  confluence.macro -> confluence.core "draws it in the browser"
}

view page of confluence {
  include *
  layout layered direction right
}
```

## Picking a theme

The theme picker lists the built-in themes (`default`, `dark`, `mono`) and the themes your text
declares. Picking one writes a `theme` line into the text, the same line you would type, so the
text stays the whole description of the diagram. Picking `default` takes the line out again. When
the view has its own `theme` line, the picker changes that one.

## Importing Mermaid, PlantUML and other diagrams

**Import…** converts a diagram from another tool into DiaDraw text: paste it, or pick a file, and
choose the format (Mermaid, PlantUML, Structurizr DSL, draw.io or BPMN 2.0). The import replaces
the text in the editor, and Ctrl+Z brings the old text back. Anything the importer could not
carry over is listed under the editor, so you can add it by hand. This is a quick way to move
diagrams from Mermaid or PlantUML macros to DiaDraw.

## Saving a diagram as a picture

Point at a diagram in the page and two buttons appear in its corner: **SVG** and **PNG** save
the diagram as a file. The PNG is drawn at twice the diagram's size, for sharp slides.

## Exporting the page

Confluence's PDF and Word exports cannot run the app, so in an export the macro shows the
diagram's text as a code block under the name of its view. Save the diagram as SVG or PNG and
attach it when an export must show the picture.

## Publishing diagrams from a repository

When your diagrams live as `.draw` files in a repository, publish them to a page instead of
pasting them into macros. The publisher draws each view of each file as SVG and attaches it to
the page: `checkout.draw` becomes `checkout.svg`, or `checkout-<view>.svg` when the file has
several views. Publishing again adds a new version of each attachment whose drawing changed, and
skips the rest, so the images on the page follow the repository.

Insert an attachment into the page once (with Confluence's image picker, or `--insert`, which
adds an image of each new attachment at the end of the page), and later publishes update it in
place. The page's id is the number in its URL, after `/pages/`.

From a terminal, with the repository built (see the
[app's README](../../src/apps/confluence/README.md)):

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net
export CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…   # from id.atlassian.com, Security, API tokens
node path/to/diadraw/src/apps/confluence/dist/cli.js publish docs/diagrams/*.draw --page 123456 --insert
```

`--view` publishes only the named views, `--dry-run` lists the attachments without contacting
Confluence, and a file with errors stops the publish with its errors, like `diadraw check`.

In GitHub Actions, the `DiaDraw to Confluence` action does the same on every push. Keep the
account and its API token in repository secrets:

```yaml
name: Diagrams to Confluence
on:
  push:
    branches: [main]
    paths: ['docs/diagrams/**']
jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mykulyak/diadraw/src/apps/confluence@main
        with:
          files: docs/diagrams/**/*.draw
          page-id: '123456'
          base-url: https://example.atlassian.net
          email: ${{ secrets.CONFLUENCE_EMAIL }}
          api-token: ${{ secrets.CONFLUENCE_API_TOKEN }}
```

The action builds the DiaDraw core on each run, which takes a few minutes until the core is
published as a package.

## Drawing in the draw.io editor

A second app, **draw.io diagrams for Confluence**, adds a **Diagram (draw.io editor)** macro for
diagrams you would rather draw by hand than write as text. Type `/diagram` in the page editor and
pick it: the open-source draw.io editor opens over the page. Draw, then press **Save & Exit**;
**Exit** leaves the macro as it was. The page shows the drawing as a picture, and the macro's
**Edit** button opens it in draw.io again, with every page of the diagram.

Point at the drawing in the page for **SVG** and **PNG** buttons. The SVG keeps the whole
diagram inside it, so draw.io (or the desktop app) opens it for editing.

draw.io comes with the app, so the editor and your diagram stay with Confluence; nothing is
loaded from draw.io's servers. The app is not made by the draw.io team; see
[its README](../../src/apps/confluence-drawio/README.md) for installing it and for updating its
copy of draw.io.

## Installing the app

The app is not in the Atlassian Marketplace yet. A site administrator installs it from the
DiaDraw repository with the Forge command line tool; the steps are in
[src/apps/confluence/README.md](../../src/apps/confluence/README.md).
