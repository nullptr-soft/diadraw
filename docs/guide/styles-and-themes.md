# Styles and themes on the canvas

A diagram's look comes from three places. The **theme** picks the colours and fonts everything is
drawn with. **Style blocks** give a shared look to every element of a kind, every item with a
tag, or every relationship. An element's own **inline style** (the Style field of the
[inspector](element-properties.md)) changes that one element. In the text they look like this:

```
theme brand extends dark {
  background "#101418"
  node person { fill "#08427b" stroke "#052e56" text "#ffffff" }
}

theme brand

style container {
  fill "#438dd5"
}
style tag "external" { fill "#999999" dashed true }
style relationship tag "async" { dashed true }
```

The [DSL reference](../dsl.md#styles-and-themes) has every selector and key. This chapter is about
changing them from the canvas: you can edit all of it without leaving the diagram, and every
change still lands in your text.

## When to use which

- Use a **style block** when the look belongs to a kind of thing or to a tag: "every database is
  orange", "everything tagged `external` is grey and dashed". New elements of that kind or with
  that tag pick it up without you doing anything.
- Use the element's own **Style** field for the one element that is different.
- Use a **theme** for the whole diagram: the background, the line colour, the font, and the
  default colours of each kind. Pick one of the [built-in themes](#built-in-themes) or make your
  own.

When several apply, the more specific one wins: the theme, then `*` blocks, kind blocks, tag
blocks, kind-and-tag blocks, and last the element's own style.

## Built-in themes

Every theme sets its own background, so a diagram looks the same in a light or a dark app and in
an exported file. Every theme after `mono` keeps each label at a contrast of at least 4.5:1 against what it sits on.

| Theme       | Background | Look                                                              |
| ----------- | ---------- | ----------------------------------------------------------------- |
| `default`   | light      | Indigo and blue, the theme every view uses until you pick another |
| `dark`      | dark       | The default colours on a dark slate page                          |
| `mono`      | light      | Black on white with grey fills, for print                         |
| `ocean`     | light      | Cyan and teal on a pale blue-white page                           |
| `forest`    | light      | Emerald greens on a soft green-white page                         |
| `sunset`    | light      | Warm oranges and rose on a cream page                             |
| `lavender`  | light      | Violet and purple on a lilac-white page                           |
| `sand`      | light      | Earthy browns and stone greys on a warm beige page                |
| `graphite`  | light      | Neutral greys with a sky-blue accent, for technical documents     |
| `pastel`    | light      | A different soft pastel per kind, with dark text                  |
| `vivid`     | light      | A different saturated colour per kind, with white text            |
| `contrast`  | light      | Black on white with bold fills and larger text, for accessibility |
| `blueprint` | dark       | White lines on blueprint blue                                     |
| `midnight`  | dark       | Violet and fuchsia on a near-black indigo page                    |
| `aurora`    | dark       | Teal and violet on a dark sea-green page                          |
| `ember`     | dark       | Orange and red on a dark brown page                               |
| `terminal`  | dark       | Phosphor green on black                                           |

Pick one in the theme field of the inspector (shown while nothing is selected), or write it:
`theme ocean` for the whole document, or `theme ocean` inside a view for that view only. A theme
of your own can start from any of them: `theme brand extends blueprint { … }`.

## Shared styles of an element

Select one element and the inspector has a **Shared styles** field at the bottom. It lists the
style blocks that apply to that element, one per line, in the order they are written:

```
container: fill=#438dd5
tag "external": fill=#999999;dashed=1
```

Each line is a selector, a colon, and the keys as the Style field takes them (`key=value`
separated by `;`). Editing the field edits the blocks:

| You                                      | The text                                     |
| ---------------------------------------- | -------------------------------------------- |
| change a value                           | that line of the block is rewritten          |
| add a key to a line                      | the key is added at the end of the block     |
| remove a key                             | its line in the block is deleted             |
| add a line, such as `database: fill=red` | a new `style` block, after the blocks shown  |
| delete a line                            | its block is deleted                         |
| change a line's selector                 | the block's selector changes, its body stays |

A change here restyles **every** element the block applies to, not just the selected one; that
is the point. To change only this element, use its Style field instead.

The field only takes selectors that apply to the selected element: its kind, its tags, its kind
and one of its tags, or `*`. A line for another kind, or for relationships, is refused with a
message in the status line. Edit those with nothing selected (below).

## Theme and styles of the document

Click an empty part of the canvas. While nothing is selected, the properties panel beside the
canvas shows the document:

- **Theme**: the document's `theme` statement. Pick or type a [built-in theme](#built-in-themes), a theme you
  have declared, or a library theme (`acme.corporate`). Clearing it removes the statement.
- **This view's theme**: the `theme` inside the current view, which overrides the document's for
  this view only. Empty means the view uses the document's theme.
- **Style blocks**: every style block in the document, including relationship ones such as
  `relationship tag "async": dashed=1`, one per line as above.

### Making your own theme

Type a name that is not a theme yet, say `night`, into Theme (or This view's theme). DiaDraw
declares it for you, starting from the theme it replaces, and picks it:

```
theme night extends dark {}

theme night
```

When the view is drawn with a theme declared in the document, the panel shows its section,
**Theme night**, with three more fields:

- **Extends**: the theme it starts from. Empty means `default`.
- **Theme keys**: one `key=value` per line: `background`, `fontFamily`, `fontSize`, `edge`,
  `edgeText`, `boundary` and the other keys the [DSL reference](../dsl.md#your-own-themes)
  lists.
- **Node colours**: one kind per line with its `fill`, `stroke` and `text`:
  `person: fill=#08427b;stroke=#052e56;text=#ffffff`. `default` is every kind without its own
  line, `external` is every element tagged `external`. A person's `text` colours its name only when
  a `shape` draws it as a filled box; the stick figure's name sits on the canvas in `labelText`.

Built-in and library themes cannot be edited in place; make your own that extends them.

### In the macOS, web and Linux apps

The apps show these fields as controls that write the same text:

- **Theme**, **This view's theme** and **Extends** are combo boxes listing the built-in themes,
  your libraries' themes and the themes the document declares. You can still type a new name.
- **Style blocks** is a list with one row per block. Pick or type its selector (`*`,
  `relationship`, a kind, `tag "external"`, `relationship tag "async"`), open the row with its
  disclosure triangle to change its keys in the same style editor the element inspector uses,
  and remove it with −. **Add style block** adds a row.
- **Theme keys** has a colour button for each colour key (`background`, `edge`, `boundary`, …)
  and a text field for `fontFamily` and the font sizes. Colour buttons offer the same preset
  palette as the element inspector, with Custom… for any other colour. The × beside a colour
  button removes the key, so
  the theme falls back to the one it extends. Keys the panel does not list are kept.
- **Node colours** has a row per kind with Fill, Line and Text colour buttons. Pick a kind and click Add
  for a new row; it is written once it has a colour.

In the web app the theme fields suggest the built-in themes and the ones the document declares,
and a style block's triangle folds it to a one-line summary of its keys.

In the Linux (GTK) app, Theme, This view's theme and Extends are entries with a ▾ that lists the
themes, Style blocks is a list of blocks with a ▸ that opens the style editor, and Theme keys and
Node colours are colour buttons.

## Applying a change

Fields work as in the element inspector: a field is applied when you press Enter or leave it
after a change, and Escape puts it back. The multi-line fields (Shared styles, Style blocks,
Theme keys, Node colours) apply when you leave them, or with ⌘Enter / Ctrl+Enter in the web app.
In the macOS app Return applies Shared styles and ⌥Return starts a new line. In both apps the Theme
and styles controls apply a picked colour or choice at once.

Each change is one undo step, shared with the text editor: ⌘Z on either side takes it back.

## What happens to your text

The text is still the source of truth, and the panel changes as little of it as it can:

- a changed value is rewritten where it is, so comments and alignment on that line stay;
- a removed key's line is deleted; a block written on one line, like
  `style tag "external" { fill "#999999" dashed true }`, stays on one line;
- new keys go at the end of their block, new blocks after the other style blocks (or above the
  `model` block when there are none), a new theme statement after the theme declarations;
- the order you wrote things in is kept, whatever order you type them in the field.

For example, changing the container fill from the panel turns

```
style container {
  fill "#438dd5"   // brand blue
  stroke "#3c7fc0"
}
```

into

```
style container {
  fill "#1168bd"   // brand blue
  stroke "#3c7fc0"
}
```

## Things to know

- **`true` and `false`** are stored as `1` and `0`, as draw.io writes them, so `dashed true`
  shows as `dashed=1` in the fields. An entry you do not change keeps the word you wrote.
- **Values** cannot contain `;` in the style fields, as in inline styles. Theme keys are one per
  line, so their values can.
- **Several blocks with one selector** are shown as several lines; the later one wins where they
  set the same key.
- **A theme with two `node` blocks for one kind** is reprinted when you change that kind's colours
  from the panel, since the panel cannot tell which block you meant.
- **Renaming or deleting a theme declaration** is done in the text; the panel edits the theme the
  view is drawn with.
- **Where it is available:** the web app, the macOS app and the Linux (GTK) app. The Windows app
  does not have the inspector yet.
