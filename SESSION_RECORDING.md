# Session recording

Bandicoot can write a log of what you do during a modelling session: where you
looked, what the maps showed there, every command Coot echoes, and how the model
changed. The log is one JSON object per line (JSON Lines), so it can be read by
a script or turned into a plain-text summary. Nothing is recorded unless you
turn it on.

The recorder is `python/bandicoot_session_recorder.py`. It needs no rebuild
step beyond the normal one and no changes to Coot's C++.

## Start recording

Any one of these:

| How | What to do |
|-----|------------|
| For one session | Launch from a terminal: `BANDICOOT_RECORD=1 bcoot` |
| From inside Bandicoot | In the scripting console: `start_session_recording()` |
| Always on | Create `~/.coot-preferences/record_sessions.py` containing the single line `start_session_recording()` |

The console prints the log path when recording starts, for example
`INFO:: session recording -> /Users/you/bandicoot-sessions/bandicoot-session-20260903-121500-4242.jsonl`.

Other console commands:

```python
session_note("blob near A/45 looks like glycerol")   # add a free-text note
session_recording_status()                           # path, event count, what is being tracked
stop_session_recording()                             # finish the file
```

Recording also stops when Bandicoot exits. Every event is flushed to disk as
it happens, so a crash loses nothing except the final `session_end` line.

## Read a log

The summary needs only Python 3, not Bandicoot:

```
python3 <bandicoot>/lib/python3.13/site-packages/coot/bandicoot_session_recorder.py summarize ~/bandicoot-sessions/<file>.jsonl
```

Add `--all` to see every event (key presses, backups, peak lists, rapid view
changes are hidden by default). Example output:

```
12:15:03  start     Bandicoot 0.9.8.95  cwd=/Users/you/refine
12:15:09  molecule  model 0: /Users/you/refine/model.pdb
12:15:09  molecule  map 1: /Users/you/refine/maps.mtz FWT PHWT
12:15:09  molecule  map 2 (difference map): /Users/you/refine/maps.mtz DELFWT PHDELWT
12:16:41  view      A/45 SER               map 1.3s  diff +5.1s (peak +1/12, +5.2s, 0.4 A)
12:16:55  command   place_typed_atom_at_pointer ("Water")
12:16:55  edit      add_water x1 (A/301)  [near A/45 SER, backup #7, via backup]
12:17:20  view      A/45 SER               map 1.1s  diff +3.4s (peak +2/12, +3.6s, 1.1 A)
12:17:48  command   add_alt_conf (0, "A", 45, "", "", 1)
12:17:52  edit      add_altloc B on A/45 SER (6 atoms)  [near A/45 SER, backup #8, via backup]
12:18:30  view      A/112 GLU              map 0.9s  diff -3.8s (peak -1/12, -3.9s, 0.6 A)
```

`s` means sigma. The peak entry reads: sign, rank among peaks of that sign,
number of peaks of that sign, peak height, distance from the screen centre to
the peak.

## What is recorded

Three sources, merged by timestamp into one file.

**Coot's command echo.** Every GUI action that goes through a scripting-API
function is printed to the terminal by Coot itself (`add_to_history` in
`src/c-interface-info.cc`; these are the same lines Coot writes to
`0-coot-history.py` when it exits). The recorder points the process's stdout
at a child `tee` process, which forwards every byte to the terminal unchanged
and appends a copy to `<session>.stdout.log` next to the JSON file. The
recorder reads that copy and logs the echoed lines as `command` events.
Examples: `set_rotation_centre`, `refine_zone`, `add_alt_conf`,
`place_typed_atom_at_pointer`, `set_contour_level_in_sigma`, `delete_residue`.
The full terminal output of the session is therefore also kept.

**Hooks Coot 0.9 already provides** (`src/graphics-info.cc`):

- `post_set_rotation_centre_script` runs after every recentre, including Space
  and Shift-Space, middle-click, Go To Atom, and clicks in the Difference Map
  Peaks dialog. The recorder logs a `view` event: rotation centre, zoom, the
  nearest residue, and for each open map the density at the centre in sigma
  units. For difference maps it also reports which peak you are on (see
  below).
- `post_manipulation_script` runs after a moving-atoms accept, a delete, or a
  mutation.
- `post_read_model_hook` runs after a model is read.
- `graphics_general_key_press_hook` runs for keys that are not handled in C,
  which is how the model-building key bindings in `KEY_SHORTCUTS.md` work.

**Model snapshots.** Coot writes a backup file before every model edit
(`make_backup` in `src/molecule-class-info.cc`, into `coot-backup/` or
`$COOT_BACKUP_DIR`). A new backup file, a manipulation hook, or a periodic
timer (every 60 s, longer for very large models) makes the recorder take a
snapshot (one call, `molecule_to_pdb_string`, parsed in Python), compare it
with the previous one residue by residue, and write an `edit` event. Edit
types:

| Type | Meaning |
|------|---------|
| `add_water`, `delete_water` | HOH residues added or removed (grouped, with the residue list) |
| `add_residue`, `delete_residue` | any other residue added or removed |
| `add_altloc`, `delete_altloc` | alternate-conformation letters appeared or vanished on a residue |
| `mutate` | residue name changed |
| `move` | atoms moved (count and largest shift in angstroms) |
| `add_atoms`, `delete_atoms` | atom count changed without an altloc change (side chain, hydrogens) |
| `occupancy`, `bfactor` | occupancy or B factor changed |

A main-loop timer (500 ms) also polls the nearest residue, so drag-panning,
which fires no hook, produces `view` events tagged `[poll]` whenever the
nearest atom changes.

## Difference-map peak rank

"Went to the largest positive difference peak" is not something Coot stores.
The recorder infers it: for each difference map it computes the peak list at
3 sigma around the model (`map_peaks_around_molecule`), sorts positive and
negative peaks separately by height, and at each `view` reports the nearest
peak within 2.5 angstroms of the screen centre with its rank. The list is
cached for five minutes per map, and the `list_age_s` field says how old it
was. This is the recorder's own ranking at 3 sigma, not the list in the
Difference Map Peaks dialog, which uses whatever sigma you chose there.

Peak search costs a fraction of a second per map, the first time a map is
looked at and again every five minutes. Set `BANDICOOT_RECORD_PEAKS=0` to
turn ranking off.

## Files and settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `BANDICOOT_RECORD` | unset | `1` starts recording at launch |
| `BANDICOOT_SESSION_DIR` | `~/bandicoot-sessions` | where log files go |
| `BANDICOOT_RECORD_STDOUT` | `1` | `0` disables the stdout tee (no `command` events) |
| `BANDICOOT_RECORD_PEAKS` | `1` | `0` disables difference-peak ranking |
| `BANDICOOT_RECORD_ALL_STDOUT` | `0` | `1` stores every terminal line as a `stdout` event (large files) |

Log file names: `bandicoot-session-<YYYYMMDD-HHMMSS>-<pid>.jsonl`, with the
terminal copy in `bandicoot-session-<YYYYMMDD-HHMMSS>-<pid>.stdout.log`.

## Event schema

Every line has `t` (local time, ISO 8601 with offset), `dt` (seconds since the
session started), `seq` (running number), and `event`. Other fields by event:

| event | fields |
|-------|--------|
| `session_start` | `recorder_version`, `program_version`, `pid`, `argv`, `cwd`, `python`, `backup_dirs`, `options` |
| `stdout_capture` | `file` (the `.stdout.log` sidecar) |
| `molecule` | `imol`, `kind` (`model`/`map`), `name`, `difference`, `replaced` |
| `molecule_closed` | `imol`, `kind`, `name` |
| `read_model` | `imol`, `molecule` |
| `view` | `source` (`hook`/`poll`), `xyz`, `zoom`, `residue` {`imol`, `chain`, `resno`, `ins`, `atom`, `alt`, `name`, `spec`}, `maps` [{`imol`, `name`, `difference`, `sigma_at_centre`, `contour_sigma`, `peak` {`sign`, `rank`, `of`, `sigma`, `dist`, `search_sigma`, `list_age_s`}}] |
| `command` | `text`, `name` (Python-style function name), `args` (raw argument text), `syntax` (`python`/`scheme`) |
| `key` | `key`, `keyval`, `ctrl` |
| `manipulation` | `imol`, `mode` (`MOVINGATOMS`/`DELETED`/`MUTATED`), `molecule` |
| `backup` | `file`, `history_index`, `imols` |
| `edit` | `imol`, `molecule`, `trigger` (`backup`/`hook`/`periodic`/`stop`), `summary` {type: count}, `changes` [...], `near`, `backup_files`, `history_index` (a list when one event covers several backups), `mode` |
| `peak_list` | `imol_map`, `imol_model`, `sigma`, `n_positive`, `n_negative`, `cost_s` |
| `note` | `text` |
| `warning`, `error` | `text` or `where`, `traceback` |
| `session_end` | `events`, `errors` |

Residue specs are written `chain/resno[ins] NAME`, for example `A/45 SER`.

## Caveats

- The `command` stream covers only actions that Coot routes through a
  scripting-API function. Coot's own manual calls the history "incomplete".
  Navigation by Space, Shift-Space and drag-panning is caught by the hook and
  the poll instead, and model edits are caught by the snapshot diff whether or
  not a command was echoed.
- The reason for a move is inferred (peak rank at 3 sigma), not stored by
  Coot. Treat `peak` as a hint, not a record of what the user saw.
- Edits are detected after the fact by diffing residues. Two edits inside one
  500 ms tick appear as one `edit` event listing both backups. Without
  graphics there is no timer, so edits are grouped up to the next hook call. Backups must be on (they are by
  default) for edits to be timestamped precisely; with backups off, edits are
  still caught by the manipulation hook or the periodic diff.
- The stdout capture changes where the process's file descriptor 1 points.
  Programs started by Bandicoot inherit it and their output is forwarded and
  copied too. `<session>.stdout.log` grows with everything Coot prints; a long
  session can reach tens of megabytes.
- A snapshot is one `molecule_to_pdb_string` call plus a parse; a diff takes
  a few milliseconds for a typical protein. The periodic diff backs off
  automatically for large models.
- Coot 0.9 adds most scripting calls to its history, including calls made by
  scripts. The recorder uses the silent ones (`active_residue`,
  `residue_info`, `map_sigma`, `density_at_point`, `map_peaks_around_molecule`,
  `molecule_to_pdb_string`, `zoom_factor`). Two it cannot avoid are echoed:
  `rotation_centre_position` (three calls per `view`) and `molecule_name`
  (once per molecule). They are filtered out of the `command` stream, but they
  do appear in `0-coot-history.py`.
- The stdout capture starts a `tee` child process; if `tee` is not on the
  PATH, recording continues without `command` events and logs a warning.
- File paths of models and maps are recorded. Nothing else leaves the machine;
  the log is a local file.

## Self-test

The module can test itself without Bandicoot, using a fake `coot` module:

```
python3 python/bandicoot_session_recorder.py selftest
```

It exercises the stdout capture (a real `tee` child), the hooks, backup
detection, the snapshot diff (water, alt conf, mutation, deletion, move),
polling, notes, and the summary.

Headless runs work too and are a quick way to check a build:

```
BANDICOOT_RECORD=1 bcoot --no-graphics --no-state-script --script my_script.py
```

There is no main-loop timer without graphics, so drag-pan polling is off and
backup notices are processed at the next hook call or at stop.
