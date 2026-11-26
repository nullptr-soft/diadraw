# Clearing the canvas

**Clear Canvas** starts the diagram you have open again from nothing. On macOS choose
**Edit ▸ Clear Canvas**; on Windows choose **Edit ▸ Clear Canvas**; on Linux choose
**Clear Canvas** in the menu; in the web app click **Clear** in the toolbar.

Everything goes: elements, relationships, views, styles and themes. The text becomes an empty
model,

```
model {
}
```

and the canvas shows its one, empty view. The file stays open under the same name, so the next
save writes the empty diagram over it.

Clearing is one change, like any other edit: a single **Undo** (⌘Z, or Ctrl+Z) brings the whole
diagram back, in the text and on the canvas, which shows the diagram's first view. When the
diagram has changes that are not saved yet, the app asks before clearing. In the web app a
diagram that is not saved to a file or a storage place counts as unsaved, so it always asks there.

To keep the diagram and start a new one beside it, use **File ▸ New** in the desktop apps
instead. To reset the zoom rather than the diagram, use **Actual Size** (⌘0, or Ctrl+0).
