/**
 * A stand-in for @forge/bridge in the local preview (`npm run dev`), which has no Confluence
 * around it. The macro's configuration lives in localStorage: the editor's Save & Exit writes it
 * and the macro frame on the preview page reads it.
 */
const KEY = 'diadraw-confluence-drawio-dev-config';

function stored(): unknown {
  try {
    return JSON.parse(localStorage.getItem(KEY) ?? 'null');
  } catch {
    return null;
  }
}

export const view = {
  getContext: () =>
    Promise.resolve({
      extension: { config: stored() ?? undefined },
      localId: 'dev',
      moduleKey: 'drawio-diagram',
      locale: navigator.language,
    }),
  submit: (payload?: { config?: unknown }) => {
    localStorage.setItem(KEY, JSON.stringify(payload?.config ?? null));
    window.parent.postMessage('drawio-dev-saved', '*');
    return Promise.resolve();
  },
  close: () => {
    window.parent.postMessage('drawio-dev-closed', '*');
    return Promise.resolve();
  },
  theme: { enable: () => Promise.resolve() },
};
