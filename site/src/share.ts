/**
 * Links into the web app. The app keeps a shared document in the URL hash, deflated and
 * base64url-encoded behind `v1:`; this is the encoder from src/apps/web/src/share.ts, run at
 * build time.
 */

const PREFIX = 'v1:';

export async function shareToken(source: string): Promise<string> {
  const packed = new Blob([new TextEncoder().encode(source)])
    .stream()
    .pipeThrough(new CompressionStream('deflate-raw'));
  const bytes = new Uint8Array(await new Response(packed).arrayBuffer());
  return PREFIX + Buffer.from(bytes).toString('base64url');
}

/** The web app's address under the site's base, with an optional `#…` suffix. */
export function appUrl(suffix = ''): string {
  return `${import.meta.env.BASE_URL.replace(/\/?$/, '/')}app/${suffix}`;
}
