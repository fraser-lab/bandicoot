dnl AM_PATH_GLOB -- MinGW only (called from configure.ac's MinGW branch).
dnl MinGW-w64 has no glob(); the tree's own implementation is in
dnl compat/glob-mingw.cc (built into libcoot-compat) with its header in
dnl compat/mingw/.
AC_DEFUN([AM_PATH_GLOB],
[
AC_PROVIDE([AM_PATH_GLOB])

AC_MSG_CHECKING([for glob])
GLOB_CFLAGS='-I$(top_srcdir)/compat/mingw'
GLOB_LIBS='$(top_builddir)/compat/libcoot-compat.la'
AC_MSG_RESULT([compat/glob-mingw.cc])

AC_SUBST(GLOB_CFLAGS)
AC_SUBST(GLOB_LIBS)
])
