import { describe, expect, it } from 'vitest';
import { decodeSource, encodeSource } from './share';

describe('share links', () => {
  it('round-trips source through a URL-safe token', async () => {
    const source = 'model {\n  a = process "Ünïcode ✓"\n}\n'.repeat(20);
    const token = await encodeSource(source);
    expect(token).toMatch(/^v1:[A-Za-z0-9_-]+$/);
    expect(token.length).toBeLessThan(source.length);
    expect(await decodeSource(token)).toBe(source);
  });

  it('rejects malformed tokens', async () => {
    expect(await decodeSource('nope')).toBeUndefined();
    expect(await decodeSource('v1:!!!')).toBeUndefined();
  });
});
