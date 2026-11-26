import { describe, expect, it } from 'vitest';
import { describeSystem, FEEDBACK_URL, feedbackUrl } from './feedback';

describe('feedback links', () => {
  it('opens the bug form with the app, version and system filled in', () => {
    const url = new URL(
      feedbackUrl('bug', { app: 'Web', version: '0.1.0', os: 'macOS · Chrome 131' }),
    );
    expect(`${url.origin}${url.pathname}`).toBe(FEEDBACK_URL);
    expect(url.searchParams.get('template')).toBe('bug_report.yml');
    expect(url.searchParams.get('app')).toBe('Web');
    expect(url.searchParams.get('version')).toBe('0.1.0');
    expect(url.searchParams.get('os')).toBe('macOS · Chrome 131');
    expect(url.search).not.toContain('+');
  });

  it('opens the feedback form and leaves out empty fields', () => {
    const url = new URL(feedbackUrl('feedback', { app: 'Desktop (Tauri)', version: '', os: '' }));
    expect(url.searchParams.get('template')).toBe('feedback.yml');
    expect(url.searchParams.get('app')).toBe('Desktop (Tauri)');
    expect(url.searchParams.has('version')).toBe(false);
    expect(url.searchParams.has('os')).toBe(false);
  });

  it('describes common browsers', () => {
    expect(
      describeSystem(
        'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36',
      ),
    ).toBe('macOS · Chrome 131');
    expect(
      describeSystem(
        'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0',
      ),
    ).toBe('Windows · Edge 131');
    expect(
      describeSystem(
        'Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0',
      ),
    ).toBe('Linux · Firefox 133');
    expect(
      describeSystem(
        'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.1 Safari/605.1.15',
      ),
    ).toBe('macOS · Safari 18.1');
    expect(describeSystem('')).toBe('');
  });
});
