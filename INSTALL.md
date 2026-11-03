# Installing a Bandicoot binary tarball

These instructions cover the prebuilt distributions:

- macOS: `bandicoot-<version>-darwin-arm64.tar.gz`
- Linux: `bandicoot-<version>-linux-x86_64.tar.gz`

If you're building from source instead, see [BUILD.md](BUILD.md).

## Requirements

- macOS Tahoe (26.x), Apple Silicon, or
- Linux, x86_64: Ubuntu 20.04 or newer, or another distribution of similar age
  or newer, with a graphical desktop (X11, or Wayland with XWayland) and OpenGL.
  Tested on Ubuntu 22.04.

That's it. As of v0.1.4.10 the binary distribution is fully self-contained:
the GTK2-Quartz stack and every other third-party library are bundled inside
the tarball, so **no Homebrew, no XQuartz, and no conda are required** at
runtime. Extract it anywhere and run.

The Linux tarball is self-contained in the same way, including GTK 2, Python
and the crystallographic libraries. It uses only a few libraries from the
system: the C library, OpenGL and the graphics driver, X11, and the font
libraries. If the system's C++ runtime is older than the one Bandicoot was
built with, as on Ubuntu 20.04, the launcher uses a copy bundled for that case.

## Install

1. Extract the tarball anywhere:
   ```sh
   tar -xvf bandicoot-<version>-darwin-arm64.tar.gz
   ```
   or, on Linux:
   ```sh
   tar -xvf bandicoot-<version>-linux-x86_64.tar.gz
   ```
   This creates a `bandicoot-<version>/` directory with `bin/`, `lib/`,
   `libexec/`, `share/`, and a `setup.sh` helper.

    **NOTE:** In this case, `<version>` is a placeholder for the Bandicoot version number. E.g. for Bandicoot v0.1.4.10 the tarball will have the filename `bandicoot-0.1.4.10-darwin-arm64.tar.gz`

2. Run the one-time setup script:
   ```sh
   cd bandicoot-<version>
   ./setup.sh --add-to-path
   ```

  **NOTE:** On Linux, `setup.sh` also adds Bandicoot to the desktop's application menu, and `--add-to-path` writes to `~/.bashrc` (or `~/.zshrc`).

  **NOTE:** If you don't want install to add the Bandicoot path to your `PATH`, omit the `--add-to-path` option. You can add the path manually:
   ```sh
   export PATH="$PWD/bin:$PATH"
   ```

## Launch

```sh
./bandicoot-<version>/bin/bcoot
```

or, with `PATH` set:

```sh
bcoot
```

## Uninstall

Delete the extracted directory. Bandicoot writes its own runtime files
(`~/0-coot-history.{py,scm}`, `~/coot-backup/`) in the working directory
you launch from — clean those up if you want a fully clean removal.

`setup.sh` also wrote `~/.local/share/applications/bandicoot.desktop` and, with
`--add-to-path`, a line ending in `# bandicoot:path-entry` in your shell
startup file; remove those too.

## Troubleshooting

- **`dyld: Library not loaded: /opt/homebrew/...`** — should not happen with
  v0.1.4.10 or later, which bundles every Homebrew library. If you see it,
  the tarball predates self-containment (upgrade), or a file was moved out of
  `lib/` — re-extract the tarball cleanly.
- **`dyld: Library not loaded: @rpath/libclipper-...`** — the bundled
  copy of that library is missing from your extracted tree. Re-extract
  the tarball; do not move individual files out of `lib/`.
- **A broken-image graphic appears on the splash, or "couldn't find
  pixmap file: bandicoot-splash.png"** — the bcoot wrapper failed to
  export `COOT_DATA_DIR` correctly. Make sure you launch through
  `bin/bcoot` (not the bare `libexec/Bandicoot`) and confirm the
  extracted tree contains `share/coot/pixmaps/`.
- **Spotlight / Launchpad can't find Bandicoot** — rerun `./setup.sh`;
  the `.desktop` file is installed into `~/.local/share/applications/`.


### Linux

- **`version 'GLIBC_2.xx' not found`** — the system is older than the one the
  tarball was built on. Build from source instead (see [BUILD.md](BUILD.md)).
- **The 3D view freezes and the terminal repeats `amdgpu: The CS has been
  cancelled because the context is lost`** (often with the screen blanking for
  a moment) — the graphics driver reset the GPU. To confirm it is the driver,
  run `LIBGL_ALWAYS_SOFTWARE=1 bcoot`: slow, but not affected. Updating the
  graphics driver (Mesa) is the remedy.
- **Buttons and menus look plain** — expected: Bandicoot uses its own bundled
  GTK 2 theme rather than the desktop's.

## Notes

- Starting in v0.1.0.0, Bandicoot embeds Python so it can
talk to Phenix (live model/map updates during refinement). As of
v0.1.1.3 `libpython3.13.dylib` is bundled inside the tarball, and as
of v0.1.4.2 the Python **standard library** is bundled too (under
`lib/python3.13/`), so Miniconda is no longer required at runtime.
(Before v0.1.4.2 the embedded interpreter still borrowed its standard
library from an external conda via `PYTHONHOME`; on a machine without a
matching conda it aborted at startup with `ModuleNotFoundError: No
module named 'encodings'`.) The Coot scientific
libraries (clipper, mmdb2, ssm, ccp4c, fftw2, libc++) and the
graphics libraries (libpng16, libfreetype6) are bundled too.
- v0.1.4.2 also bundles **numpy** and **matplotlib** (self-contained
PyPI wheels, under `lib/python3.13/site-packages/`): numpy is required
by the PanDDA event-map tiling fix, matplotlib by the pathology plots.
This is the bulk of the tarball's size; a future lean tarball may fetch
them on demand at `setup.sh` time instead.
