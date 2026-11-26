/**
 * Publishes `.draw` files to Confluence Cloud pages: each view is drawn as SVG by the core and
 * uploaded as an attachment of the page, replacing the earlier version. A page shows the diagram
 * wherever it embeds the attachment, so a diagram kept in a repository updates in Confluence when
 * the repository publishes it again. `diadraw-confluence publish` (cli.ts) and the GitHub Action
 * (action.yml) run this.
 */
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { basename, extname, resolve } from 'node:path';
import type { DiaDraw } from '@diadraw/wasm';

/** One SVG to attach: a view of a file. */
export interface Rendered {
  /** The attachment's file name: `<file>.svg`, or `<file>-<view>.svg` for files with several views. */
  readonly fileName: string;
  readonly file: string;
  readonly view: string;
  readonly svg: string;
}

export class PublishError extends Error {
  override name = 'PublishError';
}

/** Letters, digits, dashes, dots and underscores: safe in an attachment's file name. */
function slug(text: string): string {
  return text.replace(/[^\w.-]+/g, '-').replace(/^-+|-+$/g, '') || 'diagram';
}

/**
 * Draws the views of a `.draw` file: all of them, or those of `views` it has. A file with errors
 * fails with each error as `file:line:column: message`, like `diadraw check`.
 */
export async function renderFile(
  dd: DiaDraw,
  file: string,
  views?: readonly string[],
): Promise<Rendered[]> {
  const path = resolve(file);
  const text = await readFile(path, 'utf8');
  const session = dd.createSession(text, { path });
  try {
    const { model, diagnostics } = session.state();
    const errors = diagnostics.filter((d) => d.severity === 'error');
    if (errors.length) {
      throw new PublishError(
        errors.map((d) => `${file}:${d.line}:${d.column}: ${d.message}`).join('\n'),
      );
    }
    const all = Object.keys(model.views);
    const picked = views?.length ? all.filter((v) => views.includes(v)) : all;
    const base = slug(basename(path, extname(path)));
    return picked.map((view) => ({
      fileName: all.length === 1 ? `${base}.svg` : `${base}-${slug(view)}.svg`,
      file,
      view,
      svg: session.exportSvg(view),
    }));
  } finally {
    session.dispose();
  }
}

/** Where and as whom to publish. */
export interface Site {
  /** `https://<site>.atlassian.net` (with or without `/wiki`). */
  readonly baseUrl: string;
  readonly email: string;
  /** An Atlassian API token for `email` (https://id.atlassian.com/manage-profile/security/api-tokens). */
  readonly token: string;
  /** For tests; the global `fetch` otherwise. */
  readonly fetch?: typeof fetch;
}

export type Outcome = 'uploaded' | 'updated' | 'unchanged';

/** The hash recorded in each attachment's comment, so an unchanged diagram is not uploaded again. */
export function hashOf(svg: string): string {
  return createHash('sha256').update(svg).digest('hex').slice(0, 16);
}

const COMMENT_PREFIX = 'Published by DiaDraw';

export class Confluence {
  readonly #wiki: string;
  readonly #auth: string;
  readonly #fetch: typeof fetch;

  constructor(site: Site) {
    this.#wiki = site.baseUrl.replace(/\/+$/, '').replace(/\/wiki$/, '') + '/wiki';
    this.#auth = `Basic ${Buffer.from(`${site.email}:${site.token}`).toString('base64')}`;
    this.#fetch = site.fetch ?? fetch;
  }

  async #call(method: string, path: string, init: RequestInit = {}): Promise<unknown> {
    const response = await this.#fetch(this.#wiki + path, {
      ...init,
      method,
      headers: {
        Authorization: this.#auth,
        Accept: 'application/json',
        ...(init.headers as Record<string, string> | undefined),
      },
    });
    if (!response.ok) {
      const body = await response.text().catch(() => '');
      throw new PublishError(
        `Confluence answered ${response.status} ${response.statusText} to ${method} ${path}${body ? `: ${body.slice(0, 500)}` : ''}`,
      );
    }
    return response.status === 204 ? undefined : response.json();
  }

  /** The page's attachment with this name: its id and the comment of its latest version. */
  async attachment(
    pageId: string,
    fileName: string,
  ): Promise<{ id: string; comment: string } | undefined> {
    const found = (await this.#call(
      'GET',
      `/rest/api/content/${encodeURIComponent(pageId)}/child/attachment?filename=${encodeURIComponent(fileName)}&expand=version`,
    )) as {
      results?: {
        id: string;
        metadata?: { comment?: string };
        version?: { message?: string };
      }[];
    };
    const first = found.results?.[0];
    // An attachment's comment is its version's message; older responses had it in metadata.
    return (
      first && { id: first.id, comment: first.metadata?.comment ?? first.version?.message ?? '' }
    );
  }

  /**
   * Attaches the SVG to the page, as a new version when the page has an attachment of that name.
   * A diagram whose last published version is the same is left alone.
   */
  async publish(pageId: string, rendered: Rendered): Promise<Outcome> {
    const hash = hashOf(rendered.svg);
    const comment = `${COMMENT_PREFIX} from ${rendered.file} (view ${rendered.view}), sha256:${hash}`;
    const existing = await this.attachment(pageId, rendered.fileName);
    if (existing?.comment.endsWith(`sha256:${hash}`)) return 'unchanged';
    const form = new FormData();
    form.append('file', new Blob([rendered.svg], { type: 'image/svg+xml' }), rendered.fileName);
    form.append('comment', comment);
    form.append('minorEdit', 'true');
    // PUT creates the attachment or adds a version to the one with the same name.
    await this.#call('PUT', `/rest/api/content/${encodeURIComponent(pageId)}/child/attachment`, {
      body: form,
      headers: { 'X-Atlassian-Token': 'no-check' },
    });
    return existing ? 'updated' : 'uploaded';
  }

  /**
   * Adds an image of each attachment the page does not show yet to the end of the page, so a
   * first publish puts the diagrams on the page. Returns the names it added.
   */
  async insertImages(pageId: string, fileNames: readonly string[]): Promise<string[]> {
    const page = (await this.#call(
      'GET',
      `/api/v2/pages/${encodeURIComponent(pageId)}?body-format=storage`,
    )) as {
      id: string;
      status: string;
      title: string;
      body: { storage: { value: string } };
      version: { number: number };
    };
    const body = page.body.storage.value;
    const missing = fileNames.filter((name) => !body.includes(`ri:filename="${escapeXml(name)}"`));
    if (!missing.length) return [];
    const images = missing
      .map(
        (name) => `<p><ac:image><ri:attachment ri:filename="${escapeXml(name)}" /></ac:image></p>`,
      )
      .join('');
    await this.#call('PUT', `/api/v2/pages/${encodeURIComponent(pageId)}`, {
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: page.id,
        status: page.status,
        title: page.title,
        body: { representation: 'storage', value: body + images },
        version: { number: page.version.number + 1, message: 'Add DiaDraw diagrams' },
      }),
    });
    return missing;
  }
}

function escapeXml(text: string): string {
  return text.replace(/[<>&"']/g, (c) => `&#${c.charCodeAt(0)};`);
}
