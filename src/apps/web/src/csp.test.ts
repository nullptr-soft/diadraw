import { describe, expect, it } from 'vitest';
import { CLOUD_HOSTS, contentSecurityPolicy } from './csp';

const directives = (policy: string) =>
  new Map(
    policy.split('; ').map((d) => {
      const [name, ...sources] = d.split(' ');
      return [name!, sources] as const;
    }),
  );

describe('contentSecurityPolicy', () => {
  it('runs only the app’s own scripts and the WebAssembly core', () => {
    const p = directives(contentSecurityPolicy());
    expect(p.get('script-src')).toEqual(["'self'", "'wasm-unsafe-eval'"]);
    expect(p.get('object-src')).toEqual(["'none'"]);
    expect(p.get('base-uri')).toEqual(["'self'"]);
    for (const sources of p.values()) {
      expect(sources).not.toContain("'unsafe-eval'");
      expect(sources).not.toContain('*');
    }
  });

  it('lets the Storage dialog reach Google Drive and OneDrive', () => {
    const connect = directives(contentSecurityPolicy()).get('connect-src')!;
    expect(connect).toEqual(["'self'", ...CLOUD_HOSTS]);
    expect(connect).not.toContain('ws:');
  });

  it('lets Vite’s development client connect to its server', () => {
    expect(directives(contentSecurityPolicy({ dev: true })).get('connect-src')).toContain('ws:');
  });
});
