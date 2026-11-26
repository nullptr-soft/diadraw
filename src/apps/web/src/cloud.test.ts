import { createHash, webcrypto } from 'node:crypto';
import { IDBFactory } from 'fake-indexeddb';
import { describe, expect, it } from 'vitest';
import type { AuthEnvironment } from './cloud';
import {
  drawFileName,
  GoogleAccount,
  GoogleDrive,
  MicrosoftAccount,
  OneDrive,
  pkce,
  SignInRequired,
} from './cloud';
import { DiagramLibrary } from './library';
import { browserLocation, cloudLocation } from './storage';

interface Call {
  url: string;
  method: string;
  headers: Record<string, string>;
  body: string | undefined;
}

/** A fetch that records calls and answers from `routes` (first match by method and URL prefix). */
function fakeFetch(routes: Array<[string, string, (call: Call) => Response]>) {
  const calls: Call[] = [];
  const fetcher = (async (input: RequestInfo | URL, init: RequestInit = {}) => {
    const call: Call = {
      url: String(input),
      method: init.method ?? 'GET',
      headers: Object.fromEntries(new Headers(init.headers).entries()),
      body: typeof init.body === 'string' ? init.body : undefined,
    };
    calls.push(call);
    const route = routes.find(([m, prefix]) => m === call.method && call.url.startsWith(prefix));
    if (!route) return new Response('{"error":{"message":"no route"}}', { status: 500 });
    return route[2](call);
  }) as typeof fetch;
  return { fetcher, calls };
}

const json = (value: unknown, status = 200) =>
  new Response(JSON.stringify(value), { status, headers: { 'Content-Type': 'application/json' } });

class MemoryStorage {
  map = new Map<string, string>();
  getItem(k: string) {
    return this.map.get(k) ?? null;
  }
  setItem(k: string, v: string) {
    this.map.set(k, v);
  }
  removeItem(k: string) {
    this.map.delete(k);
  }
}

function authEnv(
  answer: (url: URL) => string,
  overrides: Partial<AuthEnvironment> = {},
): AuthEnvironment & { opened: URL[]; clock: { now: number } } {
  const opened: URL[] = [];
  const clock = { now: 1_000_000 };
  let counter = 0;
  return {
    fetch: (() => Promise.reject(new Error('no fetch'))) as typeof fetch,
    popup: async (url) => {
      const u = new URL(url);
      opened.push(u);
      return new URL(answer(u));
    },
    redirectUri: 'https://app.example/oauth.html',
    storage: new MemoryStorage(),
    now: () => clock.now,
    random: (n) => new Uint8Array(n).fill(++counter),
    sha256: async (text) => new Uint8Array(createHash('sha256').update(text).digest()),
    opened,
    clock,
    ...overrides,
  };
}

const googleSignedIn = (url: URL) =>
  `https://app.example/oauth.html#access_token=tok-1&expires_in=3600&token_type=Bearer&state=${url.searchParams.get('state')}`;

describe('names', () => {
  it('adds .draw once', () => {
    expect(drawFileName('Login flow')).toBe('Login flow.draw');
    expect(drawFileName(' a.DRAW ')).toBe('a.DRAW');
    expect(drawFileName('  ')).toBe('Untitled.draw');
  });
});

describe('PKCE', () => {
  it('makes an S256 challenge of the verifier, base64url without padding', async () => {
    const { verifier, challenge } = await pkce({
      random: (n) => new Uint8Array(n).fill(7),
      sha256: async (t) =>
        new Uint8Array(await webcrypto.subtle.digest('SHA-256', new TextEncoder().encode(t))),
    });
    expect(verifier).toMatch(/^[A-Za-z0-9_-]{43}$/);
    expect(challenge).toBe(createHash('sha256').update(verifier).digest('base64url'));
  });
});

describe('Google sign-in', () => {
  it('asks for drive.file with the token flow and keeps the token', async () => {
    const env = authEnv(googleSignedIn);
    const account = new GoogleAccount('client-g', env);
    expect(account.configured).toBe(true);
    expect(account.signedIn).toBe(false);
    await expect(account.token()).rejects.toBeInstanceOf(SignInRequired);
    await account.signIn();
    const url = env.opened[0]!;
    expect(url.origin + url.pathname).toBe('https://accounts.google.com/o/oauth2/v2/auth');
    expect(url.searchParams.get('client_id')).toBe('client-g');
    expect(url.searchParams.get('response_type')).toBe('token');
    expect(url.searchParams.get('scope')).toBe('https://www.googleapis.com/auth/drive.file');
    expect(url.searchParams.get('redirect_uri')).toBe('https://app.example/oauth.html');
    expect(await account.token()).toBe('tok-1');
    // Another account object on the same storage (a reload) is still signed in.
    expect(await new GoogleAccount('client-g', env).token()).toBe('tok-1');
    // An hour later the token is gone and only a click can get another.
    env.clock.now += 3600_000;
    await expect(account.token()).rejects.toBeInstanceOf(SignInRequired);
    expect(account.signedIn).toBe(false);
  });

  it('rejects an answer to another request, and a refusal', async () => {
    await expect(
      new GoogleAccount(
        'c',
        authEnv(() => 'https://app.example/oauth.html#access_token=x&state=other'),
      ).signIn(),
    ).rejects.toThrow('different request');
    await expect(
      new GoogleAccount(
        'c',
        authEnv(
          (u) =>
            `https://app.example/oauth.html#error=access_denied&state=${u.searchParams.get('state')}`,
        ),
      ).signIn(),
    ).rejects.toThrow('cancelled');
  });

  it('is unavailable without a client ID', async () => {
    const account = new GoogleAccount(undefined, authEnv(googleSignedIn));
    expect(account.configured).toBe(false);
    await expect(account.signIn()).rejects.toThrow('not set up');
  });
});

describe('Microsoft sign-in', () => {
  it('swaps a code for tokens with PKCE, and refreshes silently', async () => {
    const { fetcher, calls } = fakeFetch([
      [
        'POST',
        'https://login.microsoftonline.com/common/oauth2/v2.0/token',
        (call) => {
          const form = new URLSearchParams(call.body);
          return form.get('grant_type') === 'authorization_code'
            ? json({ access_token: 'ms-1', refresh_token: 'r-1', expires_in: 3600 })
            : json({ access_token: 'ms-2', refresh_token: 'r-2', expires_in: 3600 });
        },
      ],
    ]);
    const env = authEnv(
      (u) => `https://app.example/oauth.html?code=the-code&state=${u.searchParams.get('state')}`,
      { fetch: fetcher },
    );
    const account = new MicrosoftAccount('client-m', env);
    await account.signIn();
    const url = env.opened[0]!;
    expect(url.pathname).toBe('/common/oauth2/v2.0/authorize');
    expect(url.searchParams.get('response_type')).toBe('code');
    expect(url.searchParams.get('code_challenge_method')).toBe('S256');
    expect(url.searchParams.get('scope')).toBe('Files.ReadWrite.AppFolder offline_access');
    const exchange = new URLSearchParams(calls[0]!.body);
    expect(exchange.get('code')).toBe('the-code');
    expect(exchange.get('client_id')).toBe('client-m');
    expect(createHash('sha256').update(exchange.get('code_verifier')!).digest('base64url')).toBe(
      url.searchParams.get('code_challenge'),
    );
    expect(await account.token()).toBe('ms-1');
    env.clock.now += 3600_000;
    expect(account.signedIn).toBe(true); // the refresh token still works
    expect(await account.token()).toBe('ms-2');
    expect(new URLSearchParams(calls[1]!.body).get('refresh_token')).toBe('r-1');
  });

  it('asks for a sign-in when the refresh token is refused', async () => {
    const { fetcher } = fakeFetch([
      [
        'POST',
        'https://login.microsoftonline.com/',
        (call) =>
          new URLSearchParams(call.body).get('grant_type') === 'authorization_code'
            ? json({ access_token: 'ms-1', refresh_token: 'r-1', expires_in: 60 })
            : json({ error: 'invalid_grant' }, 400),
      ],
    ]);
    const env = authEnv(
      (u) => `https://app.example/oauth.html?code=c&state=${u.searchParams.get('state')}`,
      {
        fetch: fetcher,
      },
    );
    const account = new MicrosoftAccount('client-m', env);
    await account.signIn();
    await expect(account.token()).rejects.toBeInstanceOf(SignInRequired);
    expect(account.signedIn).toBe(false);
  });
});

describe('Google Drive', () => {
  const signedIn = async (routes: Parameters<typeof fakeFetch>[0]) => {
    const env = authEnv(googleSignedIn);
    const account = new GoogleAccount('g', env);
    await account.signIn();
    const fake = fakeFetch(routes);
    return { drive: new GoogleDrive(account, fake.fetcher), calls: fake.calls, env };
  };

  it('lists every page of files, newest first, with the token', async () => {
    const { drive, calls } = await signedIn([
      [
        'GET',
        'https://www.googleapis.com/drive/v3/files?',
        (call) =>
          new URL(call.url).searchParams.get('pageToken')
            ? json({ files: [{ id: 'b', name: 'B.draw', modifiedTime: '2026-01-01T00:00:00Z' }] })
            : json({
                files: [
                  { id: 'a', name: 'A.draw', modifiedTime: '2026-02-01T00:00:00Z', size: '12' },
                ],
                nextPageToken: 'p2',
              }),
      ],
    ]);
    expect(await drive.list()).toEqual([
      { id: 'a', name: 'A.draw', updated: Date.parse('2026-02-01T00:00:00Z'), size: 12 },
      { id: 'b', name: 'B.draw', updated: Date.parse('2026-01-01T00:00:00Z') },
    ]);
    expect(calls).toHaveLength(2);
    expect(calls[0]!.headers.authorization).toBe('Bearer tok-1');
    expect(new URL(calls[0]!.url).searchParams.get('q')).toContain('trashed = false');
  });

  it('creates with a multipart upload, saves, renames and trashes', async () => {
    const file = { id: 'f1', name: 'Flow.draw', modifiedTime: '2026-03-01T00:00:00Z', size: '5' };
    const { drive, calls } = await signedIn([
      [
        'POST',
        'https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart',
        () => json(file),
      ],
      [
        'PATCH',
        'https://www.googleapis.com/upload/drive/v3/files/f1?uploadType=media',
        () => json(file),
      ],
      [
        'PATCH',
        'https://www.googleapis.com/drive/v3/files/f1',
        () => json({ ...file, name: 'New.draw' }),
      ],
    ]);
    expect((await drive.create('Flow', 'model {}\n')).id).toBe('f1');
    const create = calls[0]!;
    const boundary = /boundary=(.+)$/.exec(create.headers['content-type']!)![1]!;
    expect(create.body).toContain(`--${boundary}\r\nContent-Type: application/json`);
    expect(create.body).toContain('{"name":"Flow.draw","mimeType":"text/plain"}');
    expect(create.body).toContain('\r\n\r\nmodel {}\n\r\n');
    expect(create.body!.endsWith(`--${boundary}--`)).toBe(true);
    await drive.save('f1', 'model { a = system }\n');
    expect(calls[1]!.body).toBe('model { a = system }\n');
    expect((await drive.rename('f1', 'New')).name).toBe('New.draw');
    expect(JSON.parse(calls[2]!.body!)).toEqual({ name: 'New.draw' });
    await drive.remove('f1');
    expect(JSON.parse(calls[3]!.body!)).toEqual({ trashed: true });
  });

  it('reads a file, and a missing one as undefined', async () => {
    const { drive } = await signedIn([
      ['GET', 'https://www.googleapis.com/drive/v3/files/gone', () => json({}, 404)],
      [
        'GET',
        'https://www.googleapis.com/drive/v3/files/f1?alt=media',
        () => new Response('model {}\n'),
      ],
      [
        'GET',
        'https://www.googleapis.com/drive/v3/files/f1',
        () => json({ id: 'f1', name: 'F.draw' }),
      ],
    ]);
    expect(await drive.read('f1')).toEqual({
      file: { id: 'f1', name: 'F.draw', updated: 0 },
      text: 'model {}\n',
    });
    expect(await drive.read('gone')).toBeUndefined();
  });

  it('asks for a sign-in again after a 401, and reports other errors', async () => {
    const { drive } = await signedIn([
      [
        'GET',
        'https://www.googleapis.com/drive/v3/files/x',
        () => json({ error: { message: 'Bad id' } }, 400),
      ],
      ['GET', 'https://www.googleapis.com/drive/v3/files?', () => json({}, 401)],
    ]);
    await expect(drive.read('x')).rejects.toThrow('Google Drive: Bad id');
    await expect(drive.list()).rejects.toBeInstanceOf(SignInRequired);
    expect(drive.account.signedIn).toBe(false);
  });
});

describe('OneDrive', () => {
  const signedIn = async (routes: Parameters<typeof fakeFetch>[0]) => {
    const fake = fakeFetch([
      [
        'POST',
        'https://login.microsoftonline.com/',
        () => json({ access_token: 'ms', refresh_token: 'r', expires_in: 3600 }),
      ],
      ...routes,
    ]);
    const env = authEnv(
      (u) => `https://app.example/oauth.html?code=c&state=${u.searchParams.get('state')}`,
      {
        fetch: fake.fetcher,
      },
    );
    const account = new MicrosoftAccount('m', env);
    await account.signIn();
    fake.calls.length = 0;
    return { drive: new OneDrive(account, fake.fetcher), calls: fake.calls };
  };
  const GRAPH = 'https://graph.microsoft.com/v1.0/me/drive';

  it('lists the files in the app folder, following next links, skipping folders', async () => {
    const { drive, calls } = await signedIn([
      [
        'GET',
        `${GRAPH}/special/approot/children`,
        () =>
          json({
            value: [
              {
                id: 'a',
                name: 'A.draw',
                lastModifiedDateTime: '2026-01-01T00:00:00Z',
                size: 3,
                file: {},
              },
              { id: 'd', name: 'Folder', folder: {} },
            ],
            '@odata.nextLink': `${GRAPH}/next-page`,
          }),
      ],
      [
        'GET',
        `${GRAPH}/next-page`,
        () =>
          json({
            value: [
              { id: 'b', name: 'B.draw', lastModifiedDateTime: '2026-02-01T00:00:00Z', file: {} },
            ],
          }),
      ],
    ]);
    expect((await drive.list()).map((f) => f.id)).toEqual(['b', 'a']);
    expect(calls[0]!.headers.authorization).toBe('Bearer ms');
  });

  it('reads through the download URL without the token', async () => {
    const { drive, calls } = await signedIn([
      [
        'GET',
        `${GRAPH}/items/i1`,
        () =>
          json({
            id: 'i1',
            name: 'I.draw',
            '@microsoft.graph.downloadUrl': 'https://dl.example/i1',
          }),
      ],
      ['GET', 'https://dl.example/i1', () => new Response('model {}\n')],
    ]);
    expect((await drive.read('i1'))?.text).toBe('model {}\n');
    expect(calls[1]!.headers.authorization).toBeUndefined();
  });

  it('creates in the app folder without overwriting, saves, renames and deletes', async () => {
    const item = { id: 'i1', name: 'Flow.draw', file: {} };
    const { drive, calls } = await signedIn([
      ['PUT', `${GRAPH}/special/approot:/`, () => json(item, 201)],
      ['PUT', `${GRAPH}/items/i1/content`, () => json(item)],
      ['PATCH', `${GRAPH}/items/i1`, () => json({ ...item, name: 'Renamed.draw' })],
      ['DELETE', `${GRAPH}/items/i1`, () => new Response(null, { status: 204 })],
    ]);
    await drive.create('Flow / v2', 'x');
    expect(calls[0]!.url).toBe(
      `${GRAPH}/special/approot:/Flow%20%2F%20v2.draw:/content?@microsoft.graph.conflictBehavior=rename`,
    );
    await drive.save('i1', 'y');
    expect(calls[1]!.body).toBe('y');
    expect((await drive.rename('i1', 'Renamed')).name).toBe('Renamed.draw');
    await drive.remove('i1');
    expect(calls[3]!.method).toBe('DELETE');
  });
});

describe('locations', () => {
  it('puts the browser library and a cloud behind one interface', async () => {
    const library = await DiagramLibrary.open(new IDBFactory());
    const browser = browserLocation(library);
    const created = await browser.create('Flow', 'model {}\n');
    expect(await browser.list()).toEqual([
      { id: created.id, name: 'Flow', updated: created.updated, size: 9 },
    ]);
    expect((await browser.read(created.id))?.text).toBe('model {}\n');
    expect((await browser.rename(created.id, 'Other'))?.name).toBe('Other');
    await browser.remove(created.id);
    expect(await browser.list()).toEqual([]);
    library.close();

    const drive = new GoogleDrive(new GoogleAccount(undefined, authEnv(googleSignedIn)), fetch);
    const cloud = cloudLocation(drive);
    expect(cloud.label).toBe('Google Drive');
    expect(cloud.cloud).toBe(drive);
  });
});
