// python-classes.cc -- the PathologyData Python class (B-factor histogram
// validation) was Python 2 only and is not built. init_pathology_data() is
// kept as a no-op so its caller needs no change.
//
// Declared void, not PyMODINIT_FUNC: under Python 3 that returns PyObject *,
// and a value-returning function with no return statement is undefined
// behaviour (GCC emits no code for it at all, not even the return).

#ifdef USE_PYTHON

#include <Python.h>
#include "python-classes.hh"

void
init_pathology_data() {
}

#endif // USE_PYTHON
