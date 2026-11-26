// Publishes the website and the web app from this machine to a gh-pages branch, for GitHub Pages
// set to "Deploy from a branch" (Settings → Pages → Source). Nothing runs in GitHub Actions to
// build it, so this works when the pages.yml workflow cannot run.
//
//   node tools/pages/publish-branch.mjs [--url <address>] [--remote origin] [--branch gh-pages]
//                                       [--out <dir>] [--no-build] [--dry-run]
//
// 1. Builds with tools/pages/build.mjs for --url (by default https://<owner>.github.io/<repo>/,
//    from the remote's GitHub address), unless --no-build reuses the last build in --out.
// 2. Adds .nojekyll, so Pages serves the folder as it is (Jekyll would drop _astro/).
// 3. Commits the folder as the branch's only commit and force-pushes it to the remote. The branch
//    holds build output only, so each publish replaces it; never point --branch at a source branch.
//
// --dry-run does everything but the push.

import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync, writeFileSync, cpSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../..', import.meta.url));
const { values } = parseArgs({
  options: {
    url: { type: 'string' },
    remote: { type: 'string', default: 'origin' },
    branch: { type: 'string', default: 'gh-pages' },
    out: { type: 'string' },
    'no-build': { type: 'boolean', default: false },
    'dry-run': { type: 'boolean', default: false },
  },
});

if (['main', 'master'].includes(values.branch)) {
  console.error(
    `Refusing to publish to ${values.branch}: the branch is replaced on every publish.`,
  );
  process.exit(1);
}

const git = (args, cwd = root) =>
  execFileSync('git', args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'] }).trim();

const remoteUrl = git(['remote', 'get-url', values.remote]);
const url = values.url ?? pagesUrl(remoteUrl);
const out = values.out
  ? resolve(values.out)
  : resolve(process.env.DIADRAW_BUILD_DIR ?? join(root, '../diadraw-build'), 'pages');

if (!values['no-build']) {
  execFileSync(
    process.execPath,
    [join(root, 'tools/pages/build.mjs'), '--url', url, '--out', out],
    {
      cwd: root,
      stdio: 'inherit',
    },
  );
} else if (!existsSync(join(out, 'index.html'))) {
  console.error(`${out} has no build; run without --no-build.`);
  process.exit(1);
}

// A throwaway repository holding a copy of the folder, so the build folder and this checkout
// stay untouched.
const work = mkdtempSync(join(tmpdir(), 'diadraw-pages-'));
try {
  cpSync(out, work, { recursive: true });
  writeFileSync(join(work, '.nojekyll'), '');
  const source = git(['rev-parse', '--short', 'HEAD']);
  const dirty = git(['status', '--porcelain']) ? ' with uncommitted changes' : '';
  git(['init', '--quiet', '--initial-branch', values.branch], work);
  git(['add', '--all'], work);
  git(
    [
      '-c',
      'commit.gpgsign=false',
      'commit',
      '--quiet',
      '-m',
      `Publish the website and the web app from ${source}${dirty}\n\nBuilt by tools/pages/publish-branch.mjs for ${url}`,
    ],
    work,
  );
  if (values['dry-run']) {
    console.log(`\nDry run: would push ${out} to ${values.remote}/${values.branch} (${url}).`);
  } else {
    console.log(`\n> git push --force ${values.remote} ${values.branch}`);
    execFileSync('git', ['push', '--force', remoteUrl, `HEAD:refs/heads/${values.branch}`], {
      cwd: work,
      stdio: 'inherit',
    });
    console.log(
      `\nPublished to ${values.branch}. With Settings → Pages → Source set to "Deploy from a branch"` +
        ` (${values.branch}, / (root)), it is served at ${url} in a minute or two.`,
    );
  }
} finally {
  rmSync(work, { recursive: true, force: true });
}

/** https://<owner>.github.io/<repo>/ from a GitHub remote, over HTTPS or SSH. */
function pagesUrl(remote) {
  const match = /github\.com[:/]([^/]+)\/(.+?)(?:\.git)?\/?$/.exec(remote);
  if (!match) {
    console.error(`${remote} is not a GitHub address; pass --url.`);
    process.exit(1);
  }
  const [, owner, repo] = match;
  return repo.toLowerCase() === `${owner.toLowerCase()}.github.io`
    ? `https://${repo}/`
    : `https://${owner}.github.io/${repo}/`;
}
