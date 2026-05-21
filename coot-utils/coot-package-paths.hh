// -*-c++-*-
//
// coot-utils/coot-package-paths.hh
//
// Runtime data-dir lookup for relocatable installs. Returns the value of
// the COOT_DATA_DIR / COOT_PIXMAPS_DIR environment variables when set
// (the bcoot wrapper sets them from $COOT_PREFIX), falling back to the
// autoconf-supplied PKGDATADIR. Use these helpers instead of bare
// PKGDATADIR so the binary loads its data files regardless of where the
// tarball is unpacked.

#ifndef COOT_UTILS_COOT_PACKAGE_PATHS_HH
#define COOT_UTILS_COOT_PACKAGE_PATHS_HH

#include <cstdlib>
#include <string>

#ifndef PKGDATADIR
#  define PKGDATADIR "/usr/local/share/coot"
#endif

namespace coot {

   inline std::string package_data_dir() {
      const char *e = std::getenv("COOT_DATA_DIR");
      if (e && e[0]) return std::string(e);
      return std::string(PKGDATADIR);
   }

   inline std::string package_pixmaps_dir() {
      const char *e = std::getenv("COOT_PIXMAPS_DIR");
      if (e && e[0]) return std::string(e);
      return package_data_dir() + "/pixmaps";
   }

}

#endif // COOT_UTILS_COOT_PACKAGE_PATHS_HH
