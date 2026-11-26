/** Renders a document's views for the preview and the SVG export, through a core session. */
import type { DiaDraw, Session } from '@diadraw/wasm';
import type { PreviewState } from './preview';

/** The views of the session's text, and `view` (or the first one) as SVG or the reason it can't be. */
export function renderState(session: Session, view: string | undefined): PreviewState {
  const state = session.state();
  const views = state.views.map((v) => v.id);
  const chosen = view !== undefined && views.includes(view) ? view : views[0];
  const errors = state.diagnostics.filter((d) => d.severity === 'error');
  if (errors.length > 0) {
    const error = errors.map((d) => `line ${d.line}:${d.column}: ${d.message}`).join('\n');
    return { views, view: chosen, error };
  }
  if (chosen === undefined) return { views, view: chosen, error: 'No view to show' };
  try {
    return { views, view: chosen, svg: session.exportSvg(chosen) };
  } catch (e) {
    return { views, view: chosen, error: e instanceof Error ? e.message : String(e) };
  }
}

/** One render of some text, for a one-off export. */
export function renderOnce(
  dd: DiaDraw,
  text: string,
  path: string | undefined,
  view?: string,
): PreviewState {
  const session = dd.createSession(text, path === undefined ? {} : { path });
  try {
    return renderState(session, view);
  } finally {
    session.dispose();
  }
}
