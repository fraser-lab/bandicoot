# Installing a Bandicoot binary tarball

These instructions cover the prebuilt
`bandicoot-<version>-darwin-arm64.tar.gz` distribution. If you're building
from source instead, see [BUILD.md](BUILD.md).

## Requirements

- macOS Tahoe (26.x), Apple Silicon
- Homebrew installed at `/opt/homebrew` with:
  ```sh
  brew install gtk+ gtkglext freeglut gsl cairo libpng sqlite bzip2 boost
  ```
**NOTE:** Homebrew _has to be_ installed in `/opt/homebrew` or the binary distribution of Bandicoot won't work. If you wish to have Homebrew in a different location, you'll have to build Bandicoot from sources as described in [BUILD.md](BUILD.md)

## Install

1. Extract the tarball anywhere:
   ```sh
   tar xf bandicoot-<version>-darwin-arm64.tar.gz
   ```
   This creates a `bandicoot-<version>/` directory with `bin/`, `lib/`,
   `libexec/`, `share/`, and a `setup.sh` helper.

    **NOTE:** In this case, `<version>` is a placeholder for the Bandicoot version number. E.g. for Bandicoot v.0.0.0.3 the tarball will have the filename `bandicoot-0.0.0.3-darwin-arm64.tar.gz`

2. Run the one-time setup script:
   ```sh
   cd bandicoot-<version> --add-to-path
   ./setup.sh
   ```

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


## Notes

- Starting in v0.1.0.0, Bandicoot embeds Python so it can
talk to Phenix (live model/map updates during refinement). As of
v0.1.1.3 `libpython3.13.dylib` is bundled inside the tarball, so
Miniconda is no longer required at runtime. The Coot scientific
libraries (clipper, mmdb2, ssm, ccp4c, fftw2, libc++) and the
graphics libraries (libpng16, libfreetype6) are bundled too.
