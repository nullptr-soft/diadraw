import { describe, expect, it } from 'vitest';
import { decodeSource, encodeSource, MAX_SHARED_SOURCE_BYTES } from './share';

describe('share links', () => {
  it('round-trips source through a URL-safe token', async () => {
    const source = 'model {\n  a = process "Ünïcode ✓"\n}\n'.repeat(20);
    const token = await encodeSource(source);
    expect(token).toMatch(/^v1:[A-Za-z0-9_-]+$/);
    expect(token.length).toBeLessThan(source.length);
    expect(await decodeSource(token)).toBe(source);
  });

  it('refuses a token that decompresses past the limit', async () => {
    const source = 'x'.repeat(100_000);
    const token = await encodeSource(source);
    expect(token.length).toBeLessThan(1_000);
    expect(await decodeSource(token, source.length)).toBe(source);
    expect(await decodeSource(token, source.length - 1)).toBeUndefined();
  });

  it('refuses a small token that would decompress to more than 8 MiB', async () => {
    const token = await encodeSource('\0'.repeat(MAX_SHARED_SOURCE_BYTES + 1));
    expect(token.length).toBeLessThan(20_000);
    expect(await decodeSource(token)).toBeUndefined();
  });

  it('rejects malformed tokens', async () => {
    expect(await decodeSource('nope')).toBeUndefined();
    expect(await decodeSource('v1:!!!')).toBeUndefined();
  });
});
