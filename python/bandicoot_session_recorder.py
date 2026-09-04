# bandicoot_session_recorder.py -- record a Bandicoot session as an event log.
#
# Writes one JSON object per line (JSON Lines) describing what the user did:
# where they looked, what the maps showed there, which commands ran, and how
# the model changed. Three sources are merged into one file by timestamp:
#
#   1. Coot's command echo. Every GUI action that goes through a scripting-API
#      function is printed to stdout by add_to_history() (src/c-interface-info.cc,
#      on by default). We point file descriptor 1 at a child `tee` process that
#      forwards every byte to the original stdout and appends a copy to
#      <session>.stdout.log; the recorder reads that file incrementally and logs
#      the echoed commands as "command" events. The same lines are what Coot
#      writes to 0-coot-history.py at exit. (A reader *thread* inside the
#      process deadlocks: Coot writes to stdout while holding the GIL.)
#   2. The Python hooks Coot 0.9 already calls (src/graphics-info.cc):
#      post_set_rotation_centre_script  -- every recentre, including Space /
#                                          Shift-Space, which the history skips
#      post_manipulation_script         -- moving-atoms accept, delete, mutate
#      post_read_model_hook             -- a model was read
#      graphics_general_key_press_hook  -- keys not handled in C (key bindings)
#   3. Model snapshots. Coot writes a backup file before every model edit
#      (molecule_class_info_t::make_backup). A new backup, a manipulation hook,
#      or a periodic timer triggers a residue-level diff of the live model
#      against the previous snapshot: "edit" events such as add_water,
#      add_altloc, delete_residue, mutate, move. A snapshot is one call,
#      molecule_to_pdb_string_py, parsed here.
#
# A main-loop timer (gobject.timeout_add, which Bandicoot routes to GLib via
# bandicoot_python_timeout_add) polls the nearest residue so that drag-panning,
# which fires no hook, is recorded too. Everything runs on the main thread;
# there are no threads.
#
# Most scripting calls in Coot 0.9 are themselves added to the history and
# echoed, so the recorder uses the ones that are not (active_residue_py,
# residue_info_py, map_sigma_py, density_at_point, map_peaks_around_molecule_py,
# molecule_to_pdb_string_py, zoom_factor, is_valid_*). The exceptions it cannot
# avoid, rotation_centre_position (three calls per view) and molecule_name
# (once per molecule), are filtered out of the command stream but do land in
# 0-coot-history.py.
#
# Start recording (any one of):
#   BANDICOOT_RECORD=1 bcoot                          environment variable
#   start_session_recording()                         scripting console
#   echo 'start_session_recording()' > ~/.coot-preferences/record_sessions.py
# Logs go to ~/bandicoot-sessions/ (override with BANDICOOT_SESSION_DIR).
#
# Read a log without Bandicoot:
#   python3 bandicoot_session_recorder.py summarize ~/bandicoot-sessions/<file>.jsonl
# Self-test without Bandicoot (fake coot module):
#   python3 bandicoot_session_recorder.py selftest
#
# See SESSION_RECORDING.md for the event schema and the caveats.

import atexit
import datetime as _bsr_datetime
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
import traceback

try:
    import coot as _bsr_coot_module
except ImportError:            # command-line use outside Bandicoot
    _bsr_coot_module = None

import __main__ as _bsr_main

BSR_VERSION = "0.1.0"

# src/manipulation-modes.hh
_BSR_MODES = {2: "DELETED", 3: "MUTATED", 4: "MOVINGATOMS"}

_BSR_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_BSR_BOLD = "\x1b[1m"
_BSR_CMD_PREFIX = "INFO:: Command: "
_BSR_BACKUP_LINE = re.compile(r"^INFO:: backup file name (.+?)\s*$")
_BSR_BACKUP_DIR_LINE = re.compile(r"^INFO using backup directory (.+?)\s*$")
_BSR_MODIFICATION = re.compile(r"_modification_(\d+)\.(?:pdb|cif|res)(?:\.gz)?$")
_BSR_PY_CMD = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*$", re.S)
_BSR_SCM_CMD = re.compile(r"^\(([^\s()]+)\s*(.*)\)\s*$", re.S)

# scripting functions the recorder itself calls that Coot echoes; dropped from
# the command stream so the log shows only what the user did
_BSR_INTERNAL_COMMANDS = {"rotation_centre_position", "molecule_name"}

# state queries Coot's own Python code issues at startup and on redraws; kept in
# the log with "noise": true and hidden from the summary
_BSR_NOISE_COMMANDS = {"use_graphics_interface_state", "filter_fileselection_filenames_state",
                       "get_active_map_drag_flag", "set_display_intro_string"}


def _bsr_parse_pdb_string(text):
    """ATOM/HETATM records -> {(chain, resno, ins): (resname, {(atom, alt): (x, y, z, occ, b)})}."""
    res = {}
    for line in text.splitlines():
        if not (line.startswith("ATOM") or line.startswith("HETATM")) or len(line) < 54:
            continue
        try:
            name = line[12:16].strip()
            alt = line[16].strip()
            rname = line[17:20].strip()
            chain = line[20:22].strip()
            try:
                resno = int(line[22:26])
            except ValueError:
                resno = line[22:26].strip()
            ins = line[26].strip()
            x, y, z = float(line[30:38]), float(line[38:46]), float(line[46:54])
            occ = float(line[54:60]) if len(line) >= 60 and line[54:60].strip() else 1.0
            b = float(line[60:66]) if len(line) >= 66 and line[60:66].strip() else 0.0
        except ValueError:
            continue
        key = (chain, resno, ins)
        if key not in res:
            res[key] = (rname, {})
        res[key][1][(name, alt)] = (x, y, z, occ, b)
    return res


def _bsr_now_iso():
    return _bsr_datetime.datetime.now().astimezone().isoformat(timespec="milliseconds")


def _bsr_env_flag(name, default=False):
    v = os.environ.get(name)
    if v is None:
        return default
    return v.strip().lower() not in ("", "0", "no", "false", "off")


def _bsr_default_session_dir():
    d = os.environ.get("BANDICOOT_SESSION_DIR")
    if not d:
        d = os.path.join(os.path.expanduser("~"), "bandicoot-sessions")
    return os.path.expanduser(d)


def _bsr_default_session_path():
    stamp = _bsr_datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return os.path.join(_bsr_default_session_dir(), "bandicoot-session-%s-%d.jsonl" % (stamp, os.getpid()))


def _bsr_spec(chain, resno, ins, name=None):
    s = "%s/%s%s" % (chain, resno, (ins or "").strip())
    if name:
        s += " " + name
    return s


def _bsr_dist(a, b):
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(a, b)))


def _bsr_parse_command(text):
    """'set_rotation_centre (1.0, 2.0, 3.0)' or '(set-rotation-centre 1.0 2.0 3.0)'
    -> (python_name, raw_args, syntax)."""
    m = _BSR_PY_CMD.match(text)
    if m:
        return m.group(1), m.group(2).strip(), "python"
    m = _BSR_SCM_CMD.match(text)
    if m:
        return m.group(1).replace("-", "_"), m.group(2).strip(), "scheme"
    return None, None, None


# ---------------------------------------------------------------- snapshots

def _bsr_diff_snapshots(old, new, max_list=20):
    """Compare two model snapshots {(chain, resno, ins): (resname, {(atom, alt): (x, y, z, occ, b)})}.
    Returns a list of change dicts, waters grouped."""
    changes = []

    added = [k for k in new if k not in old]
    removed = [k for k in old if k not in new]

    def group(keys, snap, water_type, other_type):
        waters = [k for k in keys if snap[k][0] == "HOH"]
        others = [k for k in keys if snap[k][0] != "HOH"]
        if waters:
            waters.sort()
            ch = {"type": water_type, "count": len(waters),
                  "residues": [_bsr_spec(*k) for k in waters[:max_list]]}
            if len(waters) > max_list:
                ch["residues_truncated"] = len(waters) - max_list
            changes.append(ch)
        for k in sorted(others):
            changes.append({"type": other_type, "residue": _bsr_spec(*k, name=snap[k][0]),
                            "n_atoms": len(snap[k][1])})

    group(added, new, "add_water", "add_residue")
    group(removed, old, "delete_water", "delete_residue")

    for k in sorted(k for k in new if k in old):
        oname, oatoms = old[k]
        nname, natoms = new[k]
        if oname == nname and oatoms == natoms:
            continue
        spec = _bsr_spec(*k, name=nname)
        if oname != nname:
            changes.append({"type": "mutate", "residue": _bsr_spec(*k), "from": oname, "to": nname})
        oalts = set(a for (_n, a) in oatoms if a)
        nalts = set(a for (_n, a) in natoms if a)
        alt_changed = False
        if nalts - oalts:
            alt_changed = True
            letters = sorted(nalts - oalts)
            changes.append({"type": "add_altloc", "residue": spec, "altlocs": letters,
                            "n_atoms": sum(1 for (_n, a) in natoms if a in letters)})
        if oalts - nalts:
            alt_changed = True
            letters = sorted(oalts - nalts)
            changes.append({"type": "delete_altloc", "residue": spec, "altlocs": letters,
                            "n_atoms": sum(1 for (_n, a) in oatoms if a in letters)})
        if not alt_changed and oname == nname:
            n_added = len(set(natoms) - set(oatoms))
            n_removed = len(set(oatoms) - set(natoms))
            if n_added:
                changes.append({"type": "add_atoms", "residue": spec, "n_atoms": n_added})
            if n_removed:
                changes.append({"type": "delete_atoms", "residue": spec, "n_atoms": n_removed})
        common = [a for a in natoms if a in oatoms]
        max_shift = 0.0
        n_moved = 0
        max_docc = 0.0
        max_db = 0.0
        for a in common:
            o = oatoms[a]
            n = natoms[a]
            d = _bsr_dist(o[:3], n[:3])
            if d > 0.01:
                n_moved += 1
                max_shift = max(max_shift, d)
            max_docc = max(max_docc, abs(float(n[3]) - float(o[3])))
            max_db = max(max_db, abs(float(n[4]) - float(o[4])))
        if n_moved:
            changes.append({"type": "move", "residue": spec, "n_atoms": n_moved,
                            "max_shift": round(max_shift, 2)})
        if max_docc > 0.001:
            changes.append({"type": "occupancy", "residue": spec, "max_delta": round(max_docc, 3)})
        if max_db > 0.01:
            changes.append({"type": "bfactor", "residue": spec, "max_delta": round(max_db, 2)})
    return changes


# ---------------------------------------------------------------- stdout tee

class _BsrStdoutTee:
    """Point file descriptor 1 at a child `tee` process.

    Coot's C++ (std::cout) and Python's sys.stdout both write to fd 1, so both
    reach tee, which forwards every byte to the original stdout and appends a
    copy to the sidecar file. The recorder reads the sidecar incrementally from
    the main thread (poll()). No thread is involved: a reader thread in this
    process would deadlock, because Coot writes to stdout while holding the GIL
    and blocks once the pipe is full."""

    def __init__(self, sidecar):
        self.sidecar = sidecar
        self._saved = None
        self._proc = None
        self._fh = None
        self._buf = b""

    def start(self):
        tee = shutil.which("tee")
        if not tee:
            raise RuntimeError("no `tee` executable on PATH")
        try:
            sys.stdout.flush()
        except Exception:
            pass
        d = os.path.dirname(self.sidecar)
        if d:
            os.makedirs(d, exist_ok=True)
        open(self.sidecar, "ab").close()
        self._fh = open(self.sidecar, "rb")
        self._saved = os.dup(1)
        self._proc = subprocess.Popen([tee, "-a", self.sidecar], stdin=subprocess.PIPE,
                                      stdout=self._saved, stderr=subprocess.DEVNULL, close_fds=True)
        os.dup2(self._proc.stdin.fileno(), 1)
        self._proc.stdin.close()            # fd 1 is now the only write end we hold
        try:
            sys.stdout.reconfigure(line_buffering=True)
        except Exception:
            pass

    def poll(self):
        """Complete new lines written since the last call."""
        if self._fh is None:
            return []
        try:
            data = self._fh.read()
        except OSError:
            return []
        if not data:
            return []
        self._buf += data
        parts = self._buf.split(b"\n")
        self._buf = parts.pop()
        return [p.decode("utf-8", "replace") for p in parts]

    def stop(self):
        if self._saved is None:
            return []
        try:
            sys.stdout.flush()
        except Exception:
            pass
        os.dup2(self._saved, 1)              # fd 1 leaves the pipe; tee sees EOF
        try:
            self._proc.wait(timeout=2.0)
        except Exception:
            try:
                self._proc.terminate()
            except Exception:
                pass
        lines = self.poll()
        for closer in (self._fh.close, lambda: os.close(self._saved)):
            try:
                closer()
            except Exception:
                pass
        self._saved = self._proc = self._fh = None
        return lines


# ---------------------------------------------------------------- recorder

class _BsrRecorder:

    def __init__(self, coot_api, main_ns, path=None, capture_stdout=True, rank_peaks=True,
                 use_timer=True, poll_ms=500, periodic_s=60.0, backup_scan_s=2.0,
                 peak_sigma=3.0, peak_ttl_s=300.0, record_all_stdout=False, max_changes=50):
        self.coot = coot_api
        self._main = main_ns
        self.path = path or _bsr_default_session_path()
        self.stdout_path = os.path.splitext(self.path)[0] + ".stdout.log"
        self.capture_stdout = capture_stdout
        self.rank_peaks = rank_peaks
        self.use_timer = use_timer
        self.poll_ms = int(poll_ms)
        self.periodic_s = float(periodic_s)
        self.backup_scan_s = float(backup_scan_s)
        self.peak_sigma = float(peak_sigma)
        self.peak_ttl_s = float(peak_ttl_s)
        self.record_all_stdout = record_all_stdout
        self.max_changes = int(max_changes)

        self.running = False
        self._fh = None
        self._seq = 0
        self._t0 = None
        self._timer_id = 0
        self._tee = None
        self._hooks = {}
        self._prev_hooks = {}
        self._snapshots = {}
        self._mol_seen = {}
        self._names = {}
        self._last_view_xyz = None
        self._last_view_residue = None
        self._backup_dirs = []
        self._backup_seen = set()
        self._backup_last_scan = 0.0
        self._pending_backups = []
        self._peak_cache = {}
        self._last_periodic = 0.0
        self._snapshot_cost = 0.0
        self._errors = 0
        self._started_at = None

    # ---- lifecycle

    def start(self):
        if self.running:
            return self.path
        d = os.path.dirname(self.path)
        if d:
            os.makedirs(d, exist_ok=True)
        self._fh = open(self.path, "a", encoding="utf-8")
        self._t0 = time.time()
        self._started_at = _bsr_now_iso()
        self.running = True
        self._backup_dirs = self._find_backup_dirs()
        for bd in self._backup_dirs:
            self._backup_seen.update(self._list_backups(bd))   # pre-existing backups are not events
        self._emit("session_start", recorder_version=BSR_VERSION, program_version=self._program_version(),
                   pid=os.getpid(), argv=list(sys.argv), cwd=os.getcwd(),
                   python=sys.version.split()[0], backup_dirs=list(self._backup_dirs),
                   options={"capture_stdout": self.capture_stdout, "rank_peaks": self.rank_peaks,
                            "poll_ms": self.poll_ms, "periodic_s": self.periodic_s,
                            "peak_sigma": self.peak_sigma})
        self._install_hooks()
        self._scan_molecules()
        if self.capture_stdout:
            try:
                self._tee = _BsrStdoutTee(self.stdout_path)
                self._tee.start()
                self._emit("stdout_capture", file=self.stdout_path)
            except Exception:
                self._tee = None
                self._error("stdout_tee_start")
        if self.use_timer:
            self._start_timer()
        try:
            atexit.register(self._atexit)
        except Exception:
            pass
        return self.path

    def stop(self):
        if not self.running:
            return
        try:
            self._service()
            self._diff_all(trigger="stop")
        except Exception:
            self._error("stop_diff")
        if self._tee is not None:
            try:
                for line in self._tee.stop():
                    self._on_stdout_line(line)
            except Exception:
                self._error("stdout_tee_stop")
            self._tee = None
        self._emit("session_end", events=self._seq, errors=self._errors)
        self.running = False            # the timer callback returns False on its next call
        self._restore_hooks()
        try:
            self._fh.close()
        except Exception:
            pass
        self._fh = None

    def _atexit(self):
        if self.running:
            try:
                self.stop()
            except Exception:
                pass

    def status(self):
        return {"running": self.running, "path": self.path, "started": self._started_at,
                "events": self._seq, "errors": self._errors, "timer": bool(self._timer_id),
                "stdout_capture": self._tee is not None, "stdout_file": self.stdout_path,
                "backup_dirs": list(self._backup_dirs),
                "models_tracked": sorted(self._snapshots), "molecules": dict(self._mol_seen)}

    def note(self, text):
        self._emit("note", text=str(text))

    # ---- output

    def _emit(self, event, **fields):
        if self._fh is None:
            return
        self._seq += 1
        rec = {"t": _bsr_now_iso(), "dt": round(time.time() - self._t0, 3), "seq": self._seq,
               "event": event}
        rec.update(fields)
        try:
            line = json.dumps(rec, default=str, ensure_ascii=False)
        except Exception:
            line = json.dumps({"t": rec["t"], "dt": rec["dt"], "seq": rec["seq"],
                               "event": "error", "where": "json", "text": repr(rec)[:2000]})
        self._fh.write(line + "\n")
        self._fh.flush()

    def _error(self, where):
        self._errors += 1
        if self._errors <= 5 or self._errors % 50 == 0:
            self._emit("error", where=where, count=self._errors,
                       traceback=traceback.format_exc()[-2000:])

    # ---- hooks

    def _install_hooks(self):
        self._hooks = {
            "post_set_rotation_centre_script": self._hook_recentre,
            "post_manipulation_script": self._hook_manipulation,
            "post_read_model_hook": self._hook_read_model,
            "graphics_general_key_press_hook": self._hook_key,
        }
        for name, fn in self._hooks.items():
            self._wrap_hook(name, fn)

    def _wrap_hook(self, name, fn):
        current = getattr(self._main, name, None)
        if current is fn:
            return
        self._prev_hooks[name] = current if callable(current) else None
        setattr(self._main, name, fn)

    def _ensure_hooks(self):
        # A script loaded after us (~/.coot-preferences, ~/.coot.py) may have
        # replaced a hook. Wrap it again and keep calling it.
        for name, fn in self._hooks.items():
            if getattr(self._main, name, None) is not fn:
                self._wrap_hook(name, fn)

    def _restore_hooks(self):
        for name, fn in self._hooks.items():
            if getattr(self._main, name, None) is fn:
                prev = self._prev_hooks.get(name)
                if prev is None:
                    # coot_utils convention: False means "no hook". The key hook
                    # must stay callable because C calls it unconditionally.
                    prev = (lambda *a, **k: None) if name == "graphics_general_key_press_hook" else False
                setattr(self._main, name, prev)

    def _chain(self, name, *args):
        prev = self._prev_hooks.get(name)
        if callable(prev):
            try:
                return prev(*args)
            except Exception:
                self._error("chained_" + name)
        return None

    def _hook_recentre(self):
        try:
            if self.running:
                self._service()
                self._record_view("hook")
        except Exception:
            self._error("recentre")
        return self._chain("post_set_rotation_centre_script")

    def _hook_manipulation(self, imol, mode):
        try:
            if self.running:
                self._service()
                mode_name = _BSR_MODES.get(mode, mode)
                self._emit("manipulation", imol=imol, mode=mode_name, molecule=self._mol_name(imol))
                self._diff_molecule(imol, trigger="hook", mode=mode_name)
        except Exception:
            self._error("manipulation")
        return self._chain("post_manipulation_script", imol, mode)

    def _hook_read_model(self, imol):
        try:
            if self.running:
                self._service()
                info = self._mol_info(imol)
                self._emit("read_model", imol=imol, molecule=info[1] if info else None)
                if info and info[0] == "model":
                    self._mol_seen[imol] = info
                    self._snapshots[imol] = self._snapshot(imol)
        except Exception:
            self._error("read_model")
        return self._chain("post_read_model_hook", imol)

    def _hook_key(self, key, control_flag=0):
        try:
            if self.running:
                self._service()
                k = chr(key) if isinstance(key, int) and 32 <= key < 127 else key
                self._emit("key", key=k, keyval=key, ctrl=int(bool(control_flag)))
        except Exception:
            self._error("key")
        return self._chain("graphics_general_key_press_hook", key, control_flag)

    # ---- timer

    def _start_timer(self):
        tid = 0
        try:
            import gobject      # Bandicoot's shim: timeout_add -> GLib main loop
            tid = gobject.timeout_add(self.poll_ms, self._tick)
        except Exception:
            tid = 0
        if not tid and hasattr(self.coot, "bandicoot_python_timeout_add"):
            try:
                tid = self.coot.bandicoot_python_timeout_add(self.poll_ms, self._tick)
            except Exception:
                tid = 0
        self._timer_id = tid or 0
        if not tid:
            self._emit("warning", text="no main-loop timer available: drag-panning and edits that fire "
                                       "no hook are recorded at the next hook event instead")

    def _service(self):
        # Cheap upkeep, run from every hook and every tick: re-wrap hooks that a
        # later script replaced, process backup notices from the stdout reader,
        # and notice new or closed molecules. This keeps hook-only mode (no
        # main-loop timer, e.g. --no-graphics) current without polling.
        self._ensure_hooks()
        if self._tee is not None:
            for line in self._tee.poll():
                self._on_stdout_line(line)
        self._scan_molecules()
        self._flush_pending_backups()

    def _tick(self):
        if not self.running:
            return False
        try:
            self._service()
            self._poll_view()
            now = time.time()
            if now - self._backup_last_scan >= self.backup_scan_s:
                self._backup_last_scan = now
                self._scan_backup_dirs()
                self._flush_pending_backups()
            if self._periodic_due(now):
                self._last_periodic = now
                self._diff_all(trigger="periodic")
        except Exception:
            self._error("tick")
        return self.running

    def _periodic_due(self, now):
        if not self._snapshots:
            return False
        interval = max(self.periodic_s, 50.0 * self._snapshot_cost)
        return now - self._last_periodic >= interval

    # ---- views

    def _poll_view(self):
        # active_residue_py is silent; rotation_centre_position is echoed, so
        # only read the centre once the nearest atom has changed.
        try:
            ar = self.coot.active_residue_py()
        except Exception:
            return
        key = tuple(ar[:6]) if isinstance(ar, (list, tuple)) and len(ar) >= 6 else None
        if key == self._last_view_residue:
            return
        if self._last_view_residue is None and self._last_view_xyz is None:
            self._last_view_residue = key
            return
        self._record_view("poll")

    def _record_view(self, source, xyz=None):
        c = self.coot
        if xyz is None:
            xyz = [float(c.rotation_centre_position(i)) for i in range(3)]
        rec = {"source": source, "xyz": [round(v, 2) for v in xyz]}
        try:
            rec["zoom"] = round(float(c.zoom_factor()), 2)
        except Exception:
            pass
        res = self._active_residue()
        if res:
            rec["residue"] = res
        maps = self._map_context(xyz, res.get("imol") if res else None)
        if maps:
            rec["maps"] = maps
        self._emit("view", **rec)
        self._last_view_xyz = xyz
        self._last_view_residue = (res["imol"], res["chain"], res["resno"], res["ins"], res["atom_raw"], res["alt"]) if res else None

    def _active_residue(self):
        try:
            ar = self.coot.active_residue_py()
        except Exception:
            return None
        if not ar or not isinstance(ar, (list, tuple)) or len(ar) < 6:
            return None
        imol, chain, resno, ins, atom, alt = ar[:6]
        d = {"imol": imol, "chain": chain, "resno": resno, "ins": ins,
             "atom": (atom or "").strip(), "atom_raw": atom, "alt": alt}
        try:
            rn = self.coot.residue_name_py(imol, chain, resno, ins)
            if isinstance(rn, str):
                d["name"] = rn
        except Exception:
            pass
        d["spec"] = _bsr_spec(chain, resno, ins, d.get("name"))
        return d

    def _map_context(self, xyz, imol_model):
        c = self.coot
        out = []
        try:
            n = c.graphics_n_molecules()
        except Exception:
            return out
        for imol in range(n):
            try:
                if not c.is_valid_map_molecule(imol):
                    continue
                sigma = c.map_sigma_py(imol)
                if not sigma:
                    continue
                sigma = float(sigma)
                val = float(c.density_at_point(imol, xyz[0], xyz[1], xyz[2]))
                m = {"imol": imol, "name": self._mol_name(imol),
                     "difference": bool(c.map_is_difference_map(imol)),
                     "sigma_at_centre": round(val / sigma, 2)}
                try:
                    m["contour_sigma"] = round(float(c.get_contour_level_in_sigma(imol)), 2)
                except Exception:
                    pass
                if m["difference"] and self.rank_peaks:
                    pk = self._nearest_peak(imol, xyz, imol_model, sigma)
                    if pk:
                        m["peak"] = pk
                out.append(m)
            except Exception:
                self._error("map_context")
        return out

    def _first_model(self):
        c = self.coot
        try:
            for imol in range(c.graphics_n_molecules()):
                if c.is_valid_model_molecule(imol):
                    return imol
        except Exception:
            pass
        return None

    def _compute_peaks(self, imol_map, imol_model):
        c = self.coot
        t = time.time()
        raw = False
        if imol_model is None:
            imol_model = self._first_model()
        if imol_model is not None:
            try:
                raw = c.map_peaks_around_molecule_py(imol_map, self.peak_sigma, 1, imol_model)
            except Exception:
                raw = False
        peaks = []
        if isinstance(raw, (list, tuple)):
            for item in raw:
                try:
                    peaks.append((float(item[0]), [float(v) for v in item[1]]))
                except Exception:
                    continue
        peaks.sort(key=lambda hp: -abs(hp[0]))
        pos = [p for p in peaks if p[0] > 0]
        neg = [p for p in peaks if p[0] < 0]
        self._emit("peak_list", imol_map=imol_map, imol_model=imol_model, sigma=self.peak_sigma,
                   n_positive=len(pos), n_negative=len(neg), cost_s=round(time.time() - t, 3))
        return {"pos": pos, "neg": neg}

    def _nearest_peak(self, imol_map, xyz, imol_model, sigma):
        now = time.time()
        entry = self._peak_cache.get(imol_map)
        if entry is None or now - entry[0] > self.peak_ttl_s:
            entry = (now, self._compute_peaks(imol_map, imol_model))
            self._peak_cache[imol_map] = entry
        best = None
        for sign, plist in (("+", entry[1]["pos"]), ("-", entry[1]["neg"])):
            for rank, (h, p) in enumerate(plist, 1):
                d = _bsr_dist(p, xyz)
                if d <= 2.5 and (best is None or d < best[3]):
                    best = (sign, rank, h, d, len(plist))
        if best is None:
            return None
        sign, rank, h, d, n = best
        return {"sign": sign, "rank": rank, "of": n, "sigma": round(h / sigma, 2),
                "dist": round(d, 2), "search_sigma": self.peak_sigma,
                "list_age_s": round(now - entry[0], 1)}

    # ---- molecules and snapshots

    def _mol_name(self, imol, refresh=False):
        # molecule_name is echoed by Coot, so ask once per molecule slot.
        if not refresh and imol in self._names:
            return self._names[imol]
        try:
            n = self.coot.molecule_name(imol)
            n = n if isinstance(n, str) else str(n)
        except Exception:
            n = None
        self._names[imol] = n
        return n

    def _program_version(self):
        try:
            return str(self.coot.coot_version())
        except Exception:
            return None

    def _mol_info(self, imol):
        c = self.coot
        try:
            if c.is_valid_model_molecule(imol):
                kind = ("model", False)
            elif c.is_valid_map_molecule(imol):
                kind = ("map", bool(c.map_is_difference_map(imol)))
            else:
                return None
        except Exception:
            self._error("mol_info")
            return None
        old = self._mol_seen.get(imol)
        refresh = old is None or old[0] != kind[0] or old[2] != kind[1]
        return (kind[0], self._mol_name(imol, refresh=refresh), kind[1])

    def _scan_molecules(self):
        c = self.coot
        try:
            n = c.graphics_n_molecules()
        except Exception:
            return
        current = {}
        for imol in range(n):
            info = self._mol_info(imol)
            if info:
                current[imol] = info
        for imol, info in current.items():
            old = self._mol_seen.get(imol)
            if old == info:
                continue
            self._emit("molecule", imol=imol, kind=info[0], name=info[1], difference=info[2],
                       replaced=old is not None)
            self._mol_seen[imol] = info
            if info[0] == "model":
                self._snapshots[imol] = self._snapshot(imol)
            else:
                self._peak_cache.pop(imol, None)
        for imol in list(self._mol_seen):
            if imol not in current:
                info = self._mol_seen.pop(imol)
                self._snapshots.pop(imol, None)
                self._peak_cache.pop(imol, None)
                self._names.pop(imol, None)
                self._emit("molecule_closed", imol=imol, kind=info[0], name=info[1])

    def _snapshot(self, imol):
        c = self.coot
        t = time.time()
        if hasattr(c, "molecule_to_pdb_string_py"):
            try:
                text = c.molecule_to_pdb_string_py(imol)
            except Exception:
                text = None
            if isinstance(text, str):
                res = _bsr_parse_pdb_string(text)
                self._snapshot_cost = max(self._snapshot_cost * 0.8, time.time() - t)
                return res
        return self._snapshot_by_residue(imol, t)

    def _snapshot_by_residue(self, imol, t):
        # Fallback for builds without molecule_to_pdb_string_py. These
        # enumerators are echoed by Coot (four history lines per residue).
        c = self.coot
        res = {}
        try:
            n_ch = int(c.n_chains(imol))
        except Exception:
            return res
        for ich in range(max(n_ch, 0)):
            try:
                chain = c.chain_id_py(imol, ich)
            except Exception:
                continue
            if not isinstance(chain, str):
                continue
            try:
                n_res = int(c.chain_n_residues(chain, imol))
            except Exception:
                continue
            for ser in range(max(n_res, 0)):
                try:
                    resno = int(c.seqnum_from_serial_number(imol, chain, ser))
                    ins = c.insertion_code_from_serial_number(imol, chain, ser)
                    ins = ins if isinstance(ins, str) else ""
                    info = c.residue_info_py(imol, chain, resno, ins)
                    if not isinstance(info, (list, tuple)):
                        continue
                    rname = c.residue_name_py(imol, chain, resno, ins)
                    rname = rname if isinstance(rname, str) else "?"
                    atoms = {}
                    for at in info:
                        name_alt, attrib, pos = at[0], at[1], at[2]
                        b = attrib[1]
                        if isinstance(b, (list, tuple)):
                            b = b[0]                      # anisotropic: [Biso, u11, ...]
                        atoms[(str(name_alt[0]).strip(), str(name_alt[1]))] = (
                            float(pos[0]), float(pos[1]), float(pos[2]), float(attrib[0]), float(b))
                    res[(chain, resno, ins)] = (rname, atoms)
                except Exception:
                    self._error("snapshot_residue")
        cost = time.time() - t
        self._snapshot_cost = max(self._snapshot_cost * 0.8, cost)
        return res

    def _diff_molecule(self, imol, trigger, **extra):
        c = self.coot
        try:
            if not c.is_valid_model_molecule(imol):
                return False
        except Exception:
            return False
        old = self._snapshots.get(imol)
        new = self._snapshot(imol)
        self._snapshots[imol] = new
        if old is None:
            return False
        changes = _bsr_diff_snapshots(old, new)
        if not changes:
            return False
        summary = {}
        for ch in changes:
            summary[ch["type"]] = summary.get(ch["type"], 0) + ch.get("count", 1)
        rec = {"imol": imol, "molecule": self._mol_name(imol), "trigger": trigger,
               "summary": summary, "changes": changes[:self.max_changes]}
        if len(changes) > self.max_changes:
            rec["changes_truncated"] = len(changes) - self.max_changes
        res = self._active_residue()
        if res:
            rec["near"] = res["spec"]
        rec.update(extra)
        self._emit("edit", **rec)
        return True

    def _diff_all(self, trigger, **extra):
        for imol in list(self._snapshots):
            self._diff_molecule(imol, trigger, **extra)

    # ---- backups

    def _find_backup_dirs(self):
        cands = []
        env = os.environ.get("COOT_BACKUP_DIR")
        if env:
            cands.append(env)
        cands.append(os.path.abspath("coot-backup"))
        cands.append(os.path.join(os.path.expanduser("~"), "coot-backup"))
        out = []
        for d in cands:
            d = os.path.abspath(d)
            if os.path.isdir(d) and d not in out:
                out.append(d)
        return out

    def _add_backup_dir(self, d):
        d = os.path.abspath(d)
        if os.path.isdir(d) and d not in self._backup_dirs:
            self._backup_dirs.append(d)
            self._backup_seen.update(self._list_backups(d))

    @staticmethod
    def _list_backups(d):
        try:
            return set(os.path.join(d, f) for f in os.listdir(d))
        except OSError:
            return set()

    def _scan_backup_dirs(self):
        new = []
        for d in self._backup_dirs:
            for f in self._list_backups(d):
                if f not in self._backup_seen:
                    new.append(f)
        if not new:
            return
        def mtime(f):
            try:
                return os.path.getmtime(f)
            except OSError:
                return 0.0
        for f in sorted(new, key=mtime):
            self._handle_backup(f)

    def _handle_backup(self, path):
        path = os.path.abspath(path)
        if path in self._backup_seen:
            return
        self._backup_seen.add(path)
        base = os.path.basename(path)
        m = _BSR_MODIFICATION.search(base)
        hist = int(m.group(1)) if m else None
        imols = self._models_for_backup(base)
        self._emit("backup", file=base, history_index=hist, imols=imols)
        self._pending_backups.append((base, hist, imols))

    def _flush_pending_backups(self):
        # One diff per model for all backups seen since the last flush. Several
        # edits inside one tick (or, without a timer, between two hooks) then
        # give one edit event that lists every backup it covers.
        if not self._pending_backups:
            return
        pending, self._pending_backups = self._pending_backups, []
        per_imol = {}
        unattributed = []
        for base, hist, imols in pending:
            if imols:
                for imol in imols:
                    per_imol.setdefault(imol, []).append((base, hist))
            else:
                unattributed.append((base, hist))
        if unattributed:
            for imol in list(self._snapshots):
                per_imol.setdefault(imol, []).extend(unattributed)
        for imol, items in per_imol.items():
            files = [b for b, _h in items]
            hists = [h for _b, h in items if h is not None]
            self._diff_molecule(imol, trigger="backup", backup_files=files,
                                history_index=(hists[0] if len(hists) == 1 else hists))

    def _models_for_backup(self, base):
        # make_backup names the file from the molecule name with '/' and ' '
        # replaced (molecule-class-info.cc get_save_molecule_filename); by
        # default the whole path, optionally just the display name.
        out = []
        for imol in list(self._snapshots):
            name = self._mol_name(imol) or ""
            for cand in (name, os.path.basename(name)):
                s = cand.replace("/", "_").replace(" ", "_")
                if s and base.startswith(s + "_"):
                    out.append(imol)
                    break
        return out

    # ---- stdout lines (main thread, from the tee sidecar)

    def _on_stdout_line(self, raw):
        clean = _BSR_ANSI.sub("", raw).rstrip("\r")
        if self.record_all_stdout:
            self._emit("stdout", text=clean)
        cmd = None
        i = raw.find(_BSR_BOLD)
        if i >= 0:
            cmd = _BSR_ANSI.sub("", raw[i:]).strip()
        elif clean.startswith(_BSR_CMD_PREFIX):
            cmd = clean[len(_BSR_CMD_PREFIX):].strip()
        if cmd:
            name, args, syntax = _bsr_parse_command(cmd)
            if name in _BSR_INTERNAL_COMMANDS:
                return
            if name in _BSR_NOISE_COMMANDS:
                self._emit("command", text=cmd, name=name, args=args, syntax=syntax, noise=True)
            else:
                self._emit("command", text=cmd, name=name, args=args, syntax=syntax)
            return
        m = _BSR_BACKUP_LINE.match(clean)
        if m:
            try:
                self._add_backup_dir(os.path.dirname(m.group(1)))
                self._handle_backup(m.group(1))
            except Exception:
                self._error("backup_notice")
            return
        m = _BSR_BACKUP_DIR_LINE.match(clean)
        if m:
            self._add_backup_dir(m.group(1))


# ---------------------------------------------------------------- public API (scripting console)

def _bsr_current():
    return getattr(_bsr_main, "_bandicoot_session_recorder", None)


def start_session_recording(path=None, capture_stdout=None, rank_peaks=None):
    """Start recording this Bandicoot session. Returns the log file path."""
    rec = _bsr_current()
    if rec is not None and rec.running:
        print("INFO:: session recording already running: " + rec.path)
        return rec.path
    if _bsr_coot_module is None:
        raise RuntimeError("start_session_recording() needs the coot module (run inside Bandicoot)")
    if capture_stdout is None:
        capture_stdout = _bsr_env_flag("BANDICOOT_RECORD_STDOUT", True)
    if rank_peaks is None:
        rank_peaks = _bsr_env_flag("BANDICOOT_RECORD_PEAKS", True)
    rec = _BsrRecorder(_bsr_coot_module, _bsr_main, path=path, capture_stdout=capture_stdout,
                       rank_peaks=rank_peaks,
                       record_all_stdout=_bsr_env_flag("BANDICOOT_RECORD_ALL_STDOUT", False))
    setattr(_bsr_main, "_bandicoot_session_recorder", rec)
    p = rec.start()
    print("INFO:: session recording -> " + p)
    return p


def stop_session_recording():
    """Stop recording. The log file is complete once this returns."""
    rec = _bsr_current()
    if rec is None or not rec.running:
        print("INFO:: session recording is not running")
        return None
    rec.stop()
    print("INFO:: session recording stopped: " + rec.path)
    return rec.path


def session_recording_status():
    """Print and return the recorder state."""
    rec = _bsr_current()
    st = rec.status() if rec is not None else {"running": False}
    print("INFO:: session recording: " + json.dumps(st, default=str))
    return st


def session_note(text):
    """Add a free-text note to the running session log."""
    rec = _bsr_current()
    if rec is None or not rec.running:
        print("INFO:: session recording is not running; note discarded")
        return False
    rec.note(text)
    return True


# ---------------------------------------------------------------- command line: summarize

def _bsr_short_name(name, n=28):
    if not name:
        return "?"
    b = os.path.basename(str(name))
    return b if len(b) <= n else b[:n - 1] + "~"


def _bsr_format_change(ch):
    t = ch.get("type")
    if t in ("add_water", "delete_water"):
        s = "%s x%d" % (t, ch.get("count", 0))
        if ch.get("residues"):
            s += " (" + ", ".join(ch["residues"]) + (", ..." if ch.get("residues_truncated") else "") + ")"
        return s
    if t == "mutate":
        return "mutate %s %s->%s" % (ch.get("residue"), ch.get("from"), ch.get("to"))
    if t in ("add_altloc", "delete_altloc"):
        return "%s %s on %s (%d atoms)" % (t, ",".join(ch.get("altlocs", [])), ch.get("residue"), ch.get("n_atoms", 0))
    if t == "move":
        return "move %s (%d atoms, max %.2f A)" % (ch.get("residue"), ch.get("n_atoms", 0), ch.get("max_shift", 0.0))
    if t in ("add_atoms", "delete_atoms", "add_residue", "delete_residue"):
        return "%s %s (%d atoms)" % (t, ch.get("residue"), ch.get("n_atoms", 0))
    if t in ("occupancy", "bfactor"):
        return "%s %s (max delta %s)" % (t, ch.get("residue"), ch.get("max_delta"))
    return json.dumps(ch)


def _bsr_format_event(ev):
    t = str(ev.get("t", ""))[11:19]
    kind = ev.get("event")
    if kind == "session_start":
        return "%s  start     Bandicoot %s  cwd=%s" % (t, ev.get("program_version"), ev.get("cwd"))
    if kind == "session_end":
        return "%s  end       %s events, %s errors" % (t, ev.get("events"), ev.get("errors"))
    if kind == "molecule":
        return "%s  molecule  %s %s%s: %s" % (t, ev.get("kind"), ev.get("imol"),
                                             " (difference map)" if ev.get("difference") else "", ev.get("name"))
    if kind == "molecule_closed":
        return "%s  closed    %s %s: %s" % (t, ev.get("kind"), ev.get("imol"), ev.get("name"))
    if kind == "read_model":
        return "%s  read      model %s: %s" % (t, ev.get("imol"), ev.get("molecule"))
    if kind == "view":
        where = ev.get("residue", {}).get("spec") or ("xyz %s" % ev.get("xyz"))
        parts = []
        for m in ev.get("maps", []):
            if m.get("difference"):
                s = "diff %+.1fs" % m.get("sigma_at_centre", 0.0)
                pk = m.get("peak")
                if pk:
                    s += " (peak %s%d/%d, %+.1fs, %.1f A)" % (pk["sign"], pk["rank"], pk["of"], pk["sigma"], pk["dist"])
                parts.append(s)
            else:
                parts.append("map %.1fs" % m.get("sigma_at_centre", 0.0))
        tag = "" if ev.get("source") == "hook" else "  [%s]" % ev.get("source")
        return "%s  view      %-22s %s%s" % (t, where, "  ".join(parts), tag)
    if kind == "command":
        return "%s  command   %s" % (t, ev.get("text"))
    if kind == "key":
        return "%s  key       %s%s" % (t, "Ctrl+" if ev.get("ctrl") else "", ev.get("key"))
    if kind == "manipulation":
        return "%s  manip     %s on model %s" % (t, ev.get("mode"), ev.get("imol"))
    if kind == "backup":
        return "%s  backup    #%s %s" % (t, ev.get("history_index"), ev.get("file"))
    if kind == "edit":
        s = "; ".join(_bsr_format_change(ch) for ch in ev.get("changes", []))
        if ev.get("changes_truncated"):
            s += "; ... %d more" % ev["changes_truncated"]
        extra = []
        if ev.get("near"):
            extra.append("near " + ev["near"])
        if ev.get("history_index") is not None:
            h = ev["history_index"]
            extra.append("backup #%s" % (",".join(str(x) for x in h) if isinstance(h, list) else h))
        extra.append("via " + str(ev.get("trigger")))
        return "%s  edit      %s  [%s]" % (t, s, ", ".join(extra))
    if kind == "note":
        return "%s  note      %s" % (t, ev.get("text"))
    if kind == "peak_list":
        return "%s  peaks     map %s: %s positive, %s negative at %ss (%.2f s)" % (
            t, ev.get("imol_map"), ev.get("n_positive"), ev.get("n_negative"), ev.get("sigma"), ev.get("cost_s", 0.0))
    if kind in ("error", "warning"):
        return "%s  %-9s %s" % (t, kind, ev.get("text") or ev.get("where"))
    if kind == "stdout_capture":
        return "%s  stdout    copied to %s" % (t, ev.get("file"))
    if kind == "stdout":
        return "%s  stdout    %s" % (t, ev.get("text"))
    return "%s  %-9s %s" % (t, kind, json.dumps(ev, default=str))


def _bsr_read_events(path):
    events = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except ValueError:
                events.append({"event": "unparsable", "text": line[:200]})
    return events


def _bsr_summarize(path, show_all=False, out=None):
    out = out or sys.stdout
    events = _bsr_read_events(path)
    quiet = () if show_all else ("key", "manipulation", "peak_list", "backup", "stdout", "stdout_capture")
    lines = []
    for i, ev in enumerate(events):
        kind = ev.get("event")
        if kind in quiet or (kind == "command" and ev.get("noise") and not show_all):
            continue
        if kind == "view" and not show_all:
            # collapse a run of views: keep the last one within one second
            j = i + 1
            while j < len(events) and events[j].get("event") in quiet:
                j += 1
            if j < len(events) and events[j].get("event") == "view" \
                    and float(events[j].get("dt", 0)) - float(ev.get("dt", 0)) < 1.0:
                continue
        lines.append(_bsr_format_event(ev))
    out.write("\n".join(lines) + "\n")
    return len(lines)


# ---------------------------------------------------------------- command line: selftest

class _BsrFakeCoot:
    """Stand-in for the coot module: one model, one 2mFo-DFc map, one difference map."""

    def __init__(self):
        self.centre = [0.0, 0.0, 0.0]
        self.models = {0: ["/data/test/model.pdb", {}]}
        self.maps = {1: ("/data/test/maps.mtz FWT PHWT", False),
                     2: ("/data/test/maps.mtz DELFWT PHDELWT", True)}
        self.peaks = [(0.90, [10.0, 0.0, 0.0]), (-0.60, [20.0, 0.0, 0.0]), (0.50, [30.0, 0.0, 0.0])]

    # model building helpers for the test
    def add_residue(self, chain, resno, name, atoms, x0, ins=""):
        d = {}
        for i, (an, alt) in enumerate(atoms):
            d[(an, alt)] = [x0 + 0.5 * i, 0.0, 0.0, 1.0, 20.0]
        self.models[0][1][(chain, resno, ins)] = [name, d]

    def _sorted_keys(self, imol):
        return sorted(self.models[imol][1], key=lambda k: (k[0], k[1], k[2]))

    # coot API subset
    def coot_version(self):
        return "0.9.8.95-fake"

    def graphics_n_molecules(self):
        return 3

    def is_valid_model_molecule(self, i):
        return 1 if i in self.models else 0

    def is_valid_map_molecule(self, i):
        return 1 if i in self.maps else 0

    def molecule_name(self, i):
        if i in self.models:
            return self.models[i][0]
        return self.maps[i][0]

    def n_chains(self, imol):
        return len(set(k[0] for k in self.models[imol][1]))

    def chain_id_py(self, imol, ich):
        chains = sorted(set(k[0] for k in self.models[imol][1]))
        return chains[ich] if ich < len(chains) else False

    def chain_n_residues(self, chain, imol):
        return sum(1 for k in self.models[imol][1] if k[0] == chain)

    def _chain_keys(self, imol, chain):
        return [k for k in self._sorted_keys(imol) if k[0] == chain]

    def seqnum_from_serial_number(self, imol, chain, ser):
        return self._chain_keys(imol, chain)[ser][1]

    def insertion_code_from_serial_number(self, imol, chain, ser):
        return self._chain_keys(imol, chain)[ser][2]

    def residue_info_py(self, imol, chain, resno, ins):
        r = self.models[imol][1].get((chain, resno, ins))
        if r is None:
            return False
        return [[[an, alt], [v[3], v[4], "C", ""], [v[0], v[1], v[2]]] for (an, alt), v in r[1].items()]

    def residue_name_py(self, imol, chain, resno, ins):
        r = self.models[imol][1].get((chain, resno, ins))
        return r[0] if r else False

    def molecule_to_pdb_string_py(self, imol):
        lines = []
        serial = 0
        for (chain, resno, ins), (rname, atoms) in sorted(self.models[imol][1].items()):
            for (an, alt), v in atoms.items():
                serial += 1
                rec = "HETATM" if rname == "HOH" else "ATOM  "
                lines.append("%s%5d %-4s%1s%3s %1s%4d%1s   %8.3f%8.3f%8.3f%6.2f%6.2f           C" % (
                    rec, serial, (" " + an) if len(an) < 4 else an, alt or " ", rname, chain, resno, ins or " ",
                    v[0], v[1], v[2], v[3], v[4]))
        return "\n".join(lines) + "\nEND\n"

    def rotation_centre_position(self, i):
        return self.centre[i]

    def zoom_factor(self):
        return 100.0

    def active_residue_py(self):
        best = None
        for k, (name, atoms) in self.models[0][1].items():
            for (an, alt), v in atoms.items():
                d = _bsr_dist(v[:3], self.centre)
                if best is None or d < best[0]:
                    best = (d, [0, k[0], k[1], k[2], an, alt])
        return best[1] if best else False

    def map_sigma_py(self, imol):
        return 0.30 if imol == 1 else 0.15

    def density_at_point(self, imol, x, y, z):
        if imol == 1:
            return 0.40
        for h, p in self.peaks:
            if _bsr_dist(p, (x, y, z)) < 1.0:
                return h
        return 0.05

    def map_is_difference_map(self, imol):
        return 1 if self.maps[imol][1] else 0

    def get_contour_level_in_sigma(self, imol):
        return 1.5 if imol == 1 else 3.0

    def map_peaks_around_molecule_py(self, imol_map, sigma, negative_also, imol_model):
        return [[h, list(p)] for h, p in self.peaks if h > 0 or negative_also]

    def imol_refinement_map(self):
        return 1

    def bandicoot_python_timeout_add(self, ms, cb):
        return 0


def _bsr_selftest():
    import tempfile
    import types

    tmp = tempfile.mkdtemp(prefix="bandicoot-session-selftest-")
    backup_dir = os.path.join(tmp, "coot-backup")
    os.makedirs(backup_dir)
    os.environ["COOT_BACKUP_DIR"] = backup_dir
    log_path = os.path.join(tmp, "session.jsonl")

    fake = _BsrFakeCoot()
    ns = types.SimpleNamespace()
    key_calls = []

    def original_key_hook(key, ctrl=0):
        key_calls.append(key)

    ns.graphics_general_key_press_hook = original_key_hook
    ns.post_manipulation_script = False

    rec = _BsrRecorder(fake, ns, path=log_path, capture_stdout=True, rank_peaks=True, use_timer=False)
    rec.backup_scan_s = 0.0
    rec.periodic_s = 0.0
    rec.start()
    assert ns.post_set_rotation_centre_script is rec._hooks["post_set_rotation_centre_script"]
    assert ns.graphics_general_key_press_hook is rec._hooks["graphics_general_key_press_hook"]

    # model with five residues; A/3 SER gets an alt conf later
    for resno, name in enumerate(["ALA", "ALA", "SER", "LEU", "GLY"], start=1):
        atoms = [("N", ""), ("CA", ""), ("C", ""), ("O", "")]
        if name == "SER":
            atoms += [("CB", ""), ("OG", "")]
        fake.add_residue("A", resno, name, atoms, 3.8 * resno)
    ns.post_read_model_hook(0)

    # recentre on the strongest positive difference peak (Space-key style: hook only)
    fake.centre = [10.0, 0.0, 0.0]
    ns.post_set_rotation_centre_script()

    # a GUI command echoed by Coot (bold + colour escapes) and a backup notice, then the edit
    bk1 = os.path.join(backup_dir, "_data_test_model.pdb_Wed_Sep_3_12:00:00_2026_modification_0.pdb.gz")
    open(bk1, "w").close()
    os.write(1, b"\x1b[1m\x1b[34mplace_typed_atom_at_pointer (\"Water\")\x1b[0m\n")
    os.write(1, ("INFO:: backup file name " + bk1 + "\n").encode())
    fake.add_residue("A", 301, "HOH", [("O", "")], 10.2)
    time.sleep(0.3)
    rec._tick()

    # alt conf B on A/3, backup seen only via the directory scan
    bk2 = os.path.join(backup_dir, "_data_test_model.pdb_Wed_Sep_3_12:00:01_2026_modification_1.pdb.gz")
    open(bk2, "w").close()
    ser = fake.models[0][1][("A", 3, "")]
    for (an, alt), v in list(ser[1].items()):
        if an in ("CB", "OG"):
            ser[1][(an, "A")] = list(v)
            ser[1][(an, "B")] = [v[0] + 1.0, v[1] + 1.0, v[2], 0.5, v[4]]
            del ser[1][(an, alt)]
    rec._tick()

    # mutate A/2 ALA -> LEU via the manipulation hook
    fake.models[0][1][("A", 2, "")][0] = "LEU"
    fake.models[0][1][("A", 2, "")][1][("CB", "")] = [8.0, 0.0, 0.0, 1.0, 20.0]
    ns.post_manipulation_script(0, 3)

    # delete the water via the manipulation hook
    del fake.models[0][1][("A", 301, "")]
    ns.post_manipulation_script(0, 2)

    # real-space shift on A/4 with no backup and no hook: caught by the periodic diff
    for v in fake.models[0][1][("A", 4, "")][1].values():
        v[1] += 0.5
    rec._last_periodic = 0.0
    rec._snapshot_cost = 0.0
    rec._tick()

    # a key binding, a note, a drag-pan caught by polling
    ns.graphics_general_key_press_hook(ord("w"), 0)
    rec.note("selftest note")
    fake.centre = [10.6, 0.0, 0.0]
    rec._tick()

    # an echoed call the recorder makes itself must not become a command event
    os.write(1, b"\x1b[1m\x1b[34mrotation_centre_position (0)\x1b[0m\n")
    time.sleep(0.3)
    rec.stop()
    assert os.path.exists(rec.stdout_path) and os.path.getsize(rec.stdout_path) > 0, "sidecar missing"
    assert ns.graphics_general_key_press_hook is original_key_hook
    assert ns.post_manipulation_script is False
    assert key_calls == [ord("w")], key_calls

    events = _bsr_read_events(log_path)
    kinds = [e["event"] for e in events]
    edits = [e for e in events if e["event"] == "edit"]
    edit_types = sorted(set(t for e in edits for t in e["summary"]))
    cmds = [e for e in events if e["event"] == "command"]
    views = [e for e in events if e["event"] == "view"]

    def check(cond, msg):
        if not cond:
            raise AssertionError(msg)

    check(kinds[0] == "session_start" and kinds[-1] == "session_end", "start/end missing: %s" % kinds)
    check("read_model" in kinds and "molecule" in kinds, "molecule events missing")
    check(len(cmds) == 1 and cmds[0]["name"] == "place_typed_atom_at_pointer"
          and cmds[0]["args"] == '"Water"', "command capture failed: %s" % [(c["name"], c["args"]) for c in cmds])
    check(views and views[0]["source"] == "hook" and views[0]["residue"]["spec"] == "A/2 ALA",
          "first view wrong: %s" % views[:1])
    diff_maps = [m for m in views[0]["maps"] if m["difference"]]
    check(diff_maps and diff_maps[0]["peak"]["rank"] == 1 and diff_maps[0]["peak"]["sign"] == "+"
          and diff_maps[0]["peak"]["of"] == 2, "peak rank wrong: %s" % diff_maps)
    check(views[-1]["source"] == "poll", "drag-pan poll view missing: %s" % views[-1])
    for want in ("add_water", "add_altloc", "mutate", "delete_water", "move"):
        check(want in edit_types, "edit type %s missing; got %s" % (want, edit_types))
    by_trigger = sorted(set(e["trigger"] for e in edits))
    for want in ("backup", "hook", "periodic"):
        check(want in by_trigger, "trigger %s missing; got %s" % (want, by_trigger))
    water_edit = [e for e in edits if "add_water" in e["summary"]][0]
    check(water_edit.get("history_index") == 0 and water_edit["near"].startswith("A/"),
          "water edit context wrong: %s" % water_edit)
    check(any(e["event"] == "key" and e["key"] == "w" for e in events), "key event missing")
    check(any(e["event"] == "note" for e in events), "note missing")
    check(not any(e["event"] == "error" for e in events),
          "errors logged: %s" % [e for e in events if e["event"] == "error"])

    print("selftest: %d events, edit types %s" % (len(events), edit_types))
    print("selftest: summary of %s" % log_path)
    _bsr_summarize(log_path, show_all=False)
    print("selftest OK")
    return 0


def _bsr_cli(argv):
    usage = ("usage:\n"
             "  python3 bandicoot_session_recorder.py summarize <session.jsonl> [--all]\n"
             "  python3 bandicoot_session_recorder.py selftest\n")
    if not argv or argv[0] in ("-h", "--help"):
        sys.stdout.write(usage)
        return 0
    if argv[0] == "summarize" and len(argv) >= 2:
        _bsr_summarize(argv[1], show_all="--all" in argv[2:])
        return 0
    if argv[0] == "selftest":
        return _bsr_selftest()
    sys.stdout.write(usage)
    return 2


# ---------------------------------------------------------------- module load

if _bsr_coot_module is not None:
    # Inside Bandicoot. coot_load_modules exec()s this file in __main__, so the
    # public functions are already there; if imported as a module instead,
    # publish them to __main__ so the scripting console and C hooks find them.
    for _bsr_name in ("start_session_recording", "stop_session_recording",
                      "session_recording_status", "session_note"):
        if not hasattr(_bsr_main, _bsr_name):
            setattr(_bsr_main, _bsr_name, globals()[_bsr_name])
    if _bsr_env_flag("BANDICOOT_RECORD", False):
        try:
            start_session_recording()
        except Exception:
            traceback.print_exc()
elif __name__ == "__main__":
    sys.exit(_bsr_cli(sys.argv[1:]))
