// python-classes.cc — Py2 module definition for the PathologyData class
// (used by B-factor histogram validation). Stubbed for Bandicoot's Py3
// build because porting the full PyTypeObject struct layout + replacing
// the removed Py_InitModule3 with PyModuleDef is multi-day work for a
// feature that isn't shipping in Bandicoot anyway (goograph is gated by
// HAVE_GOOCANVAS, which we build --without-gnomecanvas).
//
// The single entry point init_pathology_data() is now a no-op. Callers
// (coot-setup-python.cc, main.cc, c-interface-validate.cc) still link
// against it cleanly; the pathology_data Python module just isn't
// registered, so any Python script that tried to import it would fail
// at import time rather than at compile time.
//
// To restore: port the PyTypeObject layout to Py3 (struct fields
// shifted, tp_print removed, etc.) and replace Py_InitModule3 with
// the static PyModuleDef + PyModule_Create2 pattern.

#ifdef USE_PYTHON

#include <Python.h>
#include "python-classes.hh"

#ifndef PyMODINIT_FUNC
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC
init_pathology_data() {
    // Bandicoot Py3 stub. See file header for details.
}

#endif // USE_PYTHON
