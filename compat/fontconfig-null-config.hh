/* compat/fontconfig-null-config.hh
 *
 * Bandicoot: hand the default Pango font map an explicit fontconfig
 * configuration before GTK or wx first renders text. Linux only.
 *
 * fontconfig 2.17 (Ubuntu 26.04's 2.17.1 among them) reads
 * config->prefer_app_fonts in FcFontSetSort() without first replacing a NULL
 * config with the current one, although the API allows NULL. Pango up to 1.46
 * -- including the 1.44 the Linux release tarball bundles -- passes its font
 * map's config there, which is NULL unless one was set, so the first character
 * that needs a fallback font segfaults in FcFontSetSort. fontconfig has since
 * fixed this upstream; setting the config ourselves keeps every combination
 * working.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COMPAT_FONTCONFIG_NULL_CONFIG_HH
#define COMPAT_FONTCONFIG_NULL_CONFIG_HH

#if defined(__linux__)

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

// Call before gtk_init() / wxEntryStart(). GTK 2 and wxGTK both draw with
// pango_cairo_font_map_get_default(), which is per thread: call it from the
// thread that runs the UI.
inline void bandicoot_set_pango_fontconfig_config() {

   if (! FcInit())
      return;
   PangoFontMap *font_map = pango_cairo_font_map_get_default();
   if (PANGO_IS_FC_FONT_MAP(font_map))
      pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(font_map), FcConfigGetCurrent());
}

#else

inline void bandicoot_set_pango_fontconfig_config() {}

#endif // __linux__

#endif // COMPAT_FONTCONFIG_NULL_CONFIG_HH
