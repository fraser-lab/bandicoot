# Bandicoot

Bandicoot is a macOS-native fork of [Coot](https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/)
0.9.8.95, the structural biology macromolecular model-building program. It
keeps Coot 0.9's full functionality while replacing the in-window GTK menu
bar and toolbar with native macOS chrome, so the OpenGL drawing area can
own the entire window content view without fighting GTK widgets for
pixels.

Bandicoot targets **macOS Tahoe (26.x) on Apple Silicon**. It is not
built or tested on other macOS releases, Linux, or Windows.

## Why a fork?

Coot 0.9.8.95 builds and runs on macOS Tahoe, but several long-standing
GTK-Quartz interactions render the user interface unusable:

- The `_NSOpenGLViewBackingLayer` that GtkGLExt-Quartz attaches to the
  main `NSWindow`'s `contentView` covers any in-window GTK widget (menu
  bar, toolbars, status bar) with opaque GL pixels.
- `event->state` from GTK-Quartz arrives with the button mask set but
  no modifier flags (shift, control), so shift-click for atom labels
  never fires.
- A single physical click on Tahoe produces 2-3 GTK button-press
  events, which toggles any add/remove handler (such as the atom-label
  list) on and off.
- The freeglut on Homebrew silently fails to render text via either
  `glutStrokeCharacter` or `glutBitmapCharacter`, so atom labels and
  coordinate-axis markers don't appear.
- Retina backing-scale handling is missing in several places, causing
  the GL viewport and atom picking to misbehave.

Bandicoot addresses each of these with macOS-specific fixes layered on
top of upstream Coot. The patches are isolated to a handful of
`#ifdef __APPLE__` sites plus a new `bandicoot_appkit.{h,mm}` shim that
wraps AppKit `NSMenu`, `NSToolbar`, and text-rendering APIs.

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

## Quick start

If you have a prebuilt binary tarball
(`bandicoot-0.0.0.1-darwin-arm64.tar.gz`), see [INSTALL.md](INSTALL.md):
untar it anywhere and launch `<extracted>/bin/bcoot`. The shipped
binaries use `@rpath`/`@executable_path` so no per-install relocation
is needed.

To build from source instead, see [BUILD.md](BUILD.md). You'll need a
handful of Homebrew packages and a Miniconda environment that supplies
Clipper, MMDB2, FFTW2, and a few others.

## License

Bandicoot inherits the **GNU General Public License v3** from upstream
Coot. The full text of the license is in [COPYING](COPYING).

The Bandicoot-specific patches are licensed under the same GPL v3
terms.

## Known limitations (v0.0.0.1)

- The bottom corners of both windows do not accept resize on Tahoe.
  Top/sides work. This is a GTK-Quartz interaction not introduced by
  Bandicoot.
- Pre-existing Coot 0.9.8.95 bugs (such as Regularize Zone selecting
  the full chain instead of the picked zone) carry through unchanged.

## Credits

Bandicoot builds on the work of Paul Emsley, Bernhard Lohkamp, Kevin
Cowtan, and the many other contributors to Coot. The macOS-native
patches were developed for use within the Fraser Lab at UCSF.

For the upstream Coot README, see [README.coot.md](README.coot.md).
