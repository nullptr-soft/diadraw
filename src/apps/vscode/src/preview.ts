/** HTML for the preview webview. Pure, so it can be tested without VS Code. */

const escape = (text: string): string =>
  text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

export interface PreviewState {
  readonly views: readonly string[];
  readonly view: string | undefined;
  /** The rendered diagram, or the problem to show instead. */
  readonly svg?: string;
  readonly error?: string;
}

export function previewHtml(state: PreviewState, nonce: string, cspSource: string): string {
  const options = state.views
    .map(
      (v) =>
        `<option value="${escape(v)}"${v === state.view ? ' selected' : ''}>view ${escape(v)}</option>`,
    )
    .join('');
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${cspSource} data:; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { margin: 0; padding: 12px; font-family: var(--vscode-font-family); color: var(--vscode-foreground); }
  header { display: flex; gap: 8px; align-items: center; margin-bottom: 12px; }
  select { background: var(--vscode-dropdown-background); color: var(--vscode-dropdown-foreground); border: 1px solid var(--vscode-dropdown-border); padding: 2px 4px; }
  .diagram svg { max-width: 100%; height: auto; background: #fff; border-radius: 4px; }
  .error { color: var(--vscode-errorForeground); white-space: pre-wrap; font-family: var(--vscode-editor-font-family); }
</style>
</head>
<body>
<header>${state.views.length > 1 ? `<select id="view">${options}</select>` : ''}</header>
${state.error ? `<p class="error">${escape(state.error)}</p>` : ''}
<div class="diagram">${state.svg ?? ''}</div>
<script nonce="${nonce}">
  const vscode = acquireVsCodeApi();
  document.getElementById('view')?.addEventListener('change', (e) => vscode.postMessage({ type: 'view', view: e.target.value }));
</script>
</body>
</html>`;
}
