/** Shareable links keep the whole document in the URL hash, deflated and base64url-encoded. */
const PREFIX = 'v1:';

/**
 * The most a share link may decompress to (8 MiB, far more than any document's source). A link is
 * anyone's to craft, and deflate expands about a thousandfold, so a link of a few MB could
 * otherwise make the tab allocate gigabytes.
 */
export const MAX_SHARED_SOURCE_BYTES = 8 << 20;

/** Reads a stream to its end, failing once it has given more than `limit` bytes. */
async function readAtMost(
  stream: ReadableStream<Uint8Array>,
  limit: number,
): Promise<Uint8Array<ArrayBuffer>> {
  const reader = stream.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.length;
    if (size > limit) {
      await reader.cancel();
      throw new Error(`the shared document is larger than ${limit} bytes`);
    }
    chunks.push(value);
  }
  const out = new Uint8Array(size);
  let at = 0;
  for (const chunk of chunks) {
    out.set(chunk, at);
    at += chunk.length;
  }
  return out;
}

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

/**
 * Returns undefined for anything that is not a valid share token, and for one that decompresses
 * to more than `limit` bytes.
 */
export async function decodeSource(
  token: string,
  limit = MAX_SHARED_SOURCE_BYTES,
): Promise<string | undefined> {
  if (!token.startsWith(PREFIX)) return undefined;
  try {
    const stream = new Blob([fromBase64Url(token.slice(PREFIX.length))])
      .stream()
      .pipeThrough(new DecompressionStream('deflate-raw'));
    return new TextDecoder().decode(await readAtMost(stream, limit));
  } catch {
    return undefined;
  }
}
