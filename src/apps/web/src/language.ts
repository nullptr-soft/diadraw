import { LanguageSupport, StreamLanguage } from '@codemirror/language';
import type { Diagnostic as LintDiagnostic } from '@codemirror/lint';
import { linter } from '@codemirror/lint';
import type { Diagnostic } from '@diadraw/wasm';

/**
 * DiaDraw highlighting for CodeMirror. The grammar itself lives in the C++ core; the editor only
 * needs tokens, so a small stream tokenizer does: keywords, names being declared, element kinds,
 * strings, numbers, comments and arrows. Diagnostics come from the core (see `diadraw`).
 */
const KEYWORDS = new Set(
  (
    'use as model view of group port tech shape tag description include exclude layout direction ' +
    'spacing pin at below above left right up down align horizontally vertically order north south ' +
    'east west style prop size route via from to sequence activate deactivate over divider delay ' +
    'ref autonumber alt opt loop par critical break else note theme relationship extends text box around rotate'
  ).split(' '),
);

interface State {
  inComment: boolean;
  /** Inside a block string, `"""` … `"""`, which spans lines. */
  inBlockString: boolean;
  /** The next identifier is an element kind (after `name =`). */
  expectKind: boolean;
}

const language = StreamLanguage.define<State>({
  name: 'diadraw',
  startState: () => ({ inComment: false, inBlockString: false, expectKind: false }),
  token(stream, state) {
    if (state.inBlockString) {
      // Up to the closing `"""`, skipping escapes, or the rest of the line.
      if (stream.match(/^(?:[^"\\]|\\.|\\$|"(?!""))*"""/)) state.inBlockString = false;
      else stream.skipToEnd();
      return 'string';
    }
    if (state.inComment) {
      if (stream.skipTo('*/')) {
        stream.match('*/');
        state.inComment = false;
      } else {
        stream.skipToEnd();
      }
      return 'comment';
    }
    if (stream.eatSpace()) return null;
    if (stream.match('//')) {
      stream.skipToEnd();
      return 'comment';
    }
    if (stream.match('/*')) {
      state.inComment = true;
      return 'comment';
    }
    if (stream.match('"""')) {
      state.inBlockString = true;
      return 'string';
    }
    if (stream.match(/^"(?:[^"\\\n]|\\.)*"?/)) return 'string';
    if (stream.match(/^-?\d+(?:\.\d+)?/)) return 'number';
    if (stream.match(/^(?:-->|->>|<->|->)/)) return 'operator';
    if (stream.match(/^[{}]/)) return 'bracket';
    if (stream.match('=')) {
      state.expectKind = true;
      return 'operator';
    }
    const word = stream.match(/^[A-Za-z_]\w*/) as RegExpMatchArray | null;
    if (word) {
      if (state.expectKind) {
        state.expectKind = false;
        // In `name = a.b -> c` the word after `=` is an endpoint, not a kind.
        if (stream.match(/^(?:\.[A-Za-z_]\w*)*\s*(?:-->|<->|->)/, false)) return 'variableName';
        // A library's kind, `alias.kind`, is one name.
        stream.match(/^\.[A-Za-z_]\w*/);
        return 'typeName';
      }
      if (stream.match(/^\s*=(?!=)/, false)) return 'definition';
      return KEYWORDS.has(word[0]) ? 'keyword' : 'variableName';
    }
    stream.next();
    return null;
  },
  languageData: { commentTokens: { line: '//', block: { open: '/*', close: '*/' } } },
});

/** Highlighting plus lint from the core's diagnostics for the current text. */
export function diadraw(diagnostics: (text: string) => readonly Diagnostic[]): LanguageSupport {
  const lint = linter(
    (view) =>
      diagnostics(view.state.doc.toString()).map((d): LintDiagnostic => ({
        from: Math.min(d.from, view.state.doc.length),
        to: Math.min(Math.max(d.to, d.from), view.state.doc.length),
        severity: d.severity,
        message: d.message,
      })),
    { delay: 200 },
  );
  return new LanguageSupport(language, [lint]);
}
