import { mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadDiaDraw } from '@diadraw/wasm';
import { Confluence, PublishError, hashOf, renderFile, type Rendered } from './publish.js';

const dd = await loadDiaDraw();
const dir = await mkdtemp(join(tmpdir(), 'diadraw-confluence-'));

async function file(name: string, text: string): Promise<string> {
  const path = join(dir, name);
  await writeFile(path, text);
  return path;
}

const twoViews = `model {
  a = system "A"
  b = system "B"
  a -> b "calls"
}
view overview {
  include *
}
view onlyA {
  include a
}
`;

describe('renderFile', () => {
  it('draws every view, named after the file and the view', async () => {
    const rendered = await renderFile(dd, await file('My Flow.draw', twoViews));
    expect(rendered.map((r) => [r.fileName, r.view])).toEqual([
      ['My-Flow-overview.svg', 'overview'],
      ['My-Flow-onlyA.svg', 'onlyA'],
    ]);
    expect(rendered[0]!.svg).toContain('<svg');
  });

  it('names a single view after the file alone, and draws only the views asked for', async () => {
    const one = await renderFile(dd, await file('one.draw', 'model {\n  a = system "A"\n}\n'));
    expect(one.map((r) => [r.fileName, r.view])).toEqual([['one.svg', 'default']]);
    const picked = await renderFile(dd, await file('two.draw', twoViews), ['onlyA']);
    expect(picked.map((r) => r.fileName)).toEqual(['two-onlyA.svg']);
  });

  it('fails with the errors, and skips views the file does not have', async () => {
    const broken = await file('broken.draw', 'model {\n  a = = \n}\n');
    await expect(renderFile(dd, broken)).rejects.toThrow(PublishError);
    await expect(renderFile(dd, broken)).rejects.toThrow(/broken\.draw:2:\d+: /);
    expect(await renderFile(dd, await file('v.draw', twoViews), ['nope'])).toEqual([]);
  });
});

interface Call {
  method: string;
  url: string;
  headers: Record<string, string>;
  body: unknown;
}

/** A fake Confluence: answers each request with the next queued response, and records it. */
function fakeSite(responses: unknown[]) {
  const calls: Call[] = [];
  const fetch = (async (url: string, init: RequestInit) => {
    calls.push({
      method: init.method ?? 'GET',
      url,
      headers: init.headers as Record<string, string>,
      body: init.body,
    });
    const next = responses.shift();
    return new Response(next === undefined ? null : JSON.stringify(next), {
      status: next === undefined ? 204 : 200,
    });
  }) as typeof globalThis.fetch;
  const confluence = new Confluence({
    baseUrl: 'https://example.atlassian.net/wiki/',
    email: 'me@example.com',
    token: 'secret',
    fetch,
  });
  return { calls, confluence };
}

const diagram: Rendered = { fileName: 'flow.svg', file: 'flow.draw', view: 'main', svg: '<svg/>' };

describe('Confluence', () => {
  it('uploads a new attachment with the hash in its comment', async () => {
    const { calls, confluence } = fakeSite([{ results: [] }, { results: [{ id: 'att1' }] }]);
    expect(await confluence.publish('42', diagram)).toBe('uploaded');
    expect(calls[0]!.url).toBe(
      'https://example.atlassian.net/wiki/rest/api/content/42/child/attachment?filename=flow.svg&expand=version',
    );
    expect(calls[0]!.headers.Authorization).toBe(
      `Basic ${Buffer.from('me@example.com:secret').toString('base64')}`,
    );
    const upload = calls[1]!;
    expect(upload.method).toBe('PUT');
    expect(upload.headers['X-Atlassian-Token']).toBe('no-check');
    const form = upload.body as FormData;
    expect((form.get('file') as File).name).toBe('flow.svg');
    expect(await (form.get('file') as File).text()).toBe('<svg/>');
    expect(form.get('comment')).toBe(
      `Published by DiaDraw from flow.draw (view main), sha256:${hashOf('<svg/>')}`,
    );
  });

  it('skips a diagram whose last version is the same, and updates a changed one', async () => {
    const same = `Published by DiaDraw from flow.draw (view main), sha256:${hashOf('<svg/>')}`;
    const unchanged = fakeSite([{ results: [{ id: 'att1', version: { message: same } }] }]);
    expect(await unchanged.confluence.publish('42', diagram)).toBe('unchanged');
    expect(unchanged.calls).toHaveLength(1);

    const changed = fakeSite([
      { results: [{ id: 'att1', metadata: { comment: 'sha256:0000' } }] },
      { results: [] },
    ]);
    expect(await changed.confluence.publish('42', diagram)).toBe('updated');
  });

  it('adds images only for attachments the page does not show yet', async () => {
    const { calls, confluence } = fakeSite([
      {
        id: '42',
        status: 'current',
        title: 'Design',
        body: {
          storage: { value: '<p><ac:image><ri:attachment ri:filename="a.svg" /></ac:image></p>' },
        },
        version: { number: 7 },
      },
      {},
    ]);
    expect(await confluence.insertImages('42', ['a.svg', 'b.svg'])).toEqual(['b.svg']);
    expect(calls[0]!.url).toBe(
      'https://example.atlassian.net/wiki/api/v2/pages/42?body-format=storage',
    );
    const update = JSON.parse(calls[1]!.body as string);
    expect(update.version.number).toBe(8);
    expect(update.body.value).toMatch(/ri:filename="a\.svg".*ri:filename="b\.svg"/);
    expect(update.title).toBe('Design');
  });

  it('reports what Confluence answered when a call fails', async () => {
    const confluence = new Confluence({
      baseUrl: 'https://example.atlassian.net',
      email: 'me@example.com',
      token: 'wrong',
      fetch: (async () =>
        new Response('Unauthorized', { status: 401, statusText: 'Unauthorized' })) as typeof fetch,
    });
    await expect(confluence.publish('42', diagram)).rejects.toThrow(
      /401 Unauthorized.*Unauthorized/,
    );
  });
});
