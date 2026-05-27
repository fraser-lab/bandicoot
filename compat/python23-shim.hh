/* compat/python23-shim.hh — Py2 → Py3 compatibility for Coot's C interface.
 *
 * Coot 0.9 was written against Python 2 and is full of PyString_/PyInt_
 * call sites (~350 of them across c-interface*.cc). Python 3 removed
 * those APIs. Bandicoot uses Python 3 (--with-python=$CONDA_PREFIX),
 * which means without this shim every PyString_FromString site would
 * fail to compile.
 *
 * Strategy: macro-rewrite the Py2 names to their Py3 equivalents.
 * Covers ~90%+ of call sites verbatim. The remaining cases (mostly
 * char* vs const char* return-type mismatches around PyString_AsString)
 * need targeted fixes in the source.
 *
 * This header is force-included into every TU via -include in
 * scripts/build.sh's CXXFLAGS (and CFLAGS for the few C files that
 * include Python.h). Including it before or after Python.h is fine —
 * it only rewrites identifier text; the PyUnicode_* / PyLong_*
 * symbols come from Python.h itself when that's pulled in.
 *
 * No-op on Python 2 builds (for source compatibility with upstream
 * Coot if anyone ever wants to build that way again).
 */

#ifndef BANDICOOT_PYTHON23_SHIM_HH
#define BANDICOOT_PYTHON23_SHIM_HH

/* No version guard. The shim is force-included via -include BEFORE
 * any source-file #include, so PY_MAJOR_VERSION isn't defined yet --
 * a `#if PY_MAJOR_VERSION >= 3` would always evaluate false and the
 * substitutions wouldn't apply. Instead we substitute unconditionally
 * (Bandicoot targets Py3 only -- no Py2 build). The PyUnicode_ and
 * PyLong_ symbols come from Python.h itself when that's pulled in
 * later in the TU. Files that never include Python.h get the defines
 * applied to tokens they don't use -- harmless.
 *
 * The #ifndef guards around each define let upstream code define its
 * own override first if needed. */

/* PyString → PyUnicode (string objects).
 *
 * PyUnicode_AsUTF8 returns const char*; some Coot sites assign the
 * result to char* (mutating callers are rare but exist). Those need
 * manual fixes — the compiler will flag each one. */
#ifndef PyString_FromString
#define PyString_FromString  PyUnicode_FromString
#endif
#ifndef PyString_FromStringAndSize
#define PyString_FromStringAndSize  PyUnicode_FromStringAndSize
#endif
#ifndef PyString_AsString
#define PyString_AsString    PyUnicode_AsUTF8
#endif
#ifndef PyString_Check
/* Caveat: Py2 PyString_Check matched bytes-like; PyUnicode_Check on Py3
 * matches only unicode. If a call site needs to accept bytes too, fix
 * it explicitly with PyBytes_Check + decode. */
#define PyString_Check       PyUnicode_Check
#endif
#ifndef PyString_Size
#define PyString_Size        PyUnicode_GetLength
#endif
#ifndef PyString_FromFormat
#define PyString_FromFormat  PyUnicode_FromFormat
#endif
#ifndef PyString_Format
#define PyString_Format      PyUnicode_Format
#endif

/* PyInt → PyLong (integer objects). Python 3 unified int + long. */
#ifndef PyInt_FromLong
#define PyInt_FromLong       PyLong_FromLong
#endif
#ifndef PyInt_AsLong
#define PyInt_AsLong         PyLong_AsLong
#endif
#ifndef PyInt_Check
#define PyInt_Check          PyLong_Check
#endif
#ifndef PyInt_AS_LONG
#define PyInt_AS_LONG        PyLong_AsLong
#endif
#ifndef PyInt_Type
#define PyInt_Type           PyLong_Type
#endif

/* PyEval_CallObject was removed in Py3.13. PyObject_Call(obj, args, NULL)
 * is the modern equivalent; PyObject_CallObject(obj, args) is the
 * convenience form that matches the old signature. */
#ifndef PyEval_CallObject
#define PyEval_CallObject    PyObject_CallObject
#endif

#endif /* BANDICOOT_PYTHON23_SHIM_HH */
