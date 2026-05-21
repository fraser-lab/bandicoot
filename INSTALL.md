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
- Miniconda installed at `/opt/miniconda3` with the Coot scientific
  libraries:
  ```sh
  conda install -c conda-forge \
    clipper-cif clipper-contrib clipper-core clipper-ccp4 \
    clipper-mmdb clipper-minimol clipper-phs clipper-cns \
    mmdb2 ssm
  ```

The prebuilt binary expects Homebrew and Miniconda at those exact
paths because their dependency references are absolute. If you need a
different layout, build from source per [BUILD.md](BUILD.md).

## Install

1. Extract the tarball anywhere:
   ```sh
   tar xf bandicoot-0.0.0.1-darwin-arm64.tar.gz
   ```
   This creates a `bandicoot-0.0.0.1/` directory with `bin/`, `lib/`,
   `libexec/`, and `share/`.

2. (Optional) Add the install's `bin/` to your `PATH`:
   ```sh
   export PATH="$PWD/bandicoot-0.0.0.1/bin:$PATH"
   ```

That's it — no relocation step is needed. Bandicoot's bundled
binaries use `@rpath` / `@executable_path` so they resolve their own
libraries via paths relative to the binary, wherever you put the
tree.

## Launch

```sh
./bandicoot-0.0.0.1/bin/bcoot
```

or, with `PATH` set:

```sh
bcoot
```

The Bandicoot splash should appear for ~2.5 s, then the main window.

## Uninstall

Delete the extracted directory. Bandicoot writes its own runtime files
(`~/0-coot-history.{py,scm}`, `~/coot-backup/`) in the working directory
you launch from — clean those up if you want a fully clean removal.

## Troubleshooting

- **`dyld: Library not loaded: /opt/homebrew/...`** — a required
  Homebrew package isn't installed, or Homebrew is at a non-standard
  prefix. Install the missing package, or rebuild from source pointed
  at your prefix.
- **`dyld: Library not loaded: /opt/miniconda3/...`** — same, for
  Miniconda. Install the missing conda package.
- **`Couldn't find pixmap file: bandicoot-splash.png`** — your extracted
  tree is incomplete; re-extract from the tarball.
