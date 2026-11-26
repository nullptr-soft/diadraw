/**
 * Report a bug and Give feedback: the address of a new issue on the project's tracker, with the
 * issue form (.github/ISSUE_TEMPLATE) chosen and the app, version and system filled in. The same
 * address the native apps open (src/host/src/feedback.cpp). Nothing is sent from the app, and the
 * diagram is never included.
 */

/** Keep in step with DIADRAW_FEEDBACK_URL in src/host/CMakeLists.txt by hand. */
export const FEEDBACK_URL = 'https://github.com/mykulyak/diadraw/issues/new';

export type FeedbackKind = 'bug' | 'feedback';

export interface FeedbackSource {
  /** The issue form's app choice: `Web` or `Desktop (Tauri)`. */
  app: string;
  /** The core's version. */
  version: string;
  /** The system and browser, as {@link describeSystem} gives it. */
  os: string;
}

export function feedbackUrl(kind: FeedbackKind, source: FeedbackSource): string {
  const params = new URLSearchParams({
    template: kind === 'bug' ? 'bug_report.yml' : 'feedback.yml',
  });
  for (const [key, value] of Object.entries(source)) if (value) params.set(key, value);
  return `${FEEDBACK_URL}?${params.toString().replaceAll('+', '%20')}`;
}

/** "macOS · Chrome 131" from a user agent string; the parts it cannot tell are left out. */
export function describeSystem(userAgent: string): string {
  const os = /Windows NT/.test(userAgent)
    ? 'Windows'
    : /iPhone|iPad|iPod/.test(userAgent)
      ? 'iOS'
      : /Mac OS X|Macintosh/.test(userAgent)
        ? 'macOS'
        : /Android/.test(userAgent)
          ? 'Android'
          : /CrOS/.test(userAgent)
            ? 'ChromeOS'
            : /Linux/.test(userAgent)
              ? 'Linux'
              : '';
  // Order matters: Edge and Opera say Chrome too, and Chrome says Safari.
  const browsers: [string, RegExp][] = [
    ['Firefox', /Firefox\/(\d+)/],
    ['Edge', /Edg\/(\d+)/],
    ['Opera', /OPR\/(\d+)/],
    ['Chrome', /Chrome\/(\d+)/],
    ['Safari', /Version\/(\d+(?:\.\d+)?).*Safari\//],
  ];
  let browser = '';
  for (const [name, pattern] of browsers) {
    const match = pattern.exec(userAgent);
    if (match) {
      browser = `${name} ${match[1]}`;
      break;
    }
  }
  return [os, browser].filter(Boolean).join(' · ');
}
