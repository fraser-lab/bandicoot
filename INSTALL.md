# Installing a Bandicoot binary tarball

These instructions cover the prebuilt
`bandicoot-0.0.0.1-darwin-arm64.tar.gz` distribution. If you're building
from source instead, see [BUILD.md](BUILD.md).

## Requirements

- macOS Tahoe (26.x), Apple Silicon
- Homebrew installed at `/opt/homebrew` with:
  ```sh
  brew install gtk+ gtkglext freeglut gsl cairo libpng sqlite bzip2 boost
  ```

That's the entire runtime requirement. The Coot scientific libraries
(clipper, mmdb2, ssm, ccp4c, fftw2, libc++) are bundled inside the
tarball — you do **not** need to install them via conda. Miniconda is
only required if you intend to rebuild Bandicoot from source against
the same clipper / mmdb / ssm versions.

The prebuilt binary expects Homebrew at `/opt/homebrew` because
homebrew dependency references are absolute. If you need a different
layout, build from source per [BUILD.md](BUILD.md).

## Install

1. Extract the tarball anywhere:
   ```sh
   tar xf bandicoot-0.0.0.1-darwin-arm64.tar.gz
   ```
   This creates a `bandicoot-0.0.0.1/` directory with `bin/`, `lib/`,
   `libexec/`, `share/`, and a `setup.sh` helper.

2. Run the one-time setup script:
   ```sh
   cd bandicoot-0.0.0.1
   ./setup.sh
   ```
   This strips macOS quarantine flags, ad-hoc-signs the binaries so
   Gatekeeper stays quiet on relaunches, registers Bandicoot with
   Spotlight / Launchpad, and verifies the Homebrew / Miniconda
   prerequisites above are installed. It is idempotent and never uses
   `sudo`. Pass `--add-to-path` to also add `bandicoot-0.0.0.1/bin/` to
   your PATH (writes a tagged `export PATH=...` line to `~/.zshrc` or
   `~/.bash_profile` depending on your shell). The tag makes re-runs
   idempotent and lets you find and remove the line later.

3. (Optional) Add the install's `bin/` to your `PATH` manually if you
   skipped `--add-to-path`:
   ```sh
   export PATH="$PWD/bin:$PATH"
   ```

Data files (pixmaps, monomer dictionary, reference structures, GTK
theme) are located at runtime via `COOT_DATA_DIR` (set by the `bcoot`
wrapper) — so the install can be moved to a different directory later
and will keep working.

The bundled binaries already use `@rpath` / `@executable_path` so they
resolve their own libraries via paths relative to the binary, wherever
you put the tree.

## Launch

```sh
./bandicoot-0.0.0.1/bin/bcoot
```

or, with `PATH` set:

```sh
bcoot
```

## Uninstall

Delete the extracted directory. Bandicoot writes its own runtime files
(`~/0-coot-history.{py,scm}`, `~/coot-backup/`) in the working directory
you launch from — clean those up if you want a fully clean removal.

## Troubleshooting

- **`dyld: Library not loaded: /opt/homebrew/...`** — a required
  Homebrew package isn't installed, or Homebrew is at a non-standard
  prefix. Install the missing package, or rebuild from source pointed
  at your prefix.
- **`dyld: Library not loaded: @rpath/libclipper-...`** — the bundled
  copy of that library is missing from your extracted tree. Re-extract
  the tarball; do not move individual files out of `lib/`.
- **A broken-image graphic appears on the splash, or "couldn't find
  pixmap file: bandicoot-splash.png"** — the bcoot wrapper failed to
  export `COOT_DATA_DIR` correctly. Make sure you launch through
  `bin/bcoot` (not the bare `libexec/coot-bin`) and confirm the
  extracted tree contains `share/coot/pixmaps/`.
- **Spotlight / Launchpad can't find Bandicoot** — rerun `./setup.sh`;
  the `.desktop` file is installed into `~/.local/share/applications/`.
