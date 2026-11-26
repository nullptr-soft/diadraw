/** Shareable links keep the whole document in the URL hash, deflated and base64url-encoded. */
const PREFIX = 'v1:';

async function pipe(
  data: Uint8Array<ArrayBuffer>,
  stream: CompressionStream | DecompressionStream,
): Promise<Uint8Array<ArrayBuffer>> {
  const out = new Blob([data]).stream().pipeThrough(stream);
  return new Uint8Array(await new Response(out).arrayBuffer());
}

function toBase64Url(bytes: Uint8Array): string {
  let binary = '';
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function fromBase64Url(text: string): Uint8Array<ArrayBuffer> {
  const binary = atob(text.replace(/-/g, '+').replace(/_/g, '/'));
  return Uint8Array.from(binary, (c) => c.charCodeAt(0));
}

export async function encodeSource(source: string): Promise<string> {
  const packed = await pipe(new TextEncoder().encode(source), new CompressionStream('deflate-raw'));
  return PREFIX + toBase64Url(packed);
}

/** Returns undefined for anything that is not a valid share token. */
export async function decodeSource(token: string): Promise<string | undefined> {
  if (!token.startsWith(PREFIX)) return undefined;
  try {
    const bytes = await pipe(
      fromBase64Url(token.slice(PREFIX.length)),
      new DecompressionStream('deflate-raw'),
    );
    return new TextDecoder().decode(bytes);
  } catch {
    return undefined;
  }
}
