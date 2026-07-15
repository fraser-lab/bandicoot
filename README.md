# Bandicoot

Bandicoot is a macOS-native fork of [Coot](https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/) 0.9.8.95, the structural biology macromolecular model-building program. It
keeps Coot 0.9's full functionality while changing several UI elements in order for the app to function in MacOS Tahoe (26.x) on Apple Silicon.

**NOTE:** Bandicoot targets **macOS Tahoe (26.x) on Apple Silicon**. It is not
built or tested on other macOS releases, Linux, or Windows.

## Why a fork?

The Crystallographic Object-Oriented Tool (Coot) has been the go-to suite of software for molecular modeling, used by thousands of structural biologists all over the world for over twenty years. Recently, Coot has been fully reimagined and redesigned, as well as giving the libraries and packages under the hood a much-needed upgrade. The resulting program (Coot 1) is in wide use today. However, many suites of software for structural biology rely on the old Coot 0.9 framework in order to function, making it necessary for both versions to remain in circulation. 

While Coot 0.9.8.95 is distributed alongside Coot 1 in suites such as CCP4, it has become completely unusable on the most recent MacOS version (2.7x, Tahoe). Bandicoot, a fork of Coot 0.9.8.95, addresses each of these with macOS-specific fixes layered on top of upstream Coot. See the end of this document for a list of major changes from Coot 0.9.8.95.

## Quick start

If you have a prebuilt binary tarball
(`bandicoot-0.0.0.1-darwin-arm64.tar.gz`), see [INSTALL.md](INSTALL.md):
untar it anywhere and launch `<extracted>/bin/bcoot`.

To build from source instead, see [BUILD.md](BUILD.md). You'll need a
handful of Homebrew packages and a Miniconda environment that supplies
Clipper, MMDB2, FFTW2, and a few others.

## What changed vs Coot 0.9.8.95

- **Native macOS menu bar.** Coot's `GtkMenuBar` is walked at startup
  and mirrored into the system menu bar via `[NSApp setMainMenu:]`. The
  in-window menu bar widget is hidden.
- **Native macOS toolbar.** Coot's top `main_toolbar` is mirrored into
  an `NSToolbar` (with icons extracted from each `GtkToolButton`'s
  `GtkImage`) and attached to the main `NSWindow`'s title bar. An
  "Auto-open MTZ" item is added next to "Open Coords...".
- **Floating model toolbar.** The vertical side toolbar
  (`model_toolbar`) is reparented into its own `GtkWindow` (transient
  to the main window), so its widgets render outside the GL backing
  layer's territory. The toolbar's customization popup
  (icons / text / both) still works thanks to a `support.c::lookup_widget`
  patch that consults a `GladeParentKey` data pointer on reparented
  toplevels.
- **Native text rendering for atom labels and axes.** The broken GLUT
  text paths are bypassed; instead, labels are rendered with
  `NSString drawAtPoint` into an `NSBitmapImageRep`, uploaded as a GL
  texture, and drawn as a textured quad in the same matrix transform
  Coot's existing label code uses. Font is Menlo (monospace), size
  driven by Coot's `atom_label_font_size` preference.
- **Retina-correct GL viewport and atom picking.** `glViewport` and
  `gluUnProject` multiply by `[NSScreen backingScaleFactor]`, so the
  GL framebuffer fills the contentView and clicks pick the right atom.
- **Modifier keys via AppKit.** `[NSEvent modifierFlags]` is queried
  directly in `glarea_button_press` (and any caller of
  `bandicoot_shift_pressed` / `bandicoot_control_pressed`) because
  GTK-Quartz on Tahoe never populates `event->state` with modifier
  bits.
- **Click deduplication.** Button-press events arriving within 100 ms
  of the previous one on the same button are dropped, so the
  multi-event burst Tahoe generates for a single physical click
  doesn't fire toggle handlers multiple times.
- **App activation, splash screen, and identification.** On launch the
  app calls `[NSApp activateIgnoringOtherApps:]` so its windows come
  to the foreground; the Bandicoot splash PNG is shown for a
  guaranteed 2.5 s before the main window appears; the title bar
  reads `"Bandicoot (Coot 0.9.8.95, built <date>)"`.

The executable on disk is called `bcoot` (a symlink to the existing
`coot` wrapper script).

## License

Bandicoot inherits the **GNU General Public License v3** from upstream
Coot. The full text of the license is in [COPYING](COPYING).

The Bandicoot-specific patches are licensed under the same GPL v3
terms.

The binary tarball also bundles a number of third-party libraries,
tools, and fonts distributed under their own (GPL-compatible) licenses.
These are enumerated in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Credits

Bandicoot builds on the work of Paul Emsley, Bernhard Lohkamp, Kevin
Cowtan, and the many other contributors to Coot. The macOS-native
patches were developed by Art Lyubimov for use within the Fraser Lab at
UCSF.

For the upstream Coot README, see [README.coot.md](README.coot.md).
