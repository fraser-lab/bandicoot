
#ifdef USE_PYTHON

void setup_python_classes();

void init_pathology_data();

// static
PyObject *PathologyData_new(PyTypeObject *type, PyObject *args, PyObject *kwds);

#endif
