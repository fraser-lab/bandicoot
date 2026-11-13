
lib_LTLIBRARIES = libcoot-compat.la
pkginclude_HEADERS = coot-getopt.h coot-sysdep.h getopt_int.h sleep-fixups.h \
	fontconfig-null-config.hh

pkgincludedir = $(includedir)/coot/compat

libcoot_compat_la_SOURCES = getopt.cc getopt1.cc glob-mingw.cc

noinst_HEADERS = mingw/glob.h mingw/win32-name-clashes.h

libcoot_compat_la_LDFLAGS = $(SHARED_LDFLAGS)
