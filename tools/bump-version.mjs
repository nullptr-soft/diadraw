// Bumps DiaDraw's version: `npm run version:bump -- patch|minor|major|X.Y.Z`. It writes the new
// version to VERSION, adds a release for it to the GTK app's AppStream metadata, and then runs
// check-version.mjs --write to update every other copy (package manifests, lock files, Cargo.toml,
// tauri.conf.json, the Claude Code plugin, the bug report template). CMake, the site and the apps
// read VERSION itself, so nothing else needs to change.
//
// It then commits exactly the files it changed ("Bump the version to X.Y.Z"), leaving any other
// change in the working tree or the index out of the commit. It refuses to start when one of those
// files already has uncommitted changes, so that the commit holds only the bump. Pass --no-commit
// to update the files without committing. It does not tag.
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { versionFiles } from './check-version.mjs';

const root = new URL('../', import.meta.url);
const read = (path) => readFileSync(new URL(path, root), 'utf8');
const write = (path, text) => writeFileSync(new URL(path, root), text);

const git = (...args) =>
  execFileSync('git', args, { cwd: fileURLToPath(root), encoding: 'utf8' }).trimEnd();

const parse = (version) => {
  const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(version);
  return match && match.slice(1).map(Number);
};

/** The version after `current` for a bump of `part` (patch, minor or major) or an explicit X.Y.Z. */
export function nextVersion(current, part) {
  const [major, minor, patch] = parse(current) ?? [];
  if (major === undefined) throw new Error(`VERSION holds "${current}", not MAJOR.MINOR.PATCH`);
  if (part === 'major') return `${major + 1}.0.0`;
  if (part === 'minor') return `${major}.${minor + 1}.0`;
  if (part === 'patch') return `${major}.${minor}.${patch + 1}`;
  const wanted = parse(part ?? '');
  if (!wanted) throw new Error(`expected patch, minor, major or X.Y.Z, not "${part ?? ''}"`);
  const order = (a, b) => a.findIndex((n, i) => n !== b[i]);
  const at = order(wanted, [major, minor, patch]);
  if (at < 0 || wanted[at] < [major, minor, patch][at]) {
    throw new Error(`${part} is not later than the current version ${current}`);
  }
  return wanted.join('.');
}

/** Adds `<release version=… date=…/>` as the newest release, unless that version is listed. */
export function addRelease(metainfo, version, date) {
  if (metainfo.includes(`<release version="${version}"`)) return metainfo;
  return metainfo.replace(
    /^( *)<release /m,
    (line, indent) => `${indent}<release version="${version}" date="${date}"/>\n${line}`,
  );
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const metainfoPath = 'src/apps/gtk/data/com.diadraw.DiaDraw.metainfo.xml';
  const args = process.argv.slice(2);
  const commit = !args.includes('--no-commit');
  const [part, ...extra] = args.filter((arg) => arg !== '--no-commit');
  const files = [...new Set(['VERSION', metainfoPath, ...versionFiles])];
  try {
    if (extra.length) throw new Error(`unexpected arguments: ${extra.join(' ')}`);
    const current = read('VERSION').trim();
    const next = nextVersion(current, part);
    if (commit) {
      const dirty = git('status', '--porcelain', '--', ...files);
      if (dirty) {
        throw new Error(
          `these files have uncommitted changes, so the bump commit would include them:\n${dirty}\n` +
            'Commit or stash them first, or pass --no-commit.',
        );
      }
    }
    const metainfo = read(metainfoPath);
    const updated = addRelease(metainfo, next, new Date().toISOString().slice(0, 10));
    if (updated === metainfo && !metainfo.includes(`<release version="${next}"`)) {
      throw new Error(`${metainfoPath}: no <release> element to add the new release before`);
    }
    write('VERSION', `${next}\n`);
    write(metainfoPath, updated);
    console.log(`VERSION: ${current} → ${next}`);
    execFileSync(
      process.execPath,
      [fileURLToPath(new URL('check-version.mjs', import.meta.url)), '--write'],
      {
        stdio: 'inherit',
      },
    );
    if (commit) {
      const changed = git('diff', '--name-only', '--', ...files)
        .split('\n')
        .filter(Boolean);
      // --only commits these paths as they are in the working tree and nothing else, whatever
      // else is staged.
      git('commit', '--only', '--quiet', '-m', `Bump the version to ${next}`, '--', ...changed);
      console.log(`Committed ${changed.length} files: ${git('log', '-1', '--format=%h %s')}`);
    }
  } catch (error) {
    console.error(`version:bump: ${error.message}`);
    console.error('Usage: npm run version:bump -- patch|minor|major|X.Y.Z [--no-commit]');
    process.exit(1);
  }
}
