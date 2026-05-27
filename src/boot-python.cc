
#ifdef USE_PYTHON
#include "Python.h"  // before guile includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include "boot-python.hh"

void start_command_line_python_maybe(char **argv) {

#ifdef USE_PYTHON

   // Bandicoot doesn't expose a command-line Python REPL (no --python
   // flag), so we skip Py_Main entirely. On Py3 it takes wchar_t**
   // anyway, not char**, which would need Py_DecodeLocale conversion.
   (void) argv;

   // Skip initialization registration of signal handlers, useful when
   // Python is embedded.
   Py_InitializeEx(0);

#endif
}
