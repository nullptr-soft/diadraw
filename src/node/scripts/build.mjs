// Builds the native addon with cmake-js: configures the repository root with DIADRAW_BUILD_NODE=ON
// (and without tests, the CLI or the apps), builds only the addon, and copies it to
// dist/diadraw.node (and src/diadraw.node, for the tests). Needs CMake 3.25+ and a C++20 compiler (MSVC on Windows); Ninja when present.
//
// The build goes outside the repository, like the CMake presets: diadraw-build/node next to the
// checkout, or $DIADRAW_BUILD_DIR/node. `--debug` builds the Debug configuration, and `--CDNAME=value`
// arguments go on to CMake as `-DNAME=value` (for example FETCHCONTENT_SOURCE_DIR_MINIZ=<dir>).

import { execFileSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = fileURLToPath(new URL('..', import.meta.url));
const root = resolve(here, '../..');
const out = process.env.DIADRAW_BUILD_DIR
  ? resolve(process.env.DIADRAW_BUILD_DIR, 'node')
  : resolve(root, '../diadraw-build/node');
const config = process.argv.includes('--debug') ? 'Debug' : 'Release';

const require = createRequire(import.meta.url);
const cmakeJs = join(dirname(require.resolve('cmake-js/package.json')), 'bin/cmake-js');
// The folder holding napi.h (node-addon-api's own `include` is quoted for node-gyp).
const addonApi = dirname(require.resolve('node-addon-api/package.json'));

const options = {
  DIADRAW_BUILD_NODE: 'ON',
  DIADRAW_BUILD_TESTS: 'OFF',
  DIADRAW_BUILD_CLI: 'OFF',
  DIADRAW_BUILD_GTK: 'OFF',
  DIADRAW_BUILD_MACOS: 'OFF',
  DIADRAW_BUILD_WINUI: 'OFF',
  NODE_ADDON_API_DIR: addonApi,
};

const cmakeJsArgs = [
  '--directory',
  root,
  '--out',
  out,
  '--config',
  config,
  ...Object.entries(options).map(([k, v]) => `--CD${k}=${v}`),
  ...process.argv.slice(2).filter((a) => a.startsWith('--CD')),
];
const run = (command, extra = []) =>
  execFileSync(process.execPath, [cmakeJs, command, ...cmakeJsArgs, ...extra], {
    stdio: 'inherit',
  });
// Configure every time (CMake only redoes what changed), so `--debug` and `--CD` options reach an
// existing build folder; then build only the addon.
run('configure');
run('build', ['--target', 'diadraw_node']);

// src/node/CMakeLists.txt puts it in `out/addon` with every generator.
const built = join(out, 'addon', 'diadraw.node');
if (!existsSync(built)) {
  console.error(`@diadraw/node: the build finished but ${built} is missing`);
  process.exit(1);
}
// Into src/ too (git-ignored), so the tests run the sources directly.
mkdirSync(join(here, 'dist'), { recursive: true });
copyFileSync(built, join(here, 'dist/diadraw.node'));
copyFileSync(built, join(here, 'src/diadraw.node'));
console.log(`@diadraw/node: ${join(here, 'dist/diadraw.node')}`);
