/**
 * A stand-in for @forge/bridge in the local preview (`npm run dev`), which has no Confluence
 * around it. The macro's configuration lives in localStorage: the editor's Save writes it and the
 * macro frame on the preview page reads it.
 */
const KEY = 'diadraw-confluence-dev-config';

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
      moduleKey: 'diadraw',
    }),
  submit: (payload?: { config?: unknown }) => {
    localStorage.setItem(KEY, JSON.stringify(payload?.config ?? null));
    window.parent.postMessage('diadraw-dev-saved', '*');
    return Promise.resolve();
  },
  close: () => {
    window.parent.postMessage('diadraw-dev-closed', '*');
    return Promise.resolve();
  },
  theme: { enable: () => Promise.resolve() },
};
