# @diadraw/desktop

The DiaDraw desktop app: the web app (`src/apps/web`, on the C++ core) inside [Tauri 2](https://v2.tauri.app), with
native open and save dialogs, a file watcher (edits from another editor refresh the diagram when
there are no unsaved changes) and `.draw` file associations.

## Status

Scaffolded, **native build not run yet**: Tauri 2 needs Rust 1.77.2 or newer, and this machine
has Rust 1.68 (installed through asdf). Everything else is ready:

- `src-tauri/tauri.conf.json`: window, CSP, bundle settings, file association, icons
- `src-tauri/capabilities/default.json`: dialogs; read, write and watch files under home,
  Documents and Desktop
- `src-tauri/src`: the Rust entry point (plugins only; app logic lives in the web UI)
- `src-tauri/icons`: generated from `icon.svg` with `npm run tauri -- icon app-icon.png` (in `src/apps/desktop`)

## Running it

1. Update Rust (for example `asdf install rust latest && asdf global rust latest`, or `rustup
update`) and install the [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/).
2. Run the app against the web dev server:

   ```bash
   npm run dev:native -w @diadraw/desktop
   ```

3. Build installers:

   ```bash
   npm run build:native -w @diadraw/desktop
   ```

The native scripts are deliberately not called `dev`/`build`, so Turborepo and CI (which have
no Rust toolchain) skip them.
