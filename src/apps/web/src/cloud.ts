/**
 * Google Drive and OneDrive for the web app: signing in with OAuth 2.0 in a popup, and reading,
 * writing, listing, renaming and deleting diagrams through each service's REST API.
 *
 * DiaDraw asks for the narrowest access each service offers:
 *
 * - Google Drive, `drive.file`: only the files DiaDraw created (or that were opened with it).
 *   Diagrams are saved in the root of My Drive as `<name>.draw`.
 * - OneDrive, `Files.ReadWrite.AppFolder`: only DiaDraw's own folder, which OneDrive shows as
 *   `Apps/DiaDraw`. Files put there by hand show up too.
 *
 * Each service needs an OAuth client ID registered by whoever hosts the app (see
 * docs/guide/cloud-storage.md); without one, that service is shown as unavailable. The IDs come
 * from the build (`VITE_GOOGLE_CLIENT_ID`, `VITE_ONEDRIVE_CLIENT_ID`).
 *
 * Tokens stay in this tab's sessionStorage, so a reload keeps you signed in until the token runs
 * out (about an hour); nothing is kept once the tab is closed.
 */

import type { StoredFile } from './storage';

export type CloudId = 'gdrive' | 'onedrive';

/** Thrown when an operation needs a sign-in that only a click can start (popups need one). */
export class SignInRequired extends Error {
  constructor(readonly cloud: CloudId) {
    super(`Sign in to ${cloudLabel(cloud)} first`);
    this.name = 'SignInRequired';
  }
}

export const cloudLabel = (id: CloudId): string => (id === 'gdrive' ? 'Google Drive' : 'OneDrive');

/** The name a diagram goes by as a file: `name.draw`. */
export const drawFileName = (name: string): string =>
  /\.draw$/i.test(name.trim()) ? name.trim() : `${name.trim() || 'Untitled'}.draw`;

// ── Signing in ──

interface Token {
  readonly access: string;
  /** Milliseconds since the epoch. */
  readonly expires: number;
  readonly refresh?: string;
}

/** What a provider's sign-in needs from the page; tests replace all of it. */
export interface AuthEnvironment {
  fetch: typeof fetch;
  /** Opens `url` in a popup and resolves to the URL it was redirected back to (with `state`). */
  popup(url: string, state: string): Promise<URL>;
  /** Where the provider redirects back to: the app's oauth.html. */
  redirectUri: string;
  storage: Pick<Storage, 'getItem' | 'setItem' | 'removeItem'> | undefined;
  now(): number;
  random(bytes: number): Uint8Array;
  sha256(text: string): Promise<Uint8Array>;
}

const base64url = (bytes: Uint8Array): string =>
  btoa(String.fromCharCode(...bytes))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');

/** A PKCE verifier and its S256 challenge (RFC 7636). */
export async function pkce(env: Pick<AuthEnvironment, 'random' | 'sha256'>): Promise<{
  verifier: string;
  challenge: string;
}> {
  const verifier = base64url(env.random(32));
  return { verifier, challenge: base64url(await env.sha256(verifier)) };
}

/** Parameters of a redirect: the query and the fragment together. */
const redirectParams = (url: URL): URLSearchParams => {
  const params = new URLSearchParams(url.search);
  for (const [k, v] of new URLSearchParams(url.hash.replace(/^#/, ''))) params.set(k, v);
  return params;
};

const oauthError = (params: URLSearchParams): Error | undefined => {
  const error = params.get('error');
  if (!error) return undefined;
  const text = params.get('error_description') ?? error;
  return new Error(error === 'access_denied' ? 'Sign-in was cancelled' : text);
};

/**
 * A signed-in (or not) account with one service. `token()` hands out a valid access token, renewing
 * it silently when a refresh token allows; `signIn()` opens the popup and must run from a click.
 */
export abstract class CloudAccount {
  #token: Token | undefined;

  protected constructor(
    readonly id: CloudId,
    readonly clientId: string | undefined,
    protected readonly env: AuthEnvironment,
  ) {
    this.#token = this.#readToken();
  }

  get label(): string {
    return cloudLabel(this.id);
  }

  /** Whether this build has a client ID for the service. */
  get configured(): boolean {
    return !!this.clientId;
  }

  get signedIn(): boolean {
    return !!this.#token && (this.#token.expires > this.env.now() || !!this.#token.refresh);
  }

  async signIn(): Promise<void> {
    if (!this.clientId) throw new Error(`${this.label} is not set up in this build of DiaDraw`);
    this.#store(await this.authorize(this.clientId));
  }

  signOut(): void {
    this.#store(undefined);
  }

  /** A valid access token; throws SignInRequired when only a new sign-in can get one. */
  async token(): Promise<string> {
    const token = this.#token;
    // A minute's margin, so a token does not run out mid-request.
    if (token && token.expires - 60_000 > this.env.now()) return token.access;
    if (token?.refresh && this.clientId) {
      try {
        this.#store(await this.refresh(this.clientId, token.refresh));
        return this.#token!.access;
      } catch {
        // Refresh token expired or revoked: sign in again.
      }
    }
    this.#store(undefined);
    throw new SignInRequired(this.id);
  }

  /** Forgets the access token (after a 401), keeping a refresh token to try. */
  expire(): void {
    const token = this.#token;
    if (token) this.#store({ ...token, expires: 0 });
  }

  protected abstract authorize(clientId: string): Promise<Token>;
  protected abstract refresh(clientId: string, refreshToken: string): Promise<Token>;

  get #key(): string {
    return `diadraw.cloud.${this.id}`;
  }

  #readToken(): Token | undefined {
    try {
      const raw = this.env.storage?.getItem(this.#key);
      const token = raw ? (JSON.parse(raw) as Token) : undefined;
      return token && typeof token.access === 'string' ? token : undefined;
    } catch {
      return undefined;
    }
  }

  #store(token: Token | undefined): void {
    this.#token = token;
    try {
      if (token) this.env.storage?.setItem(this.#key, JSON.stringify(token));
      else this.env.storage?.removeItem(this.#key);
    } catch {
      // Storage blocked: the token lives in memory only.
    }
  }

  protected newState(): string {
    return base64url(this.env.random(16));
  }

  protected tokenFrom(json: Record<string, unknown>): Token {
    const access = json.access_token;
    if (typeof access !== 'string') throw new Error(`${this.label} did not return an access token`);
    const seconds = Number(json.expires_in ?? 3600);
    return {
      access,
      expires: this.env.now() + (Number.isFinite(seconds) ? seconds : 3600) * 1000,
      ...(typeof json.refresh_token === 'string' && { refresh: json.refresh_token }),
    };
  }
}

export const GOOGLE_SCOPE = 'https://www.googleapis.com/auth/drive.file';

/**
 * Google: the token flow for browser apps (a "Web application" client cannot swap a code for a
 * token without its secret). Tokens last an hour; then the user signs in again with one click,
 * which Google answers without asking while the grant stands.
 */
export class GoogleAccount extends CloudAccount {
  constructor(clientId: string | undefined, env: AuthEnvironment) {
    super('gdrive', clientId, env);
  }

  protected async authorize(clientId: string): Promise<Token> {
    const state = this.newState();
    const url = new URL('https://accounts.google.com/o/oauth2/v2/auth');
    url.search = new URLSearchParams({
      client_id: clientId,
      redirect_uri: this.env.redirectUri,
      response_type: 'token',
      scope: GOOGLE_SCOPE,
      include_granted_scopes: 'true',
      state,
    }).toString();
    const params = redirectParams(await this.env.popup(url.toString(), state));
    const error = oauthError(params);
    if (error) throw error;
    if (params.get('state') !== state) throw new Error('Sign-in answered a different request');
    return this.tokenFrom(Object.fromEntries(params));
  }

  protected refresh(): Promise<Token> {
    return Promise.reject(new Error('Google tokens in the browser cannot be refreshed'));
  }
}

export const ONEDRIVE_SCOPE = 'Files.ReadWrite.AppFolder offline_access';
const MS_AUTHORITY = 'https://login.microsoftonline.com/common/oauth2/v2.0';

/**
 * Microsoft: the authorization code flow with PKCE, as a single-page application (no secret).
 * `common` lets both personal Microsoft accounts and work or school accounts sign in.
 */
export class MicrosoftAccount extends CloudAccount {
  constructor(clientId: string | undefined, env: AuthEnvironment) {
    super('onedrive', clientId, env);
  }

  protected async authorize(clientId: string): Promise<Token> {
    const state = this.newState();
    const { verifier, challenge } = await pkce(this.env);
    const url = new URL(`${MS_AUTHORITY}/authorize`);
    url.search = new URLSearchParams({
      client_id: clientId,
      redirect_uri: this.env.redirectUri,
      response_type: 'code',
      response_mode: 'query',
      scope: ONEDRIVE_SCOPE,
      code_challenge: challenge,
      code_challenge_method: 'S256',
      prompt: 'select_account',
      state,
    }).toString();
    const params = redirectParams(await this.env.popup(url.toString(), state));
    const error = oauthError(params);
    if (error) throw error;
    const code = params.get('code');
    if (params.get('state') !== state || !code)
      throw new Error('Sign-in answered a different request');
    return this.#tokenRequest({
      client_id: clientId,
      grant_type: 'authorization_code',
      code,
      redirect_uri: this.env.redirectUri,
      code_verifier: verifier,
      scope: ONEDRIVE_SCOPE,
    });
  }

  protected refresh(clientId: string, refreshToken: string): Promise<Token> {
    return this.#tokenRequest({
      client_id: clientId,
      grant_type: 'refresh_token',
      refresh_token: refreshToken,
      scope: ONEDRIVE_SCOPE,
    });
  }

  async #tokenRequest(form: Record<string, string>): Promise<Token> {
    const response = await this.env.fetch(`${MS_AUTHORITY}/token`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams(form).toString(),
    });
    const json = (await response.json().catch(() => ({}))) as Record<string, unknown>;
    if (!response.ok) {
      throw new Error(String(json.error_description ?? json.error ?? `HTTP ${response.status}`));
    }
    return this.tokenFrom(json);
  }
}

// ── Files ──

/** A cloud folder of diagrams, the same operations as the browser's library. */
export abstract class CloudDrive {
  constructor(
    readonly account: CloudAccount,
    protected readonly fetcher: typeof fetch,
  ) {}

  abstract list(): Promise<StoredFile[]>;
  abstract read(id: string): Promise<{ file: StoredFile; text: string } | undefined>;
  abstract create(name: string, text: string): Promise<StoredFile>;
  abstract save(id: string, text: string): Promise<StoredFile>;
  abstract rename(id: string, name: string): Promise<StoredFile>;
  abstract remove(id: string): Promise<void>;

  /** An authorised request; on 401 renews the token once. 404 resolves to undefined. */
  protected async call(
    url: string,
    init: RequestInit = {},
    allowMissing = false,
  ): Promise<Response | undefined> {
    for (let attempt = 0; ; ++attempt) {
      const token = await this.account.token();
      const headers = new Headers(init.headers);
      headers.set('Authorization', `Bearer ${token}`);
      const response = await this.fetcher(url, { ...init, headers });
      if (response.status === 401 && attempt === 0) {
        this.account.expire();
        continue;
      }
      if (response.status === 404 && allowMissing) return undefined;
      if (!response.ok) throw new Error(`${this.account.label}: ${await errorText(response)}`);
      return response;
    }
  }

  protected async json<T>(url: string, init: RequestInit = {}): Promise<T> {
    return (await (await this.call(url, init))!.json()) as T;
  }
}

async function errorText(response: Response): Promise<string> {
  try {
    const json = (await response.json()) as { error?: { message?: string } | string };
    const message = typeof json.error === 'string' ? json.error : json.error?.message;
    if (message) return message;
  } catch {
    // Not JSON.
  }
  return `HTTP ${response.status} ${response.statusText}`.trim();
}

const DRIVE = 'https://www.googleapis.com/drive/v3/files';
const DRIVE_UPLOAD = 'https://www.googleapis.com/upload/drive/v3/files';
const DRIVE_FIELDS = 'id,name,modifiedTime,size';

interface DriveFile {
  id: string;
  name: string;
  modifiedTime?: string;
  size?: string;
}

const fromDrive = (f: DriveFile): StoredFile => ({
  id: f.id,
  name: f.name,
  updated: f.modifiedTime ? Date.parse(f.modifiedTime) : 0,
  ...(f.size !== undefined && { size: Number(f.size) }),
});

export class GoogleDrive extends CloudDrive {
  async list(): Promise<StoredFile[]> {
    const files: StoredFile[] = [];
    let page: string | undefined;
    do {
      const url = new URL(DRIVE);
      url.search = new URLSearchParams({
        q: "trashed = false and mimeType != 'application/vnd.google-apps.folder'",
        spaces: 'drive',
        orderBy: 'modifiedTime desc',
        pageSize: '200',
        fields: `nextPageToken,files(${DRIVE_FIELDS})`,
        ...(page && { pageToken: page }),
      }).toString();
      const json = await this.json<{ files?: DriveFile[]; nextPageToken?: string }>(url.toString());
      files.push(...(json.files ?? []).map(fromDrive));
      page = json.nextPageToken;
    } while (page);
    return files;
  }

  async read(id: string): Promise<{ file: StoredFile; text: string } | undefined> {
    const meta = await this.call(
      `${DRIVE}/${encodeURIComponent(id)}?fields=${DRIVE_FIELDS}`,
      {},
      true,
    );
    if (!meta) return undefined;
    const file = fromDrive((await meta.json()) as DriveFile);
    const body = await this.call(`${DRIVE}/${encodeURIComponent(id)}?alt=media`, {}, true);
    return body ? { file, text: await body.text() } : undefined;
  }

  async create(name: string, text: string): Promise<StoredFile> {
    const boundary = `diadraw-${base64url(crypto.getRandomValues(new Uint8Array(12)))}`;
    const metadata = { name: drawFileName(name), mimeType: 'text/plain' };
    const body =
      `--${boundary}\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n${JSON.stringify(metadata)}\r\n` +
      `--${boundary}\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n${text}\r\n--${boundary}--`;
    return fromDrive(
      await this.json<DriveFile>(`${DRIVE_UPLOAD}?uploadType=multipart&fields=${DRIVE_FIELDS}`, {
        method: 'POST',
        headers: { 'Content-Type': `multipart/related; boundary=${boundary}` },
        body,
      }),
    );
  }

  async save(id: string, text: string): Promise<StoredFile> {
    return fromDrive(
      await this.json<DriveFile>(
        `${DRIVE_UPLOAD}/${encodeURIComponent(id)}?uploadType=media&fields=${DRIVE_FIELDS}`,
        { method: 'PATCH', headers: { 'Content-Type': 'text/plain; charset=UTF-8' }, body: text },
      ),
    );
  }

  async rename(id: string, name: string): Promise<StoredFile> {
    return fromDrive(
      await this.json<DriveFile>(`${DRIVE}/${encodeURIComponent(id)}?fields=${DRIVE_FIELDS}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: drawFileName(name) }),
      }),
    );
  }

  /** Moves the file to Drive's trash, from where it can be restored for 30 days. */
  async remove(id: string): Promise<void> {
    await this.call(`${DRIVE}/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ trashed: true }),
    });
  }
}

const GRAPH = 'https://graph.microsoft.com/v1.0/me/drive';
const GRAPH_SELECT = '$select=id,name,lastModifiedDateTime,size,file';

interface DriveItem {
  id: string;
  name: string;
  lastModifiedDateTime?: string;
  size?: number;
  file?: unknown;
  '@microsoft.graph.downloadUrl'?: string;
}

const fromGraph = (item: DriveItem): StoredFile => ({
  id: item.id,
  name: item.name,
  updated: item.lastModifiedDateTime ? Date.parse(item.lastModifiedDateTime) : 0,
  ...(item.size !== undefined && { size: item.size }),
});

export class OneDrive extends CloudDrive {
  async list(): Promise<StoredFile[]> {
    const files: StoredFile[] = [];
    let next: string | undefined = `${GRAPH}/special/approot/children?${GRAPH_SELECT}&$top=200`;
    while (next) {
      const json: { value?: DriveItem[]; '@odata.nextLink'?: string } = await this.json(next);
      files.push(...(json.value ?? []).filter((i) => i.file).map(fromGraph));
      next = json['@odata.nextLink'];
    }
    return files.sort((a, b) => b.updated - a.updated || a.name.localeCompare(b.name));
  }

  async read(id: string): Promise<{ file: StoredFile; text: string } | undefined> {
    // The item's short-lived download URL needs no token (and avoids a cross-origin redirect).
    const meta = await this.call(`${GRAPH}/items/${encodeURIComponent(id)}`, {}, true);
    if (!meta) return undefined;
    const item = (await meta.json()) as DriveItem;
    const download = item['@microsoft.graph.downloadUrl'];
    if (!download) throw new Error('OneDrive gave no download address for this file');
    const body = await this.fetcher(download);
    if (!body.ok) throw new Error(`OneDrive: ${await errorText(body)}`);
    return { file: fromGraph(item), text: await body.text() };
  }

  async create(name: string, text: string): Promise<StoredFile> {
    const path = encodeURIComponent(drawFileName(name));
    return fromGraph(
      await this.json<DriveItem>(
        `${GRAPH}/special/approot:/${path}:/content?@microsoft.graph.conflictBehavior=rename`,
        { method: 'PUT', headers: { 'Content-Type': 'text/plain' }, body: text },
      ),
    );
  }

  async save(id: string, text: string): Promise<StoredFile> {
    return fromGraph(
      await this.json<DriveItem>(`${GRAPH}/items/${encodeURIComponent(id)}/content`, {
        method: 'PUT',
        headers: { 'Content-Type': 'text/plain' },
        body: text,
      }),
    );
  }

  async rename(id: string, name: string): Promise<StoredFile> {
    return fromGraph(
      await this.json<DriveItem>(`${GRAPH}/items/${encodeURIComponent(id)}`, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: drawFileName(name) }),
      }),
    );
  }

  /** Moves the item to the OneDrive recycle bin. */
  async remove(id: string): Promise<void> {
    await this.call(`${GRAPH}/items/${encodeURIComponent(id)}`, { method: 'DELETE' });
  }
}

// ── The page's environment ──

const MESSAGE = 'diadraw-oauth';

/**
 * Opens `url` in a popup and waits for oauth.html to report where the provider sent it back.
 * oauth.html answers through both `window.opener` and a BroadcastChannel, because a provider's
 * Cross-Origin-Opener-Policy can cut the popup off from its opener.
 */
export function browserPopup(url: string, state: string): Promise<URL> {
  const popup = window.open(url, 'diadraw-sign-in', 'popup,width=520,height=680');
  if (!popup) {
    return Promise.reject(new Error('The sign-in window was blocked; allow popups for DiaDraw'));
  }
  return new Promise((resolve, reject) => {
    const channel = 'BroadcastChannel' in window ? new BroadcastChannel(MESSAGE) : undefined;
    const finish = (result: URL | Error) => {
      window.removeEventListener('message', onMessage);
      channel?.close();
      clearTimeout(timer);
      if (result instanceof Error) reject(result);
      else resolve(result);
    };
    const accept = (data: unknown) => {
      const message = data as { type?: string; url?: string } | null;
      if (message?.type !== MESSAGE || typeof message.url !== 'string') return;
      const answer = new URL(message.url);
      if (redirectParams(answer).get('state') === state) finish(answer);
    };
    const onMessage = (e: MessageEvent) => {
      if (e.origin === location.origin) accept(e.data);
    };
    window.addEventListener('message', onMessage);
    if (channel) channel.onmessage = (e) => accept(e.data);
    const timer = setTimeout(() => finish(new Error('Sign-in timed out')), 5 * 60_000);
  });
}

/** The page's sign-in environment: real popups, fetch, sessionStorage and Web Crypto. */
export function browserAuthEnvironment(): AuthEnvironment {
  let storage: Storage | undefined;
  try {
    storage = sessionStorage;
  } catch {
    storage = undefined;
  }
  return {
    fetch: (...args) => fetch(...args),
    popup: browserPopup,
    redirectUri: new URL('oauth.html', document.baseURI).toString(),
    storage,
    now: () => Date.now(),
    random: (n) => crypto.getRandomValues(new Uint8Array(n)),
    sha256: async (text) =>
      new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))),
  };
}

/** Google Drive and OneDrive as this build is set up for them. */
export function browserClouds(): { gdrive: GoogleDrive; onedrive: OneDrive } {
  const auth = browserAuthEnvironment();
  const fetcher: typeof fetch = (...args) => fetch(...args);
  return {
    gdrive: new GoogleDrive(
      new GoogleAccount(import.meta.env.VITE_GOOGLE_CLIENT_ID || undefined, auth),
      fetcher,
    ),
    onedrive: new OneDrive(
      new MicrosoftAccount(import.meta.env.VITE_ONEDRIVE_CLIENT_ID || undefined, auth),
      fetcher,
    ),
  };
}
