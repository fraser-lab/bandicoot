# bandicoot_console.py -- eval/exec backend for the native macOS Python
# scripting console.
#
# Bandicoot has no PyGTK, so the console GUI (a multi-line editor + Run / Save /
# Exit buttons + a read-only output pane) is built in C
# (create_python_window / setup_python_window in src/). This module supplies the
# language engine the C side calls on every Run.
#
# _bandicoot_console_eval(cmd) is exposed to C. Because this file is loaded by
# coot_load_modules.py via exec(code, globals()) and that loader runs in
# __main__, this module's globals() *is* __main__ -- the same namespace as the
# old single-line entry and every coot command. So user variables persist
# between Runs and all coot functions are already in scope, no imports needed.

import sys as _bcon_sys
import io as _bcon_io
import traceback as _bcon_traceback


def _bandicoot_console_eval(cmd):
    """Run `cmd` in the __main__ namespace with stdout+stderr captured.

    Returns a 2-tuple (had_error, output_text) that the C caller unpacks:
      * had_error   -- bool; True if the code raised (C colours the text red)
      * output_text -- str; captured stdout/stderr, plus the repr of a bare
                       expression (REPL-style echo), plus a traceback on error.

    A single expression is eval'd so its value echoes like a real prompt;
    anything else (assignments, multi-line blocks, whole scripts) is exec'd.
    """
    if cmd is None:
        return (False, "")

    g = globals()  # == __main__ (see module docstring)
    buf = _bcon_io.StringIO()
    old_out, old_err = _bcon_sys.stdout, _bcon_sys.stderr
    _bcon_sys.stdout = buf
    _bcon_sys.stderr = buf
    had_error = False
    try:
        try:
            code = compile(cmd, "<console>", "eval")
        except SyntaxError:
            # not a single expression -- run as statements
            code = compile(cmd, "<console>", "exec")
            exec(code, g)
        else:
            result = eval(code, g)
            if result is not None:
                buf.write(repr(result) + "\n")
    except SystemExit:
        # a user calling exit()/quit() shouldn't kill Bandicoot
        buf.write("(SystemExit ignored in the scripting console)\n")
    except BaseException:
        had_error = True
        exc_type, exc_val, tb = _bcon_sys.exc_info()
        # Drop our own eval/exec frame so the traceback starts at the user's
        # code. Compile-time errors (SyntaxError) have no user frame -- show
        # just the error, not our internals.
        user_tb = tb.tb_next if tb is not None else None
        if user_tb is not None:
            buf.write("".join(
                _bcon_traceback.format_exception(exc_type, exc_val, user_tb)))
        else:
            buf.write("".join(
                _bcon_traceback.format_exception_only(exc_type, exc_val)))
    finally:
        _bcon_sys.stdout = old_out
        _bcon_sys.stderr = old_err

    return (had_error, buf.getvalue())
