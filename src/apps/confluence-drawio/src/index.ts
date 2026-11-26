/**
 * The app's Forge function: what the macro becomes when a page is exported to PDF or Word.
 * Custom UI macros are frames, which an export cannot draw, so the export says where the
 * diagram is instead.
 *
 * The handler runs in the Forge runtime (Node), bundled by the Forge CLI from this file. It
 * imports nothing, so that bundle stays small.
 */

/** An Atlassian Document Format node, as much of it as the export builds. */
export interface AdfNode {
  readonly type: string;
  readonly version?: number;
  readonly content?: readonly AdfNode[];
  readonly text?: string;
  readonly marks?: readonly { readonly type: string }[];
}

export function exportAdf(): AdfNode {
  return {
    type: 'doc',
    version: 1,
    content: [
      {
        type: 'paragraph',
        content: [
          { type: 'text', text: 'Diagram', marks: [{ type: 'strong' }] },
          {
            type: 'text',
            text: ': open this page in Confluence to see it, or download it there as SVG or PNG and attach it.',
          },
        ],
      },
    ],
  };
}

/** The `adfExport` handler named in manifest.yml (`index.adfExport`). */
export const adfExport = (): Promise<AdfNode> => Promise.resolve(exportAdf());
