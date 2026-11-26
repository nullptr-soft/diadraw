/**
 * The app's Forge function: what the DiaDraw macro becomes when a page is exported to PDF or Word.
 * Custom UI macros are iframes, which an export cannot draw, so the export shows the diagram's
 * DiaDraw text as a code block under a caption naming the view.
 *
 * The handler runs in the Forge runtime (Node), bundled by the Forge CLI from this file. It imports
 * nothing, so that bundle stays small and needs no module resolution beyond this file.
 */

/** An Atlassian Document Format node, as much of it as the export builds. */
export interface AdfNode {
  readonly type: string;
  readonly version?: number;
  readonly attrs?: Readonly<Record<string, unknown>>;
  readonly content?: readonly AdfNode[];
  readonly text?: string;
  readonly marks?: readonly { readonly type: string }[];
}

/**
 * Where the macro's configuration is in the export payload. Atlassian has passed it as
 * `config`, `extensionPayload.config` and `context.extension.config`; any of them is read.
 */
function configOf(payload: unknown): unknown {
  const p = (payload ?? {}) as Record<string, Record<string, unknown> | undefined>;
  return (
    p.config ??
    p.extensionPayload?.config ??
    (p.context?.extension as Record<string, unknown> | undefined)?.config
  );
}

export function exportAdf(payload: unknown): AdfNode {
  const config = (configOf(payload) ?? {}) as Record<string, unknown>;
  const source = typeof config.source === 'string' ? config.source : '';
  const view = typeof config.view === 'string' ? config.view : '';
  const caption = view ? `DiaDraw diagram (view ${view})` : 'DiaDraw diagram';
  return {
    type: 'doc',
    version: 1,
    content: [
      {
        type: 'paragraph',
        content: [{ type: 'text', text: caption, marks: [{ type: 'strong' }] }],
      },
      {
        type: 'codeBlock',
        attrs: { language: 'diadraw' },
        content: source ? [{ type: 'text', text: source }] : [],
      },
    ],
  };
}

/** The `adfExport` handler named in manifest.yml (`index.adfExport`). */
export const adfExport = (payload: unknown): Promise<AdfNode> =>
  Promise.resolve(exportAdf(payload));
