---
applyTo: "site/src/content/**,site/src/components/**,site/astro.config.mjs,site/src/localizedMetadata.ts,site/scripts/sync-docs.mjs,docs/dsl.md,docs/libraries.md,docs/guide/**,docs/i18n/**"
---

# Writing for the DiaDraw website

- Write for someone trying to draw a diagram. Lead with what they can do or what they will see. For example, prefer “Drag the element onto the canvas” to “Use the intuitive drag-and-drop functionality.” Use a button's text to name its action and a link's text to name its destination.
- Describe behavior that exists today. Check the editor, a runnable example, or the reference before promising a feature. In a walkthrough, quote the label the reader will actually see in the editor; explain it in their language if necessary.
- When the meaning changes, update English, Polish (`pl`), Ukrainian (`uk`), French (`fr`) and German (`de`) together. A typo or awkward phrase in just one language needs only a local fix. Translate the intent rather than the sentence structure, and use the same term as nearby pages for the same concept. Include titles, descriptions, action labels and alt text.
- Edit the authored MDX or the reference's Markdown source, never a generated reference page. For shared website UI, reuse the locale message keys rather than hard-coding English in a component. See [Maintaining translations](../../site/README.md#maintaining-translations) for source paths and build commands.
- Keep `DiaDraw`, DSL keywords, identifiers, commands, filenames and runnable examples intact across translations. Preserve page slugs so the language picker stays on the same page. When a heading changes, check links to its fragment in that language. Links to other docs should stay in the reader's locale; the editor and tutorial images are shared.
