// Builds the C++ core for WebAssembly (the root `wasm` CMake preset) and copies the module
// into dist/core/. Needs Emscripten (EMSDK pointing at an emsdk checkout, or one in ~/emsdk or
// ~/Software/emsdk), CMake and Ninja.

import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { delimiter, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('..', import.meta.url));
const root = join(here, '../..');
// Presets build outside the repo, in the sibling folder diadraw-build/<preset>; DIADRAW_BUILD_DIR
// moves that folder (worktrees side by side should not share one).
const out = process.env.DIADRAW_BUILD_DIR
  ? resolve(process.env.DIADRAW_BUILD_DIR, 'wasm')
  : join(root, '../diadraw-build/wasm');

const env = { ...process.env };
const windows = process.platform === 'win32';
// Windows spells the variable Path; spreading process.env loses its case-insensitivity.
const pathKey = windows
  ? (Object.keys(env).find((k) => k.toUpperCase() === 'PATH') ?? 'Path')
  : 'PATH';
const prepend = (dir) => {
  env[pathKey] = [dir, env[pathKey]].join(delimiter);
};
if (!env.EMSDK) {
  for (const guess of [join(homedir(), 'emsdk'), join(homedir(), 'Software/emsdk')]) {
    if (existsSync(join(guess, 'upstream/emscripten/emcc.py'))) {
      env.EMSDK = guess;
      break;
    }
  }
}
if (!env.EMSDK) {
  console.error(
    '@diadraw/wasm: set EMSDK to an emsdk checkout (https://emscripten.org) to build the core.',
  );
  process.exit(1);
}
prepend(join(env.EMSDK, 'upstream/emscripten'));

// Finds a tool on PATH, or, on Windows, where its installers put it (a terminal opened before
// the install does not see the new PATH yet).
const onPath = (name) =>
  (env[pathKey] ?? '')
    .split(delimiter)
    .some((dir) => dir && existsSync(join(dir, windows ? `${name}.exe` : name)));
const find = (name, windowsDirs) => {
  if (onPath(name)) return;
  const dir = windows
    ? windowsDirs.find((d) => d && existsSync(join(d, `${name}.exe`)))
    : undefined;
  if (dir) return prepend(dir);
  console.error(`@diadraw/wasm: ${name} not found; install it and open a new terminal.`);
  process.exit(1);
};
const programFiles = env.ProgramFiles ?? 'C:/Program Files';
const vsCmake = [18, 2022].flatMap((v) =>
  ['Community', 'Professional', 'Enterprise', 'BuildTools'].map((e) =>
    join(
      programFiles,
      `Microsoft Visual Studio/${v}/${e}/Common7/IDE/CommonExtensions/Microsoft/CMake`,
    ),
  ),
);
find('cmake', [join(programFiles, 'CMake/bin'), ...vsCmake.map((d) => join(d, 'CMake/bin'))]);
find('ninja', [
  join(env.LOCALAPPDATA ?? '', 'Microsoft/WinGet/Links'),
  join(
    env.LOCALAPPDATA ?? '',
    'Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe',
  ),
  'C:/ProgramData/chocolatey/bin',
  ...vsCmake.map((d) => join(d, 'Ninja')),
]);

const run = (args) => execFileSync('cmake', args, { cwd: root, env, stdio: 'inherit' });
// Configures afresh when the build folder belongs to another source tree (one made before the
// CMake project moved from cpp/ to the root, or by another checkout).
const cache = join(out, 'CMakeCache.txt');
const source = existsSync(cache)
  ? /^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$/m.exec(readFileSync(cache, 'utf8'))?.[1]
  : undefined;
const fold = (path) => (windows ? resolve(path).toLowerCase() : resolve(path));
const same = (a, b) => fold(a.trim()) === fold(b);
if (!source || !same(source, root) || !existsSync(join(out, 'build.ninja'))) {
  run(['--preset', 'wasm', '-B', out, ...(source ? ['--fresh'] : [])]);
}
run(['--build', out]);

// Into src/core too (git-ignored), so tests and tools can run the sources directly.
mkdirSync(join(here, 'dist/core'), { recursive: true });
for (const file of ['diadraw.mjs', 'diadraw.wasm']) {
  copyFileSync(join(out, file), join(here, 'dist/core', file));
  copyFileSync(join(out, file), join(here, 'src/core', file));
}
copyFileSync(join(here, 'src/core/diadraw.d.mts'), join(here, 'dist/core/diadraw.d.mts'));
