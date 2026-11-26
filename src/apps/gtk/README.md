# DiaDraw for Linux (gtkmm 4)

A gtkmm 4 shell over the C++ core; `src/README.md` describes what the native shells share.

## Build

Needs gtkmm 4.10 or later (`libgtkmm-4.0-dev` on Debian and Ubuntu, `gtkmm4.0-devel` on
Fedora) and libcurl for File › Cloud Storage (`libcurl4-openssl-dev`, `libcurl-devel`), CMake
3.25+ and Ninja. From the repository root:

```bash
cmake --preset release && cmake --build --preset release   # diadraw-build/release/apps/gtk/diadraw-gtk
```

## Packages

- **Tarball, DEB, RPM:** `cpack --preset release` after the build. DEB and RPM are made when
  `dpkg-deb` or `rpmbuild` is installed. They install `diadraw-gtk`, the `diadraw` CLI, a desktop
  entry, the `*.draw` file type, AppStream data, icons, and draw.io's stencil sets (with their
  NOTICE) in `share/diadraw/drawio`.
- **Flatpak:** `flatpak/com.diadraw.DiaDraw.yml`, on the GNOME 47 runtime. It builds the gtkmm
  stack too, since the runtime has GTK but not its C++ bindings:

  ```bash
  flatpak install flathub org.gnome.Platform//47 org.gnome.Sdk//47
  flatpak-builder --user --install --force-clean build-flatpak src/apps/gtk/flatpak/com.diadraw.DiaDraw.yml
  flatpak run com.diadraw.DiaDraw
  ```

All of these have been built (in containers, arm64) and tested: the unit and gesture tests pass
on Ubuntu 24.04 (GCC 13) and Fedora 41 (GCC 14); the DEB, RPM and Flatpak bundle install into
clean systems and pull their dependencies, and the CLI and the app run from them with the
stencil sets. A Flatpak bundle to hand around:

```bash
flatpak-builder --repo=repo --force-clean build-flatpak src/apps/gtk/flatpak/com.diadraw.DiaDraw.yml
flatpak build-bundle repo DiaDraw.flatpak com.diadraw.DiaDraw
flatpak install DiaDraw.flatpak
```
