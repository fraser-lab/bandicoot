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
  conda install --override-channels -c bioconda -c conda-forge \
    clipper libccp4 mmdb2 ssm
  ```
  `bioconda/clipper` 2.1.20180802 is the CCP4 crystallography library;
  it ships all the `libclipper-*.2.dylib` files Bandicoot links against
  in a single package. (It is unrelated to the conda-forge "clipper"
  CLIP-seq Python tool.) `--override-channels` bypasses the Anaconda
  default channels (`pkgs/main`, `pkgs/r`), which now require Terms-of-
  Service acceptance and will otherwise abort a non-interactive install.

  If you don't already have Miniconda at that exact path:
  ```sh
  curl -fsSL -o /tmp/miniconda.sh \
    https://repo.anaconda.com/miniconda/Miniconda3-latest-MacOSX-arm64.sh
  sudo mkdir -p /opt/miniconda3 && sudo chown "$USER":staff /opt/miniconda3
  bash /tmp/miniconda.sh -b -u -p /opt/miniconda3
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
   `libexec/`, `share/`, and a `setup.sh` helper.

2. Run the one-time setup script:
   ```sh
   cd bandicoot-0.0.0.1
   ./setup.sh --add-to-path
   ```
   This script handles a few things that would otherwise make the install a lot more annoying than it is; the `--add-to-path` option adds `bandicoot-0.0.0.1/bin/` to your PATH (writes a tagged `export PATH=...` line to `~/.zshrc` or `~/.bash_profile` depending on your shell). If you don't want that to happen automatically, simply don't use the `--add-to-path` option.

The install can be moved to a different folder and still work, though you would need to update the path in your `~/.zshrc` file.

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
- **`dyld: Library not loaded: /opt/miniconda3/lib/libclipper-...`** —
  install `bioconda/clipper`. For `libccp4c` → `bioconda/libccp4`; for
  `libmmdb2` → `bioconda/mmdb2`; for `libssm` → `bioconda/ssm`. All
  four come from the requirements command above.
- **A broken-image graphic appears on the splash, or "couldn't find
  pixmap file: bandicoot-splash.png"** — the bcoot wrapper failed to
  export `COOT_DATA_DIR` correctly. Make sure you launch through
  `bin/bcoot` (not the bare `libexec/coot-bin`) and confirm the
  extracted tree contains `share/coot/pixmaps/`.
- **Spotlight / Launchpad can't find Bandicoot** — rerun `./setup.sh`;
  the `.desktop` file is installed into `~/.local/share/applications/`.
