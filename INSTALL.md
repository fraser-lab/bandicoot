# Installing a Bandicoot binary tarball

These instructions cover the prebuilt distributions:

- macOS: `bandicoot-<version>-darwin-arm64.tar.gz`
- Linux: `bandicoot-<version>-linux-x86_64.tar.gz`

If you're building from source instead, see [BUILD.md](BUILD.md).

## Requirements

- macOS Tahoe (26.x), Apple Silicon, or
- Linux, x86_64: Ubuntu 22.04 or newer, or another distribution of similar age
  or newer, with a graphical desktop (X11, or Wayland with XWayland) and OpenGL.
  Tested on Ubuntu 22.04, 24.04 and 26.04. (Bandicoot also runs on Ubuntu 20.04,
  but has to be built from sources.)

## Install

1. Extract the tarball anywhere:

	a. on MacOS:
   ```sh
   tar -xvf bandicoot-<version>-darwin-arm64.tar.gz
   ```
	b. on Linux:
   ```sh
   tar -xvf bandicoot-<version>-linux-x86_64.tar.gz
   ```
   This creates a `bandicoot-<version>/` directory (e.g. `bandicoot_0.3.0.2`) with `bin/`, `lib/`, `libexec/`, `share/`, and a `setup.sh` helper.


3. Run the one-time setup script:
   ```sh
   cd bandicoot-<version>
   ./setup.sh --add-to-path
   ```

  **NOTE:** On Linux, `setup.sh` also adds Bandicoot to the desktop's application menu; on both operating systems, `--add-to-path` adds the path to Bandicoot executable to the RC file (`.bashrc`, `.zshrc` or the like).

  **NOTE:** If you don't want install to add the Bandicoot path to your `PATH`, omit the `--add-to-path` option. You can add the path manually, e.g. for `bash` or `zsh`:
   ```sh
   export PATH="$PWD/bin:$PATH"
   ```

## Launch

	`./bandicoot-<version>/bin/bcoot`

or, with `PATH` set:

	`bcoot`

## Uninstall

Delete the extracted directory. Bandicoot writes its own runtime files
(`~/0-coot-history.{py,scm}`, `~/coot-backup/`) in the working directory
you launch from — clean those up if you want a fully clean removal.

`setup.sh` also wrote `~/.local/share/applications/bandicoot.desktop` and, with
`--add-to-path`, a line ending in `# bandicoot:path-entry` in your shell
startup file; remove those too.

## Troubleshooting

### MacOS

- **Icons missing on most toolbar buttons** - Bandicoot hasn't been properly set up, usually because `setup.sh` was not run after unpacking the tarball. See [Installation](#Install) for how to run the setup script.
- **`dyld: Library not loaded: @rpath/libclipper-...`** — the bundled
  copy of that library is missing from your extracted tree. Re-extract
  the tarball; do not move individual files out of `lib/`.
- **Spotlight / Launchpad can't find Bandicoot** — rerun `./setup.sh`;
  the `.desktop` file is installed into `~/.local/share/applications/`.


### Linux

- **`version 'GLIBC_2.xx' not found`** — the system is older than the one the
  tarball was built on. Build from source instead (see [BUILD.md](BUILD.md)).
- **The 3D view freezes and the terminal repeats `amdgpu: The CS has been
  cancelled because the context is lost`** (often with the screen blanking for
  a moment) — the graphics driver reset the GPU. To confirm it is the driver,
  run `LIBGL_ALWAYS_SOFTWARE=1 bcoot`: slow, but not affected. Updating the
  graphics driver (Mesa) is the remedy. (**NOTE**: So far this effect has only been seen when using `Ligand -> FLEV This Residue`.)

