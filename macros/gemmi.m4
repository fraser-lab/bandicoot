# gemmi.m4
#
# Bandicoot v0.2: locate gemmi, used as the mmCIF I/O layer (Scope A -- mmdb
# remains the model).
#
# Unlike mmdb2 and clipper, gemmi ships NO pkg-config file (Homebrew 0.7.5
# installs only a CMake config), so this checks headers and the library
# directly rather than asking pkg-config.
#
# Two facts this test is shaped around:
#   - gemmi/mmdb.hpp is header-only (copy_to_mmdb / copy_from_mmdb are inline)
#     and #includes <mmdb2/mmdb_manager.h>, so MMDB_CXXFLAGS must be in scope.
#     Call this macro AFTER AM_PATH_MMDB2.
#   - the CIF I/O we actually need is NOT header-only: read_cif_gz and
#     update_mmcif_block are GEMMI_DLL, so libgemmi_cpp is a hard link-time
#     requirement. The test therefore LINKS rather than merely compiling.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or (at
# your option) any later version.

AC_DEFUN([AM_PATH_GEMMI],
[
AC_PROVIDE([AM_PATH_GEMMI])

AC_ARG_WITH([gemmi],
   AS_HELP_STRING([--with-gemmi=PFX],
                  [prefix where gemmi is installed (default: compiler search path)]),
   [gemmi_prefix="$withval"], [gemmi_prefix=""])

AC_MSG_CHECKING([for gemmi])

if test "x$gemmi_prefix" != x ; then
   GEMMI_CXXFLAGS="-I$gemmi_prefix/include"
   GEMMI_LIBS="-L$gemmi_prefix/lib -lgemmi_cpp"
else
   GEMMI_CXXFLAGS=""
   GEMMI_LIBS="-lgemmi_cpp"
fi

save_CXXFLAGS="$CXXFLAGS"
save_LIBS="$LIBS"
CXXFLAGS="$CXXFLAGS $GEMMI_CXXFLAGS $MMDB_CXXFLAGS"
LIBS="$LIBS $GEMMI_LIBS $MMDB_LIBS"

AC_LANG_PUSH(C++)
dnl read_structure_file exercises the header set; read_cif_gz is GEMMI_DLL and
dnl so proves libgemmi_cpp is present and linkable, which is the real question.
AC_LINK_IFELSE([AC_LANG_PROGRAM(
   [[#include <gemmi/mmread.hpp>
     #include <gemmi/read_cif.hpp>
     #include <gemmi/mmdb.hpp>
     #include <gemmi/polyheur.hpp>]],
   [[gemmi::Structure st;
     gemmi::setup_entities(st);
     st.merge_chain_parts();
     mmdb::Manager *mol = new mmdb::Manager;
     gemmi::copy_to_mmdb(st, mol);
     gemmi::cif::Document doc = gemmi::read_cif_gz("/dev/null");]])],
   [coot_found_gemmi=yes], [coot_found_gemmi=no])
AC_LANG_POP(C++)

CXXFLAGS="$save_CXXFLAGS"
LIBS="$save_LIBS"

AC_MSG_RESULT($coot_found_gemmi)

if test $coot_found_gemmi = no ; then
   AC_MSG_FAILURE([gemmi not found (headers and/or libgemmi_cpp).
   Bandicoot v0.2 needs gemmi for mmCIF I/O. On macOS: brew install gemmi.
   If it lives outside the compiler search path, pass --with-gemmi=PREFIX.])
fi

AC_SUBST(GEMMI_CXXFLAGS)
AC_SUBST(GEMMI_LIBS)

])
