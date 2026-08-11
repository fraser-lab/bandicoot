# Third-Party Licenses

Bandicoot is a macOS-native fork of [Coot](https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/)
0.9.8.95. Coot's own source code and the Bandicoot-specific patches are
licensed under the **GNU General Public License v3** — the full text is in
[COPYING](COPYING).

The prebuilt binary tarball (`bandicoot-<version>-darwin-arm64.tar.gz`) is an
**aggregate**: alongside the GPL-3 Coot/Bandicoot code it bundles a number of
third-party libraries, executables, fonts, and Python packages that are
distributed under their own licenses. All of them are GPL-3-compatible, but
their license notices must accompany binary redistribution. This file
enumerates those components and their licenses.

As of Bandicoot 0.1.4.10 the GTK2-Quartz stack (GTK, GLib, GObject, GIO, Pango,
Cairo, GDK, gdk-pixbuf, ATK, GtkGLExt) and its dependencies are **bundled** in
the tarball and rewritten to `@rpath`, so Homebrew is no longer required at
runtime. They are enumerated in the tables below. (freeglut is no longer linked
at all — its calls have native replacements — so neither it nor Mesa libGL ship.)

> **Note on completeness.** License classifications below are grouped by
> component family. Where a component ships its own license file inside the
> tarball, that file is the authoritative text; the summaries here are for
> convenience. Versions correspond to Bandicoot 0.1.4.7 and may change between
> releases.

---

## 1. Coot and Bandicoot (GPL-3)

The following are the GPL-3 parts of the distribution:

- `libcoot-*` shared libraries
- `coot`, `bcoot`, `bandicoot.inspect`, and the `coot-*` helper executables
- `lidia`, `findligand`, `findwaters`, `mmrrcc`, `dynarama`
- Coot Scheme (`share/coot/scheme/`) and Python (`site-packages/coot/`,
  `site-packages/lidia/`) scripts

License: **GNU GPL v3** — see [COPYING](COPYING).

---

## 2. Bundled C/C++ libraries

### 2.1 GPL (GPL-2 or later)

| Component | Files | License |
|---|---|---|
| FFTW 2 | `libfftw.2.dylib`, `librfftw.2.dylib` | GNU GPL v2 or later |
| GNU Scientific Library | `libgsl.28.dylib`, `libgslcblas.0.dylib` | GNU GPL v3 |
| GNU Readline | `libreadline.8.dylib` | GNU GPL v3 |

### 2.2 LGPL / weak-copyleft

| Component | Files | License |
|---|---|---|
| Clipper (Kevin Cowtan) | `libclipper-*.dylib` | LGPL / Clipper license |
| MMDB2 (CCP4 / E. Krissinel) | `libmmdb2.0.dylib` | LGPL v3 |
| CCP4 libraries | `libccp4c.0.dylib`, `libccp4mg-utils*.dylib` | LGPL |
| SSM (E. Krissinel) | `libssm.2.dylib` | LGPL |
| libart_lgpl | `libart_lgpl_2.2.dylib` | LGPL |
| libgnomecanvas | `libgnomecanvas-2.0.dylib` | LGPL v2+ |
| GooCanvas | `libgoocanvas.3.dylib` | LGPL v2 |
| librsvg | `librsvg-2.2.dylib` | LGPL v2+ |
| gdk-pixbuf loaders | `lib/gdk-pixbuf-2.0/.../libpixbufloader-*` | LGPL v2+ |
| GNU libiconv | `libiconv.2.dylib` | LGPL v2+ |
| GNU gettext runtime | `libintl.8.dylib` | LGPL v2+ |
| GTK+ 2 (GTK-Quartz) | `libgtk-quartz-2.0.0.dylib`, `libgdk-quartz-2.0.0.dylib`, `libgailutil.18.dylib` | LGPL v2+ |
| GtkGLExt | `libgtkglext-quartz-1.0.0.dylib`, `libgdkglext-quartz-1.0.0.dylib` | LGPL v2+ |
| GLib (GLib/GObject/GIO/GModule) | `libglib-2.0.0.dylib`, `libgobject-2.0.0.dylib`, `libgio-2.0.0.dylib`, `libgmodule-2.0.0.dylib` | LGPL v2+ |
| Pango | `libpango-1.0.0.dylib`, `libpangocairo-1.0.0.dylib`, `libpangoft2-1.0.0.dylib` | LGPL v2+ |
| Cairo | `libcairo.2.dylib`, `libcairo-gobject.2.dylib` | LGPL v2.1 / MPL 1.1 (dual) |
| ATK | `libatk-1.0.0.dylib` | LGPL v2+ |
| gdk-pixbuf | `libgdk_pixbuf-2.0.0.dylib` | LGPL v2+ |
| GNU FriBidi | `libfribidi.0.dylib` | LGPL v2.1+ |
| libthai | `libthai.0.dylib` | LGPL v2.1+ |
| libdatrie | `libdatrie.1.dylib` | LGPL v2.1+ |
| Graphite2 | `libgraphite2.3.dylib` | LGPL v2.1 / MPL / GPL (tri-license) |

### 2.3 Permissive (BSD / MIT / zlib / PSF / etc.)

| Component | Files | License |
|---|---|---|
| LLVM libc++ | `libc++.1.dylib` | Apache-2.0 with LLVM exception |
| OpenSSL | `libssl.3.dylib`, `libcrypto.3.dylib` | Apache-2.0 |
| libffi | `libffi.8.dylib` | MIT |
| SQLite | `libsqlite3.dylib` | Public domain |
| CPython runtime | `libpython3.13.dylib`, `lib/python3.13/*.so` | Python Software Foundation License (`lib/python3.13/LICENSE.txt`) |
| HarfBuzz | `libharfbuzz.0.dylib` | MIT ("Old MIT") |
| Little CMS 2 | `liblcms2.2.dylib` | MIT |
| FreeType | `libfreetype.6.dylib` | FreeType License (BSD-style; dual with GPLv2) |
| libpng | `libpng16.16.dylib` | libpng / zlib license |
| libjpeg (IJG) | `libjpeg.62.4.0.dylib` | IJG (BSD-like) |
| libtiff | `libtiff.6.dylib` | libtiff (BSD-like) |
| libwebp / sharpyuv | `libwebp*.dylib`, `libsharpyuv.0.dylib` | BSD-3 (Google) |
| libavif | `libavif.16.4.2.dylib` | BSD-2 |
| OpenJPEG | `libopenjp2.2.5.4.dylib` | BSD-2 |
| Brotli | `libbrotli*.dylib` | MIT |
| libxcb | `libxcb.1.1.0.dylib` | MIT (X.org) |
| libXau | `libXau.6.dylib` | MIT (X.org) |
| X11 client libs (via Cairo) | `libX11.6.dylib`, `libX11-xcb.1.dylib`, `libXext.6.dylib`, `libXrender.1.dylib`, `libXdmcp.6.dylib` | MIT (X.org) |
| fontconfig | `libfontconfig.1.dylib` | MIT-style (fontconfig license) |
| pixman | `libpixman-1.0.dylib` | MIT |
| PCRE2 | `libpcre2-8.0.dylib` | BSD-3-Clause |
| zlib-ng | `libz.1.3.1.zlib-ng.dylib` | zlib license |
| xz / liblzma | `liblzma.5.dylib` | 0BSD / public domain |
| Expat (libexpat) | `libexpat.1.dylib` | MIT |
| mpdecimal | `libmpdec.4.dylib` | BSD-2-Clause |
| bzip2 | `libbz2.dylib` | BSD-like (bzip2 license) |
| ncurses | `libncursesw.6.dylib`, `libtinfow.6.dylib`, `libpanelw.6.dylib` | MIT (X11-style) |

---

## 3. Bundled executables

| Program | Version | Copyright | License |
|---|---|---|---|
| `reduce` (`bin/reduce`) | 4.14 (2023) | 1997–2016 J. Michael Word; 2020–2023 Richardson Lab, Duke University | Apache-2.0 |
| `probe` (`bin/probe`) | 2.26 (2023) | 1996–2016 J. Michael Word; 2021–2023 Richardson Lab, Duke University | Apache-2.0 |

Upstream: <https://github.com/rlabduke/reduce>, <https://github.com/rlabduke/probe>

---

## 4. Bundled Python packages

Located under `lib/python3.13/site-packages/`. Where a package ships a license
file (`*.dist-info/LICENSE*`), that file is authoritative.

| Package | Version | License |
|---|---|---|
| NumPy | 2.5.1 | BSD-3-Clause |
| matplotlib | 3.11.0 | matplotlib license (PSF-based, BSD-compatible) |
| Pillow (PIL) | 12.3.0 | MIT-CMU / HPND |
| contourpy | 1.3.3 | BSD-3-Clause |
| kiwisolver | 1.5.0 | BSD-3-Clause |
| cycler | 0.12.1 | BSD (matplotlib project) |
| fontTools | 4.63.0 | MIT |
| pyparsing | 3.3.2 | MIT |
| packaging | 26.2 | Apache-2.0 / BSD-2 (dual) |
| python-dateutil | 2.9.0.post0 | Apache-2.0 / BSD-3 (dual) |
| six | 1.17.0 | MIT |

> Housekeeping: the tarball currently ships duplicate dist-info directories for
> two packages (`numpy-2.4.6` alongside `numpy-2.5.1`, and `pillow-12.2.0`
> alongside `pillow-12.3.0`). The active/installed versions are numpy 2.5.1 and
> Pillow 12.3.0; the stale dist-info dirs should be pruned from the build.

---

## 4a. Bundled crystallographic data

Checked into the repository under `data/coot-data/` (see its `README.md` for
provenance and the exact monomer list) and installed to `share/coot/`. These files
have been redistributed in every Bandicoot tarball since v0.1.0.0; as of v0.1.4.13
they live in the source tree as well, so a clean checkout builds without staging
data from elsewhere.

| Data | Version | Source | Notes |
|---|---|---|---|
| CCP4 / REFMAC monomer library (curated 115-file subset) | `mon_lib` 5.60, update 16/02/22 | CCP4 | Installed to `share/coot/lib/data/monomers`. The full library is ~35,600 files / 677 MB; only the common-residue subset is shipped. |
| Coot reference structures | Coot 0.9.8.95 | Coot | Installed to `share/coot/reference-structures`. Also shipped by CCP4 9.x under `coot_py3/share/coot/`. |

---

## 5. Bundled fonts

| Font | Location | License |
|---|---|---|
| Bitstream Vera | `share/coot/fonts/` (`COPYRIGHT.TXT`) | Bitstream Vera License (permissive) |
| DejaVu | `matplotlib/mpl-data/fonts/ttf/` (`LICENSE_DEJAVU`) | Bitstream Vera / public-domain additions |
| STIX | `matplotlib/mpl-data/fonts/ttf/` (`LICENSE_STIX`) | STIX Font License (OFL-like) |

---

## 6. Compliance notes

- All bundled components are **GPL-3-compatible**, so the aggregate tarball is
  distributable under GPL-3.
- FFTW 2 is itself GPL (v2 or later), which is compatible with GPL-3.
- For LGPL components, binary redistribution requires providing the library's
  license text and enabling relink/replacement of the library (satisfied here
  because the libraries are shipped as separate replaceable `.dylib` files).
- For Apache-2.0, BSD, MIT, PSF, and font-licensed components, retain the
  copyright and permission notices — this file plus the license files bundled
  inside the tarball serve that purpose.
- Corresponding source for the GPL/LGPL components is available from the
  Bandicoot repository and from each project's upstream.
