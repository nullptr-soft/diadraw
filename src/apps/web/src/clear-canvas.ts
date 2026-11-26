// Clear Canvas: the whole text becomes an empty diagram as one undoable change, as in the native
// apps (dd::host::Document::clear).

/** What Clear Canvas leaves: the same empty diagram the native apps leave. */
export const EMPTY_DIAGRAM = 'model {\n}\n';

/** What Clear Canvas does with `text`, given the text last saved (`undefined` when never saved). */
export function clearAction(text: string, saved: string | undefined): 'nothing' | 'clear' | 'ask' {
  if (text === EMPTY_DIAGRAM) return 'nothing';
  // Saved: the file still has it, and undo brings it back. Unsaved: ask first.
  return saved !== undefined && text === saved ? 'clear' : 'ask';
}
