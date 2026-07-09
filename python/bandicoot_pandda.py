# Bandicoot PanDDA-inspect addon -- minimal event browser.
#
# Adapted from inspect_pandda_analyse.py (Tobias Krojer, MAX IV Laboratory, MIT
# licensed). The pygtk UI is removed; the logic (parse pandda.analyse results,
# navigate events, load model + event/z maps, recentre) is driven from a native
# Bandicoot C panel, or the scripting window for testing.
#
# MAP ALIGNMENT.  Two independent ways the event map gets onto the model:
#   * Newer pandda data has an event-map MTZ -> auto_read_make_and_draw_maps_from_mtz
#     (FFT map fills the cell, lands on the model). Preferred when present.
#   * PanDDA-v1 (CCP4 v7+) writes only native .ccp4 boxes, and Coot 0.9 has a
#     regression: it does NOT periodically tile a CCP4 map, and it misreads the
#     non-standard Z-fast/Y/X axis order pandda uses -- so the box lands on a
#     symmetry/cell mate, not the model. Fix per a coworker's recipe (ART.md):
#     read the .ccp4 ourselves, permute axes to standard X,Y,Z, circularly roll
#     the grid so the box is centred on the event, rewrite NSTART + zero ORIGIN,
#     write a temp .ccp4, and hand THAT to read_ccp4_map. See _fix_ccp4_map().
#
# Drive from the scripting window:
#     import bandicoot_pandda as bp
#     print(bp.set_folder("/path/to/pandda")); bp.next_event(); bp.prev_event()

import os
import glob
import csv
import struct
import tempfile

import coot


def _fix_ccp4_map(path, centre):
    """Rewrite a native pandda .ccp4 so Coot 0.9 draws it on the model:
    standard axis order + grid rolled to centre the box on `centre` (the event
    xyz, in Angstrom). Returns a temp .ccp4 path, or None to use the original."""
    try:
        import numpy as np
    except Exception:
        return None
    try:
        with open(path, "rb") as f:
            hdr = bytearray(f.read(1024))
            NC, NR, NS = struct.unpack_from("<3i", hdr, 0)
            mode, = struct.unpack_from("<i", hdr, 12)
            NX, NY, NZ = struct.unpack_from("<3i", hdr, 28)
            cell = struct.unpack_from("<6f", hdr, 40)
            MAPC, MAPR, MAPS = struct.unpack_from("<3i", hdr, 64)
            NSYMBT, = struct.unpack_from("<i", hdr, 92)
            if mode != 2:                      # only float32 maps
                return None
            symdata = f.read(NSYMBT)
            data = np.frombuffer(f.read(NC * NR * NS * 4), dtype="<f4")
        data = data.reshape(NS, NR, NC)        # file order: sections, rows, cols
        # permute file axes (S,R,C) -> crystal (X,Y,Z)
        file_axis_crystal = [MAPS, MAPR, MAPC]                 # for file axes 0,1,2
        order = [file_axis_crystal.index(k) for k in (1, 2, 3)]
        A = np.transpose(data, order)          # A[iX, iY, iZ], shape (NX, NY, NZ)
        # roll each axis so the box is centred on the event
        nstart = []
        for e, L, N, ax in ((centre[0], cell[0], NX, 0),
                            (centre[1], cell[1], NY, 1),
                            (centre[2], cell[2], NZ, 2)):
            g = (e / L) * N if L else 0.0       # event position in grid units
            ns = int(round(g)) - N // 2
            A = np.roll(A, -ns, axis=ax)
            nstart.append(ns)
        nsx, nsy, nsz = nstart
        # write standard-order map: O[iZ, iY, iX] = A[iX, iY, iZ]
        O = np.ascontiguousarray(np.transpose(A, (2, 1, 0)), dtype="<f4")
        dmin, dmax, dmean = float(O.min()), float(O.max()), float(O.mean())
        struct.pack_into("<3i", hdr, 0, NX, NY, NZ)      # NC,NR,NS
        struct.pack_into("<3i", hdr, 16, nsx, nsy, nsz)  # NCSTART,NRSTART,NSSTART
        struct.pack_into("<3i", hdr, 28, NX, NY, NZ)     # NX,NY,NZ
        struct.pack_into("<3i", hdr, 64, 1, 2, 3)        # MAPC,MAPR,MAPS -> standard
        struct.pack_into("<3f", hdr, 76, dmin, dmax, dmean)
        struct.pack_into("<3f", hdr, 196, 0.0, 0.0, 0.0) # zero MRC2014 ORIGIN
        fd, tmp = tempfile.mkstemp(suffix=".ccp4", prefix="bcoot_pandda_")
        with os.fdopen(fd, "wb") as f:
            f.write(bytes(hdr))
            f.write(symdata)
            f.write(O.tobytes())
        return tmp
    except Exception as e:
        print("PanDDA: _fix_ccp4_map failed (%s); using raw map" % e)
        return None


# Standard residue / solvent codes; anything else in a HETATM record is treated
# as a modelled ligand when splitting a fitted model (from the shim).
_STANDARD_RESIDUES = frozenset([
    "ALA", "ARG", "ASN", "ASP", "CYS", "GLN", "GLU", "GLY", "HIS", "ILE", "LEU",
    "LYS", "MET", "PHE", "PRO", "SER", "THR", "TRP", "TYR", "VAL", "MSE",
    "HOH", "WAT", "SO4", "PO4", "GOL", "EDO", "PEG", "MPD", "NA", "CL", "MG", "ZN",
])


def _rename_pdb_residue(src_path, new_code):
    """Temp PDB with residue name LIG replaced by new_code (cols 18-20). The
    original file is never touched. Returns the temp path (or src on failure)."""
    try:
        with open(src_path) as f:
            lines = f.readlines()
        out = []
        for ln in lines:
            if ln[:6].rstrip() in ("ATOM", "HETATM", "ANISOU", "TER") \
               and len(ln) > 20 and ln[17:20] == "LIG":
                ln = ln[:17] + new_code + ln[20:]
            out.append(ln)
        fd, tmp = tempfile.mkstemp(suffix="_" + new_code + ".pdb")
        with os.fdopen(fd, "w") as f:
            f.writelines(out)
        return tmp
    except Exception:
        return src_path


def _rename_cif_residue(src_path, new_code):
    """Temp CIF with LIG replaced by new_code (LIG only appears as the compound
    id). Returns the temp path (or src on failure)."""
    try:
        with open(src_path) as f:
            content = f.read()
        fd, tmp = tempfile.mkstemp(suffix="_" + new_code + ".cif")
        with os.fdopen(fd, "w") as f:
            f.write(content.replace("LIG", new_code))
        return tmp
    except Exception:
        return src_path


def _acedrg_build(smiles, tlc):
    """Build a 3D ligand (.pdb + .cif) from a SMILES string by running acedrg
    directly. Returns (pdb_path, cif_path, error_message); error_message is
    None on success, and a human-readable string (no 'PanDDA:' prefix) on
    failure.

    Coot's own new_molecule_by_smiles_string goes through acedrg_env(), which
    does os.environ['CBIN'] (KeyError when CCP4 vars aren't exported) and uses
    a ';' PATH separator -- so we bypass it. The SBGrid acedrg wrapper sets up
    its own CCP4 environment; we only need it on PATH. Shared by the PanDDA
    ligand loader and the standalone ligand_from_smiles_standalone()."""
    import subprocess
    smiles = (smiles or "").strip()
    tlc = ((tlc or "").strip() or "LIG")[:3]
    if not smiles:
        return (None, None, "empty SMILES string")
    try:
        from shutil import which
    except Exception:
        which = None
    acedrg = (which("acedrg") if which else None)
    if not acedrg:
        return (None, None, "acedrg not on PATH "
                "(launch Bandicoot from a shell with SBGrid/CCP4 active)")
    workdir = tempfile.mkdtemp(prefix="bcoot_acedrg_")
    smi = os.path.join(workdir, "ligand.smi")
    with open(smi, "w") as f:
        f.write(smiles + "\n")
    stub = os.path.join(workdir, tlc)
    try:
        proc = subprocess.run([acedrg, "-i", smi, "-r", tlc, "-o", stub],
                              cwd=workdir, stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except Exception as e:
        return (None, None, "could not run acedrg (%s)" % e)
    pdb, cif = stub + ".pdb", stub + ".cif"
    if proc.returncode != 0 or not os.path.isfile(pdb):
        if proc.stdout:
            print("acedrg output (tail):\n"
                  + proc.stdout.decode("utf-8", "replace")[-500:])
        return (None, None, "acedrg could not build %s from that SMILES "
                "(see terminal)" % tlc)
    return (pdb, cif, None)


class PanddaInspect(object):

    _ALIASES = {
        "dtag": "dtag",
        "event_idx": "event", "event_num": "event",
        "site_idx": "site",   "site_num": "site",
        "1-BDC": "bdc",       "bdc": "bdc",
        "x": "x", "y": "y", "z": "z",
        "high_resolution": "resolution", "analysed_resolution": "resolution",
        "r_free": "r_free", "r_work": "r_work",
        "cluster_size": "cluster_size",
        "z_peak": "z_peak", "map_uncertainty": "map_uncertainty",
    }

    # pandda_inspect_events.csv annotation columns (appended to the analyse CSV),
    # matching pandda.inspect so the file round-trips with that tool.
    _ANNOT = [("Interesting",       "interesting", "False"),
              ("Ligand Placed",     "placed",      "False"),
              ("Ligand Confidence", "confidence",  "unassigned"),
              ("Comment",           "comment",     "None"),
              ("Viewed",            "viewed",      "False")]
    CONFIDENCE_VALUES = ["unassigned", "no ligand bound", "unknown ligand",
                         "low confidence", "high confidence"]

    def __init__(self):
        self._view_setup_done = False
        # map-visibility preferences persist across events and folders so the
        # panel checkboxes keep their meaning while navigating.
        self.show_emap = True
        self.show_zmap = True
        self.show_xray = False
        self.show_average = False
        self.reset_all()

    def reset_all(self):
        self.panddaDir = None
        self.analysis_folder = None
        self.eventCSV = None
        self.elist = []
        self.col = {}
        self.ci = {}                  # annotation-column name -> index
        self.order = []               # elist row-indices in the current view order
        self.pos = 0                  # position within self.order
        self.index = 0                # = self.order[self.pos] (current elist row)
        self._csv_backed_up = False
        # site annotations (pandda_inspect_sites.csv): Name/Comment per site_idx
        self.sitesCSV = None
        self.slist = []               # rows of the sites CSV (incl. header)
        self.scol = {}                # sites-CSV column name -> index
        self._sites_backed_up = False
        # ligand auto-loader config (from ~/.coot-preferences/bandicoot-ligands)
        self._lig_index = {}          # dtag -> ligand_id
        self._lig_cifs_dir = None     # root of per-ligand isomer directories
        self._split_on_load = False   # split fitted models into protein+ligand mols
        self.reset_event()

    def reset_event(self):
        self.xtal = self.event = self.site = self.bdc = None
        self.x = self.y = self.z = None
        self.resolution = self.r_free = self.r_work = None
        self.z_peak = self.cluster_size = self.map_uncertainty = None
        self.pdb = self.emap = self.zmap = self.ligcif = None
        self.merged = False
        self._lig_cycle = -1          # index into the open_next_ligand candidate list
        self._lig_mols = []           # all shim-loaded/split ligand molecules
        self.mol = {"protein": None, "emap": None, "zmap": None, "ligand": None,
                    "xray": None, "average": None, "compare": None}

    def _ensure_view_setup(self):
        if not self._view_setup_done:
            coot.set_show_symmetry_master(1)
            self._view_setup_done = True

    # ---- folder selection + CSV parsing ----------------------------------
    def set_folder(self, path):
        # A new (or failed) folder selection starts clean: any error below
        # leaves no dataset loaded, so the panel keeps nav disabled instead of
        # navigating the previous dataset against the wrong directory.
        self.reset_all()
        if not path or not os.path.isdir(path):
            return self._msg("PanDDA: not a folder: %s" % path)
        self.panddaDir = os.path.realpath(path)
        self._load_ligand_config()    # needs panddaDir for the project-relative index
        self.analysis_folder = None
        for sub in ("analyses", "results", "analysis"):
            cand = os.path.join(self.panddaDir, sub)
            if os.path.isdir(cand):
                self.analysis_folder = cand
                break
        if not self.analysis_folder:
            return self._msg("PanDDA: no analyses/ folder under %s" % path)

        # We READ/WRITE pandda_inspect_events.csv (the working copy, with our
        # annotation columns); if it doesn't exist yet, seed it from the read-only
        # pandda_analyse_events.csv. The analyse CSV is never modified.
        self.eventCSV = os.path.join(self.analysis_folder, "pandda_inspect_events.csv")
        if os.path.isfile(self.eventCSV):
            src = self.eventCSV
        else:
            src = os.path.join(self.analysis_folder, "pandda_analyse_events.csv")
            if not os.path.isfile(src):
                return self._msg("PanDDA: no event CSV in %s" % self.analysis_folder)

        with open(src) as f:
            self.elist = list(csv.reader(f))
        if len(self.elist) < 2:
            return self._msg("PanDDA: event CSV has no events")
        self.col = {}
        for n, name in enumerate(self.elist[0]):
            if name in self._ALIASES:
                self.col[self._ALIASES[name]] = n
        for need in ("dtag", "event", "x", "y", "z"):
            if need not in self.col:
                return self._msg("PanDDA: event CSV missing '%s' column" % need)

        self._ensure_annotation_columns()
        self._csv_backed_up = False
        if src != self.eventCSV:          # just seeded -> write the inspect copy
            self._save_csv()

        self._load_sites()
        self.order = list(range(1, len(self.elist)))   # all events, file order
        self.pos = 0
        self._ensure_view_setup()
        return self._load_pos()

    # ---- site annotations (pandda_inspect_sites.csv) ---------------------
    def _load_sites(self):
        """Parse pandda_inspect_sites.csv (the working copy with Name/Comment),
        seeding it from pandda_analyse_sites.csv if absent. Best-effort: site
        annotation is optional, so any failure just leaves it disabled."""
        self.sitesCSV = self.slist = None
        self.slist = []
        self.scol = {}
        self._sites_backed_up = False
        if not self.analysis_folder:
            return
        self.sitesCSV = os.path.join(self.analysis_folder, "pandda_inspect_sites.csv")
        src = self.sitesCSV
        if not os.path.isfile(src):
            src = os.path.join(self.analysis_folder, "pandda_analyse_sites.csv")
            if not os.path.isfile(src):
                self.sitesCSV = None
                return
        try:
            with open(src) as f:
                self.slist = list(csv.reader(f))
        except Exception:
            self.sitesCSV = None
            self.slist = []
            return
        if len(self.slist) < 1:
            self.sitesCSV = None
            return
        header = self.slist[0]
        for name in ("Name", "Comment"):
            if name not in header:
                header.append(name)
                for row in self.slist[1:]:
                    row.append("None")
        for n, name in enumerate(header):
            self.scol[name] = n
        if src != self.sitesCSV:          # just seeded -> write the inspect copy
            self._save_sites()

    def _save_sites(self):
        if not self.sitesCSV:
            return
        try:
            if not self._sites_backed_up and os.path.isfile(self.sitesCSV):
                import shutil
                bak = self.sitesCSV + ".bcoot-bak"
                if not os.path.isfile(bak):
                    shutil.copyfile(self.sitesCSV, bak)
            self._sites_backed_up = True
            with open(self.sitesCSV, "w", newline="") as f:
                csv.writer(f).writerows(self.slist)
        except Exception as e:
            print("PanDDA: sites CSV save failed (%s)" % e)

    def _site_row(self):
        """The current site's row in self.slist, or None."""
        if not self.slist or "site_idx" not in self.scol or self.site is None:
            return None
        k = self.scol["site_idx"]
        for row in self.slist[1:]:
            if k < len(row) and row[k] == str(self.site):
                return row
        return None

    def _site_get(self, name, default="None"):
        row = self._site_row()
        n = self.scol.get(name)
        if row is None or n is None or n >= len(row):
            return default
        return row[n] or default

    def _site_set(self, name, value):
        row = self._site_row()
        n = self.scol.get(name)
        if row is None or n is None:
            return
        while len(row) <= n:
            row.append("")
        row[n] = value
        self._save_sites()

    def get_site_name(self):    return self._site_get("Name")
    def set_site_name(self, s):
        s = (s or "").strip()
        self._site_set("Name", s if s else "None"); return self.status()
    def get_site_comment(self): return self._site_get("Comment")
    def set_site_comment(self, s):
        s = (s or "").strip()
        self._site_set("Comment", s if s else "None"); return self.status()

    # ---- annotation columns + CSV writeback ------------------------------
    def _ensure_annotation_columns(self):
        header = self.elist[0]
        for name, key, default in self._ANNOT:
            if name not in header:
                header.append(name)
                for row in self.elist[1:]:
                    row.append(default)
        self.ci = {}
        for n, name in enumerate(header):
            for cname, key, _d in self._ANNOT:
                if name == cname:
                    self.ci[key] = n

    def _save_csv(self):
        if not self.eventCSV:
            return
        try:
            # one-time safety backup of a pre-existing inspect CSV
            if not self._csv_backed_up and os.path.isfile(self.eventCSV):
                import shutil
                bak = self.eventCSV + ".bcoot-bak"
                if not os.path.isfile(bak):
                    shutil.copyfile(self.eventCSV, bak)
            self._csv_backed_up = True
            with open(self.eventCSV, "w", newline="") as f:
                csv.writer(f).writerows(self.elist)
        except Exception as e:
            print("PanDDA: CSV save failed (%s)" % e)

    def _annot_get(self, key, default=""):
        n = self.ci.get(key)
        if n is None or not self.elist or self.index <= 0:
            return default
        row = self.elist[self.index]
        return row[n] if n < len(row) else default

    def _annot_set(self, key, value):
        n = self.ci.get(key)
        if n is None or not self.elist or self.index <= 0:
            return
        row = self.elist[self.index]
        while len(row) <= n:
            row.append("")
        row[n] = value
        self._save_csv()

    def _mark_viewed(self):
        if self.ci.get("viewed") is not None and self._annot_get("viewed") != "True":
            self._annot_set("viewed", "True")

    # annotation accessors (used by the panel)
    def get_interesting(self): return self._annot_get("interesting", "False")
    def set_interesting(self, on):
        self._annot_set("interesting", "True" if on else "False"); return self.status()
    def get_confidence(self):  return self._annot_get("confidence", "unassigned")
    def set_confidence(self, s):
        self._annot_set("confidence", s or "unassigned"); return self.status()
    def get_comment(self):     return self._annot_get("comment", "None")
    def set_comment(self, s):
        s = (s or "").strip()
        self._annot_set("comment", s if s else "None"); return self.status()
    def get_placed(self):      return self._annot_get("placed", "False")
    def set_placed(self, on):
        # the inspect-UI "Ligand Placed / No Ligand Placed" radio, independent of Save
        self._annot_set("placed", "True" if on else "False"); return self.status()

    # ---- state queries for the inspect UI --------------------------------
    def is_loaded(self):
        return "True" if self.order else "False"

    def get_map_state(self):
        # "emap|zmap|xray|average" booleans, so a rebuilt panel's map
        # checkboxes can reflect what is actually displayed.
        return "%s|%s|%s|%s" % (self.show_emap, self.show_zmap,
                                self.show_xray, self.show_average)

    def progress(self):
        # "event_pos|event_total|site_pos|site_total" for the inspect header
        if not self.order:
            return "0|0|0|0"
        sites = self._sites_in_order()
        cur = self.elist[self.index][self.col["site"]] if (
            "site" in self.col and self.index > 0) else None
        try:
            si = sites.index(cur) + 1
        except (ValueError, TypeError):
            si = 0
        return "%d|%d|%d|%d" % (self.pos + 1, len(self.order), si, len(sites))

    def _sites_in_order(self):
        if "site" not in self.col:
            return []
        k = self.col["site"]
        sites = []
        for ri in self.order:
            s = self.elist[ri][k]
            if s not in sites:
                sites.append(s)
        return sites

    # ---- event selection / sorting / filtering ---------------------------
    def set_selection(self, criterion):
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        rows = list(range(1, len(self.elist)))
        c = (criterion or "").strip().lower()
        if c.startswith("sort by cluster") and "cluster_size" in self.col:
            k = self.col["cluster_size"]
            def csize(i):
                try:
                    return float(self.elist[i][k])
                except (ValueError, IndexError):
                    return 0.0
            rows.sort(key=csize, reverse=True)
        elif c.startswith("sort by z") and "z_peak" in self.col and "dtag" in self.col:
            # shim's default order: datasets by their max z-peak desc, then
            # events within a dataset by z-peak desc (keeps a dataset together).
            zk = self.col["z_peak"]
            dk = self.col["dtag"]
            def zval(i):
                try:
                    return float(self.elist[i][zk])
                except (ValueError, IndexError):
                    return float("-inf")
            dmax = {}
            for i in rows:
                d = self.elist[i][dk]
                z = zval(i)
                if d not in dmax or z > dmax[d]:
                    dmax[d] = z
            rows.sort(key=lambda i: (-dmax[self.elist[i][dk]], -zval(i)))
        elif c.startswith("sort alpha") and "dtag" in self.col:
            k = self.col["dtag"]
            rows.sort(key=lambda i: self.elist[i][k])
        elif c.startswith("unviewed"):
            rows = [i for i in rows if self._row(i, "viewed") != "True"]
        elif c.startswith("interesting"):
            rows = [i for i in rows if self._row(i, "interesting") == "True"]
        elif c.startswith("not modelled"):
            rows = [i for i in rows if not self._is_modelled(i)]
        elif c.startswith("modelled"):
            rows = [i for i in rows if self._is_modelled(i)]
        if not rows:
            return self._msg("PanDDA: no events match '%s'" % criterion)
        self.order = rows
        self.pos = 0
        return self._load_pos()

    def _row(self, i, key):
        n = self.ci.get(key)
        if n is None:
            return ""
        row = self.elist[i]
        return row[n] if n < len(row) else ""

    def _is_modelled(self, i):
        # a dataset counts as modelled if our CSV flag is set OR a built model
        # already exists on disk (e.g. from a previous inspect session).
        if self._row(i, "placed") == "True":
            return True
        if "dtag" not in self.col:
            return False
        dtag = self.elist[i][self.col["dtag"]]
        p = os.path.join(self.panddaDir, "processed_datasets", dtag,
                         "modelled_structures", "%s-pandda-model.pdb" % dtag)
        return os.path.isfile(p)

    def _cell(self, key, default=""):
        if key in self.col:
            return self.elist[self.index][self.col[key]]
        return default

    # ---- navigation ------------------------------------------------------
    def next_event(self):
        return self._go(1)

    def prev_event(self):
        return self._go(-1)

    def _load_pos(self):
        if not self.order:
            return self._msg("PanDDA: no events in current selection")
        self.pos = max(0, min(self.pos, len(self.order) - 1))
        self.index = self.order[self.pos]
        return self._load_current()

    def _go(self, step):
        if not self.order:
            return self._msg("PanDDA: no events in current selection")
        self.pos = (self.pos + step) % len(self.order)   # wrap past either end
        self.index = self.order[self.pos]
        return self._load_current()

    def next_site(self):
        return self._go_site(1)

    def prev_site(self):
        return self._go_site(-1)

    def _go_site(self, step):
        if not self.order:
            return self._msg("PanDDA: no events in current selection")
        if "site" not in self.col:
            return self._msg("PanDDA: event CSV has no site column")
        k = self.col["site"]
        sites = self._sites_in_order()          # unique site ids, in view order
        cur = self.elist[self.index][k] if self.index > 0 else ""
        try:
            i = sites.index(cur)
        except ValueError:
            i = 0
        target = sites[(i + step) % len(sites)]   # wrap past either end
        for p, ri in enumerate(self.order):       # jump to that site's first event
            if self.elist[ri][k] == target:
                self.pos = p
                self.index = ri
                break
        return self._load_current()

    def next_unviewed(self):
        return self._go_match(lambda i: self._row(i, "viewed") != "True",
                              "no more unviewed events")

    def next_modelled(self):
        return self._go_match(lambda i: self._is_modelled(i),
                              "no modelled events ahead")

    def _go_match(self, pred, none_msg):
        """Advance to the next event (wrapping) whose elist row matches pred."""
        if not self.order:
            return self._msg("PanDDA: no events in current selection")
        n = len(self.order)
        for off in range(1, n + 1):
            p = (self.pos + off) % n
            if pred(self.order[p]):
                self.pos = p
                self.index = self.order[p]
                return self._load_current()
        return self._msg("PanDDA: %s" % none_msg)

    def dataset_list(self):
        # unique dataset codes (dtags) in current view order, newline-joined
        if not self.order or "dtag" not in self.col:
            return ""
        k = self.col["dtag"]
        seen = []
        for ri in self.order:
            d = self.elist[ri][k]
            if d not in seen:
                seen.append(d)
        return "\n".join(seen)

    def go_to_dataset(self, dtag):
        # jump to the first event of the named dataset within the current view
        if not self.order:
            return self._msg("PanDDA: no folder loaded")
        if "dtag" not in self.col:
            return self._msg("PanDDA: event CSV has no dtag column")
        k = self.col["dtag"]
        for p, ri in enumerate(self.order):
            if self.elist[ri][k] == dtag:
                self.pos = p
                self.index = ri
                return self._load_current()
        return self._msg("PanDDA: dataset %s not in current selection" % dtag)

    # ---- map visibility --------------------------------------------------
    def set_show_emap(self, on):
        self.show_emap = bool(on)
        imol = self.mol.get("emap")
        if imol is not None and imol >= 0:
            coot.set_map_displayed(imol, 1 if self.show_emap else 0)
            coot.graphics_draw()
        return self.status()

    def set_show_zmap(self, on):
        self.show_zmap = bool(on)
        imol = self.mol.get("zmap")
        if imol is not None and imol >= 0:
            coot.set_map_displayed(imol, 1 if self.show_zmap else 0)
            coot.graphics_draw()
        return self.status()

    def _set_map_shown(self, key, on):
        m = self.mol.get(key)
        if m is None:
            return
        for im in (m if isinstance(m, (list, tuple)) else [m]):
            if im is not None and im >= 0:
                coot.set_map_displayed(im, 1 if on else 0)

    def set_show_xray(self, on):
        self.show_xray = bool(on)
        if self.show_xray and not self.mol.get("xray"):
            self._load_xray([])          # lazy-load for the current event
        else:
            self._set_map_shown("xray", self.show_xray)
        coot.graphics_draw()
        return self.status()

    def set_show_average(self, on):
        self.show_average = bool(on)
        if self.show_average and not self.mol.get("average"):
            self._load_average([])
        else:
            self._set_map_shown("average", self.show_average)
        coot.graphics_draw()
        return self.status()

    # ---- ligand modelling ------------------------------------------------
    # ---- ligand auto-loader (Preferences > Others > Ligands) -------------
    def _load_ligand_config(self):
        """Resolve the ligand index CSV + cifs dir for the auto-loader.

        The index is PROJECT-specific, so it is hardcoded (for now) to
        'dataset_ligand_index.csv' one level up from the selected PanDDA folder
        (i.e. in the project folder); the Ligands-prefs index path is only a
        fallback. The cifs dir (a shared ligand library) comes from the
        Preferences > Others > Ligands tab. Best-effort: missing config just
        disables the auto-loader."""
        self._lig_index = {}
        self._lig_cifs_dir = None
        self._split_on_load = False
        # prefs file lines: 0 index(unused) | 1 cifs dir | 2 split flag "1"/"0"
        cfg_index = ""
        cfg = os.path.expanduser("~/.coot-preferences/bandicoot-ligands")
        try:
            with open(cfg) as f:
                lines = f.read().splitlines()
            cfg_index = lines[0].strip() if len(lines) > 0 else ""
            cifs_dir = lines[1].strip() if len(lines) > 1 else ""
            if cifs_dir and os.path.isdir(cifs_dir):
                self._lig_cifs_dir = cifs_dir
            split = lines[2].strip() if len(lines) > 2 else ""
            self._split_on_load = split in ("1", "Yes", "true")   # default No
        except Exception:
            pass
        # project-relative index (one up from the pandda folder) wins
        index_path = ""
        if self.panddaDir:
            proj = os.path.join(os.path.dirname(self.panddaDir),
                                "dataset_ligand_index.csv")
            if os.path.isfile(proj):
                index_path = proj
        if not index_path and cfg_index and os.path.isfile(cfg_index):
            index_path = cfg_index            # fallback to the prefs path
        if index_path:
            self._lig_index = self._parse_ligand_index(index_path)

    def _parse_ligand_index(self, path):
        """CSV with columns 'dataset id' and 'ligand id' (shim format); lenient
        about 'dtag'/'ligand' synonyms. Returns {dtag: ligand_id}."""
        out = {}
        try:
            with open(path) as f:
                rows = list(csv.reader(f))
            if not rows:
                return out
            # Excel "Save As CSV" wraps a whole row in quotes when the cell's
            # own text contains a comma, so csv.reader hands back a single
            # field per row (e.g. ['x03697-1,pxr-1_C02']). Detect that over-
            # quoting from the header and re-split every row on the comma.
            if len(rows[0]) == 1 and "," in rows[0][0]:
                split = []
                for r in rows:
                    if len(r) == 1 and "," in r[0]:
                        split.append(next(csv.reader([r[0]])))
                    else:
                        split.append(r)
                rows = split
            hdr = [h.strip().lower() for h in rows[0]]
            def col(*names, **kw):
                for n in names:
                    if n in hdr:
                        return hdr.index(n)
                return kw.get("default")
            di = col("dataset id", "dtag", "dataset", default=0)
            li = col("ligand id", "ligand", "ligand_id", default=1)
            for r in rows[1:]:
                if len(r) > max(di, li):
                    d = r[di].strip()
                    lg = r[li].strip()
                    if d and lg:
                        out[d] = lg
        except Exception as e:
            print("PanDDA: could not read ligand index (%s)" % e)
        return out

    @staticmethod
    def _stereo_suffix(dir_basename):
        """Trailing R/S stereo token of a dir name (e.g. 'R','RS','RRR'), else
        None. Case-insensitive (handles lowercase 'rr'/'ss' directories)."""
        last = dir_basename.split("_")[-1].upper()
        if last and all(c in "RS" for c in last):
            return last
        return None

    @staticmethod
    def _stereo_to_code(stereo):
        """Map a stereo suffix to a unique 3-letter PDB residue code."""
        if len(stereo) == 1:
            return "LI" + stereo          # R->LIR, S->LIS
        if len(stereo) == 2:
            return "L" + stereo           # RR->LRR, RS->LRS, ...
        return stereo[:3]                 # RRR..SSS used as-is

    def _resolve_isomer_files(self, idir):
        """(pdb, cif) for an isomer dir, preferring the {base}-1 tautomer files."""
        base = os.path.basename(idir)
        taut_pdb = os.path.join(idir, base + "-1.pdb")
        taut_cif = os.path.join(idir, base + "-1.cif")
        pdb = taut_pdb if os.path.isfile(taut_pdb) else os.path.join(idir, base + ".pdb")
        cif = taut_cif if os.path.isfile(taut_cif) else os.path.join(idir, base + ".cif")
        return pdb, cif

    def _resolve_isomer_dirs(self, dtag):
        """Isomer directories for a dataset's ligand_id: an exact {ligand_id}
        directory, else all {ligand_id}_{stereo} directories."""
        if not self._lig_cifs_dir:
            return []
        lig_id = self._lig_index.get(dtag)
        if not lig_id:
            return []
        exact = os.path.join(self._lig_cifs_dir, lig_id)
        if os.path.isdir(exact):
            return [exact]
        prefix = lig_id + "_"
        try:
            return sorted(os.path.join(self._lig_cifs_dir, e)
                          for e in os.listdir(self._lig_cifs_dir)
                          if e.startswith(prefix)
                          and os.path.isdir(os.path.join(self._lig_cifs_dir, e)))
        except OSError:
            return []

    def _near_ligand_dirs(self, lig_id):
        """Directory names under the cifs dir that match lig_id but for '-'/'_'
        separators or case — the usual CSV-vs-folder naming slip (e.g. CSV
        'pxr-1_E14' vs folder 'pxr_1_E14'). Used only to enrich the error."""
        def norm(s):
            return s.lower().replace("-", "_")
        target = norm(lig_id)
        hits = []
        try:
            for e in sorted(os.listdir(self._lig_cifs_dir)):
                if not os.path.isdir(os.path.join(self._lig_cifs_dir, e)):
                    continue
                ne = norm(e)
                if ne == target or ne.startswith(target + "_"):
                    hits.append(e)
        except OSError:
            pass
        return hits

    def _find_ligand_cif(self):
        """The expected ligand restraints .cif for this dataset/event: the
        event-specific rhofit result first, else the dataset's ligand_files/."""
        if self.event:
            for d in self._candidate_dirs():
                p = os.path.join(d, self.event, "rhofit", "best.cif")
                if os.path.isfile(p):
                    return p
        for d in self._candidate_dirs():
            hits = sorted(glob.glob(os.path.join(d, "ligand_files", "*.cif")))
            if hits:
                return hits[0]
        return None

    def _load_ligand(self):
        """Lazy-load the first available ligand candidate; return imol (or None)."""
        cands = self._ligand_candidates()
        if not cands:
            return None
        coord, cif, code = cands[0]
        return self._open_candidate(coord, cif, code)

    def _set_ligand_occupancy(self, imol):
        # match pandda.inspect: ligand occupancy = (1-BDC). Harmless no-op if the
        # ligand isn't chain X / residue 1.
        try:
            coot.set_occupancy_residue_range(imol, "X", 1, 1, float(self.bdc))
        except (ValueError, TypeError):
            pass

    def _centre_molecule_on_view(self, imol):
        # move a molecule's centroid onto the screen/rotation centre (the event);
        # replaces the missing move_molecule_to_screen_centre binding.
        dx = coot.rotation_centre_position(0) - coot.molecule_centre_internal(imol, 0)
        dy = coot.rotation_centre_position(1) - coot.molecule_centre_internal(imol, 1)
        dz = coot.rotation_centre_position(2) - coot.molecule_centre_internal(imol, 2)
        coot.translate_molecule_by(imol, dx, dy, dz)

    # Fixed carbon bond colour-map rotations (degrees), so models look the same
    # across datasets instead of Coot's per-molecule hue rotation. Ligands get a
    # distinct value so an unmerged ligand is visually obvious vs the model it
    # will merge into.
    MODEL_BOND_COLOUR = 18.0
    LIGAND_BOND_COLOUR = 125.0

    def _read_model(self, path, recentre=0, colour=None):
        """Read a coordinate file and pin its bond colour-map rotation to a fixed
        value (model vs ligand), so loads are consistent across datasets/isomers
        rather than Coot's per-molecule hue rotation. All driver model loads route
        through here; ligand loads pass colour=LIGAND_BOND_COLOUR."""
        if colour is None:
            colour = self.MODEL_BOND_COLOUR
        imol = coot.handle_read_draw_molecule_with_recentre(path, recentre)
        if imol is not None and imol >= 0:
            try:
                coot.set_molecule_bonds_colour_map_rotation(imol, colour)
            except Exception:
                pass
        return imol

    def load_ligand_file(self, path):
        """Load a user-supplied ligand coordinate file as the current ligand.
        If it's a coordinate file with a sibling <stem>.cif restraints dictionary,
        load that too automatically (saves a manual import for real-space refine)."""
        if not self.elist:
            return self._msg("PanDDA: load a PanDDA folder and pick an event first")
        if not path or not os.path.isfile(path):
            return self._msg("PanDDA: not a file: %s" % path)
        stem, ext = os.path.splitext(path)
        if ext.lower() == ".cif":
            coot.read_cif_dictionary(path)        # restraints and/or mmCIF coords
        else:
            sibling_cif = stem + ".cif"           # e.g. lig.pdb -> lig.cif
            if os.path.isfile(sibling_cif):
                coot.read_cif_dictionary(sibling_cif)
                print("PanDDA: auto-loaded restraints %s" % os.path.basename(sibling_cif))
        imol = self._read_model(path, 0, self.LIGAND_BOND_COLOUR)
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            return self._msg("PanDDA: no coordinates read from %s" % os.path.basename(path))
        self.mol["ligand"] = imol
        self._set_ligand_occupancy(imol)
        coot.graphics_draw()
        return self.status()

    def ligand_from_smiles(self, smiles, tlc):
        """Build a 3D ligand from a SMILES string via acedrg (see _acedrg_build)
        and load it as the current PanDDA ligand: occupancy from BDC, dropped on
        the event. The acedrg core is shared with the standalone
        ligand_from_smiles_standalone() behind Other Modelling Tools."""
        if not self.elist:
            return self._msg("PanDDA: load a PanDDA folder and pick an event first")
        pdb, cif, err = _acedrg_build(smiles, tlc)
        if err:
            return self._msg("PanDDA: " + err)
        if cif and os.path.isfile(cif):
            coot.read_cif_dictionary(cif)
        imol = self._read_model(pdb, 0, self.LIGAND_BOND_COLOUR)
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            return self._msg("PanDDA: acedrg built %s but Coot could not read it"
                             % os.path.basename(pdb))
        self.mol["ligand"] = imol
        self._set_ligand_occupancy(imol)
        # a freshly-built ligand has no meaningful pose -> drop it on the event
        self._centre_molecule_on_view(imol)
        coot.graphics_draw()
        return self.status()

    def _no_ligand_reason(self):
        """A specific explanation for why the auto-loader found no ligand for the
        current dataset (so the 'no ligand' message is diagnosable)."""
        self._load_ligand_config()        # reflect any just-edited prefs path
        if not self._lig_cifs_dir:
            return ("no ligand CIFs directory set "
                    "(Preferences > Others > Ligands) for %s" % self.xtal)
        if not self._lig_index:
            return ("no dataset_ligand_index.csv found (expected one level above "
                    "the pandda folder) for %s" % self.xtal)
        lig_id = self._lig_index.get(self.xtal)
        if not lig_id:
            return "%s is not listed in the ligand index" % self.xtal
        dirs = self._resolve_isomer_dirs(self.xtal)
        if not dirs:
            msg = ("%s maps to ligand '%s', but no folder named '%s' (nor "
                   "'%s_<stereo>') was found in the ligand CIFs directory %s"
                   % (self.xtal, lig_id, lig_id, lig_id, self._lig_cifs_dir))
            near = self._near_ligand_dirs(lig_id)
            if near:
                msg += (" -- but %s %s present; the ligand id in the CSV must "
                        "match the folder name exactly (check '-' vs '_' and "
                        "case)" % (", ".join("'%s'" % n for n in near),
                                   "is" if len(near) == 1 else "are"))
            return msg
        return ("ligand '%s' directory found (%s) but no .pdb/.cif inside"
                % (lig_id, ", ".join(os.path.basename(d) for d in dirs)))

    def _ligand_candidates(self):
        """Ligand coordinate files for this dataset/event as (coord, cif, code)
        triples. Auto-loader isomers (from the Ligands prefs config, stereo-coded
        + tautomer-preferred) come first, then the event rhofit result, then the
        dataset's ligand_files/."""
        self._load_ligand_config()        # pick up any just-edited Ligands prefs path
        cands = []
        seen = set()
        def add(coord, cif, code="LIG"):
            if coord and os.path.isfile(coord) and coord not in seen:
                seen.add(coord)
                cands.append((coord, cif if (cif and os.path.isfile(cif)) else None, code))
        # auto-loader: isomer directories for this dataset's ligand_id
        for idir in self._resolve_isomer_dirs(self.xtal):
            sfx = self._stereo_suffix(os.path.basename(idir))
            code = self._stereo_to_code(sfx) if sfx else "LIG"
            pdb, cif = self._resolve_isomer_files(idir)
            add(pdb, cif, code)
        if self.event:
            for d in self._candidate_dirs():
                best = os.path.join(d, self.event, "rhofit", "best.pdb")
                add(best, os.path.join(d, self.event, "rhofit", "best.cif"))
        for d in self._candidate_dirs():
            for pdb in sorted(glob.glob(os.path.join(d, "ligand_files", "*.pdb"))):
                add(pdb, pdb[:-4] + ".cif")
            for cif in sorted(glob.glob(os.path.join(d, "ligand_files", "*.cif"))):
                sib = cif[:-4] + ".pdb"     # .cif may carry coords itself
                add(sib if os.path.isfile(sib) else cif, cif)
        return cands

    def _open_candidate(self, coord, cif, code):
        """Load one ligand candidate, renaming LIG->stereo code on the fly for
        multi-isomer ligands (originals untouched). Tracks the imol in
        self._lig_mols and sets it as the current ligand. Returns imol or None."""
        load_pdb = _rename_pdb_residue(coord, code) if code != "LIG" else coord
        load_cif = None
        if cif:
            self.ligcif = cif
            load_cif = _rename_cif_residue(cif, code) if code != "LIG" else cif
            coot.read_cif_dictionary(load_cif)
        imol = self._read_model(load_pdb, 0, self.LIGAND_BOND_COLOUR)
        # temp renamed files are parsed into memory now -> remove them
        if load_pdb != coord:
            try: os.remove(load_pdb)
            except OSError: pass
        if load_cif and cif and load_cif != cif:
            try: os.remove(load_cif)
            except OSError: pass
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            return None
        self.mol["ligand"] = imol
        if imol not in self._lig_mols:
            self._lig_mols.append(imol)
        self._set_ligand_occupancy(imol)
        return imol

    def open_next_ligand(self):
        """Load the next available ligand coordinate file, cycling through them."""
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        cands = self._ligand_candidates()
        if not cands:
            return self._msg("PanDDA: %s" % self._no_ligand_reason())
        self._lig_cycle = (self._lig_cycle + 1) % len(cands)
        coord, cif, code = cands[self._lig_cycle]
        old = self.mol.get("ligand")
        if old is not None and old >= 0 and coot.is_valid_model_molecule(old):
            coot.close_molecule(old)
            if old in self._lig_mols:
                self._lig_mols.remove(old)
        imol = self._open_candidate(coord, cif, code)
        if imol is None:
            return self._msg("PanDDA: could not read %s" % os.path.basename(coord))
        coot.graphics_draw()
        return self._msg("PanDDA: ligand %d/%d  %s"
                         % (self._lig_cycle + 1, len(cands), os.path.basename(coord)))

    def open_ligand_here(self):
        """Load ALL candidate ligands (every isomer/tautomer for this dataset) at
        once, each centred on the current event. Use the display panel to hide
        the isomers you don't want, then Merge (which merges only displayed)."""
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        cands = self._ligand_candidates()
        if not cands:
            return self._msg("PanDDA: %s" % self._no_ligand_reason())
        n = 0
        for coord, cif, code in cands:
            imol = self._open_candidate(coord, cif, code)
            if imol is not None and imol >= 0:
                self._centre_molecule_on_view(imol)   # drop it on the event
                n += 1
        coot.graphics_draw()
        if not n:
            return self._msg("PanDDA: %s" % self._no_ligand_reason())
        return self._msg("PanDDA: opened %d ligand(s) at the event "
                         "(hide unwanted isomers, then Merge)" % n)

    def reload_saved_model(self):
        """Reload the last saved <dtag>-pandda-model.pdb (else the input model)."""
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        saved = self._find(
            os.path.join("modelled_structures", "%s-pandda-model.pdb" % self.xtal))
        path = saved or self._find("%s-pandda-input.pdb" % self.xtal)
        if not path:
            return self._msg("PanDDA: no model to reload for %s" % self.xtal)
        return self._replace_protein(path,
                                     "reloaded %s" % os.path.basename(path))

    def reset_to_unfitted(self):
        """Discard edits: reload the original <dtag>-pandda-input.pdb."""
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        path = self._find("%s-pandda-input.pdb" % self.xtal)
        if not path:
            return self._msg("PanDDA: no unfitted model for %s" % self.xtal)
        return self._replace_protein(path, "reset to unfitted model")

    def _replace_protein(self, path, note):
        old = self.mol.get("protein")
        if old is not None and old >= 0 and coot.is_valid_model_molecule(old):
            coot.close_molecule(old)
        coot.set_nomenclature_errors_on_read("ignore")
        self.pdb = path
        self.mol["protein"] = self._read_model(path, 0)
        self.merged = False
        coot.set_rotation_centre(self.x, self.y, self.z)
        coot.graphics_draw()
        return self._msg("PanDDA: %s" % note)

    def load_unfitted_comparison(self):
        """Load <dtag>-pandda-input.pdb as an extra molecule for comparison only,
        without replacing the working model."""
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        path = self._find("%s-pandda-input.pdb" % self.xtal)
        if not path:
            return self._msg("PanDDA: no unfitted model for %s" % self.xtal)
        old = self.mol.get("compare")
        if old is not None and old >= 0 and coot.is_valid_model_molecule(old):
            coot.close_molecule(old)
        coot.set_nomenclature_errors_on_read("ignore")
        self.mol["compare"] = self._read_model(path, 0)
        coot.graphics_draw()
        return self._msg("PanDDA: loaded comparison model %s" % os.path.basename(path))

    def place_ligand(self):
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        imol = self.mol.get("ligand")
        # re-load if there's no current ligand, or the previous one was deleted
        # (a stale imol would crash _centre_molecule_on_view).
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            imol = self._load_ligand()              # lazy-load / reload
            if imol is None or imol < 0:
                return self._msg("PanDDA: %s" % self._no_ligand_reason())
        self._centre_molecule_on_view(imol)
        coot.graphics_draw()
        return self.status()

    def merge_ligand(self):
        prot = self.mol.get("protein")
        if prot is None or prot < 0:
            return self._msg("PanDDA: no model loaded to merge into")
        # merge only DISPLAYED ligand molecules, so hidden isomers (the ones you
        # decided against) are excluded -- toggle them off before merging.
        to_merge = []
        for m in list(self._lig_mols):
            if m is not None and m >= 0 and coot.is_valid_model_molecule(m):
                try:
                    shown = coot.mol_is_displayed(m)
                except Exception:
                    shown = 1
                if shown:
                    to_merge.append(m)
        if not to_merge:                      # back-compat: the single current ligand
            lig = self.mol.get("ligand")
            if lig is not None and lig >= 0 and coot.is_valid_model_molecule(lig):
                to_merge = [lig]
        if not to_merge:
            return self._msg("PanDDA: no displayed ligand to merge (Place/Open a ligand first)")
        coot.merge_molecules_py(to_merge, prot)
        for m in to_merge:
            coot.close_molecule(m)
            if m in self._lig_mols:
                self._lig_mols.remove(m)
        self.mol["ligand"] = None
        self.merged = True
        coot.graphics_draw()
        return self._msg("PanDDA: merged %d ligand(s) into the model" % len(to_merge))

    def save_model(self):
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        prot = self.mol.get("protein")
        if prot is None or prot < 0:
            return self._msg("PanDDA: no model loaded to save")
        ms = os.path.join(self.panddaDir, "processed_datasets", self.xtal,
                          "modelled_structures")
        try:
            if not os.path.isdir(ms):
                os.makedirs(ms)
        except OSError as e:
            return self._msg("PanDDA: cannot create %s (%s)" % (ms, e))
        # keep a versioned backup (fitted-vNNNN.pdb), then update the canonical
        # <dtag>-pandda-model.pdb that pandda.inspect reads back.
        nums = []
        for p in glob.glob(os.path.join(ms, "fitted-v*.pdb")):
            try:
                nums.append(int(os.path.basename(p)[len("fitted-v"):-len(".pdb")]))
            except ValueError:
                pass
        versioned = os.path.join(ms, "fitted-v%04d.pdb" % ((max(nums) + 1) if nums else 1))
        coot.write_pdb_file(prot, versioned)
        model = os.path.join(ms, "%s-pandda-model.pdb" % self.xtal)
        try:
            import shutil
            shutil.copyfile(versioned, model)
        except Exception as e:
            return self._msg("PanDDA: wrote %s but could not update model (%s)"
                             % (os.path.basename(versioned), e))
        try:
            coot.info_dialog("Saved %s\nand %s-pandda-model.pdb"
                             % (os.path.basename(versioned), self.xtal))
        except Exception:
            pass
        print("PanDDA: saved %s and %s-pandda-model.pdb" % (versioned, self.xtal))
        if self.merged:                       # record that a ligand was modelled
            self._annot_set("placed", "True")
        return self.status()

    def _load_current(self):
        self.reset_event()
        self._ensure_view_setup()
        self._close_all_molecules()

        self.xtal = self._cell("dtag")
        self.event = self._cell("event")
        self.site = self._cell("site", "?")
        self.bdc = self._cell("bdc", "")
        self.resolution = self._cell("resolution", "?")
        self.r_free = self._cell("r_free", "?")
        self.r_work = self._cell("r_work", "?")
        self.z_peak = self._cell("z_peak", "?")
        self.cluster_size = self._cell("cluster_size", "?")
        self.map_uncertainty = self._cell("map_uncertainty", "?")
        try:
            self.x = float(self._cell("x"))
            self.y = float(self._cell("y"))
            self.z = float(self._cell("z"))
        except ValueError:
            return self._msg("PanDDA: bad coordinates for %s event %s" % (self.xtal, self.event))

        missing = []
        self._load_pdb(missing)
        # recentre on the event BEFORE drawing the maps -- _fix_ccp4_map centres
        # the native map box here.
        coot.set_rotation_centre(self.x, self.y, self.z)
        self._load_emap(missing)
        self._load_zmap(missing)
        self._load_xray(missing)
        self._load_average(missing)
        coot.graphics_draw()

        if self.mol["emap"] is not None and self.mol["emap"] >= 0:
            coot.set_imol_refinement_map(self.mol["emap"])

        self._mark_viewed()
        s = self.status()
        if missing:
            s += "  [missing: %s]" % ", ".join(missing)
        return self._msg(s)

    # ---- file finding ----------------------------------------------------
    def _candidate_dirs(self):
        dirs = [os.path.join(self.panddaDir, "processed_datasets", self.xtal),
                os.path.join(self.panddaDir + "-export", self.xtal)]
        return [d for d in dirs if os.path.isdir(d)]

    def _find(self, *names):
        for d in self._candidate_dirs():
            for nm in names:
                p = os.path.join(d, nm)
                if os.path.isfile(p):
                    return p
        return None

    def _glob_one(self, *patterns):
        for d in self._candidate_dirs():
            for pat in patterns:
                hits = sorted(glob.glob(os.path.join(d, pat)))
                if hits:
                    return hits[0]
        return None

    def _read_mtz_first_map(self, mtz):
        res = coot.auto_read_make_and_draw_maps_from_mtz(mtz)
        if isinstance(res, (list, tuple)):
            if not res:
                return -1
            for m in res[1:]:
                coot.close_molecule(m)
            return res[0]
        return res

    def _read_native_ccp4(self, path, is_diff):
        """Read a native pandda .ccp4 with the Coot-0.9 axis/tiling fix."""
        tmp = _fix_ccp4_map(path, (self.x, self.y, self.z))
        if tmp:
            imol = coot.read_ccp4_map(tmp, is_diff)   # Coot loads into memory now
            try:
                os.remove(tmp)
            except OSError:
                pass
            return imol
        return coot.read_ccp4_map(path, is_diff)      # fallback: raw (may misalign)

    def _load_pdb(self, missing):
        pdb = self._find(
            os.path.join("modelled_structures", "%s-pandda-model.pdb" % self.xtal),
            "%s-pandda-input.pdb" % self.xtal)
        if not pdb:
            missing.append("model")
            return
        self.pdb = pdb
        coot.set_nomenclature_errors_on_read("ignore")
        # Optional (Preferences > Others > Ligands, default OFF): a fitted model
        # has ligand(s) merged in; split them into separate molecules so real-
        # space refinement has no protein-ligand LJ clashes (use Merge Ligand
        # With Model before saving). Only for already-modelled structures.
        self._load_ligand_config()        # pick up the latest split-on-load pref
        if self._split_on_load and "modelled_structures" in pdb:
            prot = self._split_fitted_model(pdb)
            if prot is not None:
                self.mol["protein"] = prot
                return
        self.mol["protein"] = self._read_model(pdb, 0)

    def _split_fitted_model(self, filename):
        """Parse a fitted model file, splitting non-standard ligand residues into
        their own molecules. Loads a protein-only model (returned) plus one
        molecule per ligand residue (tracked in self._lig_mols). Returns the
        protein-only imol, or None when there are no ligands to split."""
        cryst1 = ""
        prot_lines = []
        res_lines = {}                    # (chain,resseq,icode,code) -> [lines]
        try:
            with open(filename) as f:
                for ln in f:
                    if ln.startswith("CRYST1"):
                        cryst1 = ln
                        prot_lines.append(ln)
                        continue
                    if ln[:6] in ("HETATM", "ANISOU") and len(ln) >= 20:
                        code = ln[17:20].strip()
                        if code not in _STANDARD_RESIDUES:
                            if len(ln) >= 27:
                                key = (ln[21], ln[22:26].strip(), ln[26].strip(), code)
                                res_lines.setdefault(key, []).append(ln)
                            continue       # exclude from protein PDB
                    prot_lines.append(ln)
        except Exception as e:
            print("PanDDA: could not parse fitted model (%s)" % e)
            return None
        if not res_lines:
            return None                   # no ligands -> caller loads normally

        try:
            fd, prot_tmp = tempfile.mkstemp(suffix="_protein_only.pdb")
            with os.fdopen(fd, "w") as f:
                f.writelines(prot_lines)
            prot_imol = self._read_model(prot_tmp, 0)
            try: os.remove(prot_tmp)
            except OSError: pass
        except Exception as e:
            print("PanDDA: could not load protein-only model (%s)" % e)
            return None

        for (chain, resseq, icode, code), lines in res_lines.items():
            try:
                fd, lig_tmp = tempfile.mkstemp(suffix="_split_" + code + ".pdb")
                with os.fdopen(fd, "w") as f:
                    if cryst1:
                        f.write(cryst1)
                    f.writelines(lines)
                    f.write("END\n")
                lig_imol = self._read_model(lig_tmp, 0, self.LIGAND_BOND_COLOUR)
                try: os.remove(lig_tmp)
                except OSError: pass
                if lig_imol is not None and lig_imol >= 0:
                    self._lig_mols.append(lig_imol)
                    print("PanDDA: split %s chain %s resid %s -> imol %d"
                          % (code, chain, resseq, lig_imol))
            except Exception as e:
                print("PanDDA: could not split ligand %s (%s)" % (code, e))
        return prot_imol

    def _load_emap(self, missing):
        mtz = self._glob_one("%s-event_%s*.mtz" % (self.xtal, self.event),
                             "%s-pandda-output-event-*.mtz" % self.xtal)
        if mtz:
            self.emap = mtz
            self.mol["emap"] = self._read_mtz_first_map(mtz)
        else:
            ccp4 = self._glob_one("%s-event_%s_1-BDC_*_map.native.ccp4" % (self.xtal, self.event))
            if not ccp4:
                missing.append("event_map")
                return
            self.emap = ccp4
            self.mol["emap"] = self._read_native_ccp4(ccp4, 0)
        imol = self.mol["emap"]
        if imol is not None and imol >= 0:
            coot.set_last_map_colour(0, 0, 1)
            # PanDDA maps are pre-scaled (absolute value == sigma). The event map
            # is contoured at 2.0*(1-BDC) absolute (~2 sigma effective). self.bdc
            # is the CSV "1-BDC" value, i.e. (1-BDC). Matches pandda.inspect.
            try:
                coot.set_contour_level_absolute(imol, 2.0 * float(self.bdc))
            except (ValueError, TypeError):
                pass
            coot.set_map_displayed(imol, 1 if self.show_emap else 0)

    def _load_zmap(self, missing):
        mtz = self._glob_one("%s-z_map*.mtz" % self.xtal,
                             "%s-pandda-output.mtz" % self.xtal)
        if mtz:
            self.zmap = mtz
            self.mol["zmap"] = self._read_mtz_first_map(mtz)
        else:
            ccp4 = self._find("%s-z_map.native.ccp4" % self.xtal)
            if not ccp4:
                missing.append("z_map")
                return
            self.zmap = ccp4
            self.mol["zmap"] = self._read_native_ccp4(ccp4, 1)
        zimol = self.mol["zmap"]
        if zimol is not None and zimol >= 0:
            # fixed colour (green) so the z-map looks the same on every dataset,
            # rather than Coot's per-map colour rotation (event map = blue).
            try:
                coot.set_map_colour(zimol, 0.0, 0.8, 0.0)
            except Exception:
                pass
            # z-map (pre-scaled): contour at z-score 3.0 absolute (pandda default)
            try:
                coot.set_contour_level_absolute(zimol, 3.0)
            except (ValueError, TypeError):
                pass
            coot.set_map_displayed(zimol, 1 if self.show_zmap else 0)

    def _load_xray(self, missing):
        # 2Fo-Fc + Fo-Fc from the dataset input MTZ; only when the toggle is on
        if not self.show_xray:
            return
        mtz = self._find("%s-pandda-input.mtz" % self.xtal)
        if not mtz:
            missing.append("xray_map")
            return
        res = coot.auto_read_make_and_draw_maps(mtz)
        self.mol["xray"] = list(res) if isinstance(res, (list, tuple)) else [res]
        self._set_map_shown("xray", self.show_xray)

    def _load_average(self, missing):
        # ground-state average map; native .ccp4 needs the same axis/tiling fix
        if not self.show_average:
            return
        ccp4 = self._find("%s-ground-state-average-map.native.ccp4" % self.xtal)
        if ccp4:
            self.mol["average"] = self._read_native_ccp4(ccp4, 0)
        else:
            mtz = self._find("%s-ground-state-average-map.native.mtz" % self.xtal)
            if not mtz:
                missing.append("average_map")
                return
            self.mol["average"] = self._read_mtz_first_map(mtz)
        self._set_map_shown("average", self.show_average)

    def _close_all_molecules(self):
        for i in range(coot.graphics_n_molecules()):
            if coot.is_valid_model_molecule(i) or coot.is_valid_map_molecule(i):
                coot.close_molecule(i)

    # ---- status ----------------------------------------------------------
    def status(self):
        if not self.order:
            return "PanDDA: no folder loaded"
        return "PanDDA %d/%d  %s  event %s  site %s  res %s  (1-BDC %s)" % (
            self.pos + 1, len(self.order), self.xtal, self.event,
            self.site, self.resolution, self.bdc)

    def get_info(self):
        # '|'-joined dataset-info fields for the panel, in a fixed order:
        # crystal, resolution, r_work, r_free, event, site, BDC.
        if not self.order:
            return ""
        fields = [self.xtal, self.resolution, self.r_work, self.r_free,
                  self.event, self.site, self.bdc]
        return "|".join("" if v is None else str(v) for v in fields)

    def get_info_inspect(self):
        # '|'-joined dataset/event fields for the pandda.inspect-style panel:
        # dtag, event, bdc, z_peak, cluster_size, resolution, map_uncertainty,
        # r_free, r_work.
        if not self.order:
            return ""
        fields = [self.xtal, self.event, self.bdc, self.z_peak, self.cluster_size,
                  self.resolution, self.map_uncertainty, self.r_free, self.r_work]
        return "|".join("" if v is None else str(v) for v in fields)

    # ---- HTML inspect summary --------------------------------------------
    def _all_rows(self):
        return list(range(1, len(self.elist))) if self.elist else []

    def _cellval(self, i, key, default=""):
        n = self.col.get(key)
        if n is None:
            return default
        row = self.elist[i]
        return row[n] if n < len(row) else default

    def summary_html(self):
        """Regenerate the HTML report and return its path (for the browser)."""
        return self.write_html()

    def write_html(self):
        """Write analyses/pandda_inspect.html: a Bootstrap/DataTables progress
        report styled like pandda_analyse.html. Returns the path (or a 'PanDDA: '
        error string)."""
        if not self.analysis_folder or not self.elist:
            return self._msg("PanDDA: no folder loaded")
        try:
            import html as _html
            esc = _html.escape
        except Exception:                  # very old pythons
            def esc(s): return (str(s).replace("&", "&amp;")
                                .replace("<", "&lt;").replace(">", "&gt;"))
        rows = self._all_rows()
        total = len(rows)
        viewed = sum(1 for i in rows if self._row(i, "viewed") == "True")
        interesting = sum(1 for i in rows if self._row(i, "interesting") == "True")
        modelled = sum(1 for i in rows if self._is_modelled(i))
        conf_counts = {}
        for i in rows:
            c = self._row(i, "confidence") or "unassigned"
            conf_counts[c] = conf_counts.get(c, 0) + 1
        pct = lambda n: (100.0 * n / total) if total else 0.0

        # per-site aggregation (in numeric site order where possible)
        sk = self.col.get("site")
        site_rows = {}
        for i in rows:
            s = self.elist[i][sk] if (sk is not None and sk < len(self.elist[i])) else "?"
            site_rows.setdefault(s, []).append(i)
        def site_sort_key(s):
            try:
                return (0, float(s))
            except (ValueError, TypeError):
                return (1, str(s))

        H = []
        H.append('<!DOCTYPE html>\n<html lang="en">\n  <head>\n'
                 '    <meta charset="utf-8">\n'
                 '    <meta name="viewport" content="width=device-width, initial-scale=1">\n'
                 '    <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/'
                 'bootstrap/3.3.6/css/bootstrap.min.css">\n'
                 '    <link rel="stylesheet" href="https://cdn.datatables.net/'
                 '1.10.11/css/dataTables.bootstrap.min.css">\n'
                 '    <script src="https://code.jquery.com/jquery-1.12.0.min.js"></script>\n'
                 '    <script src="https://cdn.datatables.net/1.10.11/js/'
                 'jquery.dataTables.min.js"></script>\n'
                 '    <script src="https://cdn.datatables.net/1.10.11/js/'
                 'dataTables.bootstrap.min.js"></script>\n'
                 '    <script type="text/javascript" class="init">\n'
                 '$(document).ready(function() { $(\'#main-table\').DataTable('
                 '{"paging": false}); } );\n'
                 '    </script>\n'
                 '    <title>PANDDA Inspect Summary</title>\n  </head>\n  <body>\n')
        H.append('    <div class="container">\n')
        H.append('      <h1>PANDDA Inspect Summary</h1>\n')
        H.append('      <h4><samp>%s</samp></h4>\n' % esc(self.panddaDir or ""))

        # progress bars
        H.append('      <div class="row"><div class="col-xs-12">\n')
        H.append('        <div class="progress">\n')
        H.append('          <div class="progress-bar progress-bar-success" '
                 'style="width:%.1f%%">Modelled %d</div>\n' % (pct(modelled), modelled))
        H.append('          <div class="progress-bar progress-bar-info" '
                 'style="width:%.1f%%">Viewed %d</div>\n'
                 % (pct(max(0, viewed - modelled)), viewed))
        H.append('        </div>\n      </div></div>\n')

        # headline counts
        def alert(kind, label, n):
            return ('        <div class="col-xs-6 col-sm-3"><div class="alert alert-%s" '
                    'role="alert"><strong>%s: %d</strong></div></div>\n'
                    % (kind, label, n))
        H.append('      <div class="row">\n')
        H.append(alert("info", "Events", total))
        H.append(alert("info", "Viewed", viewed))
        H.append(alert("success", "Interesting", interesting))
        H.append(alert("warning", "Modelled", modelled))
        H.append('      </div>\n')
        if conf_counts:
            H.append('      <div class="row">\n')
            for c in self.CONFIDENCE_VALUES:
                if c in conf_counts:
                    H.append(alert("info", esc(c), conf_counts[c]))
            H.append('      </div>\n')

        # per-site table
        H.append('      <h2>Sites</h2>\n')
        H.append('      <table class="table table-bordered table-striped">\n'
                 '        <thead><tr><th>Site</th><th>Events</th><th>Modelled</th>'
                 '<th>Name</th><th>Comment</th></tr></thead>\n        <tbody>\n')
        for s in sorted(site_rows, key=site_sort_key):
            ev = site_rows[s]
            nmod = sum(1 for i in ev if self._is_modelled(i))
            name = comment = "None"
            if self.slist and "site_idx" in self.scol:
                kk = self.scol["site_idx"]
                for r in self.slist[1:]:
                    if kk < len(r) and r[kk] == str(s):
                        name = r[self.scol["Name"]] if "Name" in self.scol else "None"
                        comment = r[self.scol["Comment"]] if "Comment" in self.scol else "None"
                        break
            H.append('          <tr><td>%s</td><td>%d</td><td>%d</td><td>%s</td>'
                     '<td>%s</td></tr>\n'
                     % (esc(s), len(ev), nmod, esc(name), esc(comment)))
        H.append('        </tbody>\n      </table>\n')

        # per-event DataTable
        H.append('      <h2>Events</h2>\n')
        H.append('      <table id="main-table" class="table table-bordered table-striped">\n'
                 '        <thead><tr><th>Dataset</th><th>Event</th><th>Site</th>'
                 '<th>1-BDC</th><th>Z-peak</th><th>Resolution</th><th>Interesting</th>'
                 '<th>Confidence</th><th>Ligand Placed</th><th>Comment</th>'
                 '<th>Viewed</th></tr></thead>\n        <tbody>\n')
        for i in rows:
            cells = [self._cellval(i, "dtag"), self._cellval(i, "event"),
                     self._cellval(i, "site"), self._cellval(i, "bdc"),
                     self._cellval(i, "z_peak"), self._cellval(i, "resolution"),
                     self._row(i, "interesting") or "False",
                     self._row(i, "confidence") or "unassigned",
                     "True" if self._is_modelled(i) else "False",
                     self._row(i, "comment") or "None",
                     self._row(i, "viewed") or "False"]
            H.append('          <tr>' + "".join("<td>%s</td>" % esc(c) for c in cells)
                     + '</tr>\n')
        H.append('        </tbody>\n      </table>\n')
        H.append('    </div>\n  </body>\n</html>\n')

        path = os.path.join(self.analysis_folder, "pandda_inspect.html")
        try:
            with open(path, "w") as f:
                f.write("".join(H))
        except Exception as e:
            return self._msg("PanDDA: could not write HTML (%s)" % e)
        print("PanDDA: wrote %s" % path)
        return path

    def _msg(self, s):
        print(s)
        # "PanDDA: ..." (with a colon) is an error/notice -> pop a dialog so it
        # isn't lost in occluded stdout. A normal status ("PanDDA N/M ...") won't.
        if s.startswith("PanDDA: "):
            try:
                coot.info_dialog(s)
            except Exception:
                pass
        return s


# ---- module-level singleton + driver functions -----------------------------
_inst = None

def _get():
    global _inst
    if _inst is None:
        _inst = PanddaInspect()
    return _inst

def set_folder(path):
    return _get().set_folder(path)

def next_event():
    return _get().next_event()

def prev_event():
    return _get().prev_event()

def next_site():
    return _get().next_site()

def prev_site():
    return _get().prev_site()

def set_show_emap(on):
    return _get().set_show_emap(on)

def set_show_zmap(on):
    return _get().set_show_zmap(on)

def set_show_xray(on):
    return _get().set_show_xray(on)

def set_show_average(on):
    return _get().set_show_average(on)

def set_selection(criterion):
    return _get().set_selection(criterion)

def get_interesting():
    return _get().get_interesting()

def set_interesting(on):
    return _get().set_interesting(on)

def get_confidence():
    return _get().get_confidence()

def set_confidence(s):
    return _get().set_confidence(s)

def get_comment():
    return _get().get_comment()

def set_comment(s):
    return _get().set_comment(s)

def load_ligand_file(path):
    return _get().load_ligand_file(path)

def ligand_from_smiles(smiles, tlc):
    return _get().ligand_from_smiles(smiles, tlc)

def ligand_from_smiles_standalone(smiles, tlc):
    """Build a 3D ligand from SMILES via acedrg and load it at the current view
    centre, with no PanDDA session required. Backs the 'Ligand from SMILES...'
    button in Other Modelling Tools. Returns a one-line status string."""
    tlc = ((tlc or "").strip() or "LIG")[:3]
    pdb, cif, err = _acedrg_build(smiles, tlc)
    if err:
        return "Ligand from SMILES: " + err
    if cif and os.path.isfile(cif):
        coot.read_cif_dictionary(cif)
    imol = coot.handle_read_draw_molecule_with_recentre(pdb, 0)
    if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
        return ("Ligand from SMILES: acedrg built %s but Coot could not read it"
                % os.path.basename(pdb))
    try:
        coot.set_molecule_bonds_colour_map_rotation(imol,
                                                    PanddaInspect.LIGAND_BOND_COLOUR)
    except Exception:
        pass
    # a freshly-built ligand has no meaningful pose -> drop it at the view centre
    dx = coot.rotation_centre_position(0) - coot.molecule_centre_internal(imol, 0)
    dy = coot.rotation_centre_position(1) - coot.molecule_centre_internal(imol, 1)
    dz = coot.rotation_centre_position(2) - coot.molecule_centre_internal(imol, 2)
    coot.translate_molecule_by(imol, dx, dy, dz)
    coot.graphics_draw()
    return "Ligand from SMILES: built %s as molecule %d" % (tlc, imol)

def place_ligand():
    return _get().place_ligand()

def merge_ligand():
    return _get().merge_ligand()

def save_model():
    return _get().save_model()

def dataset_list():
    return _get().dataset_list()

def go_to_dataset(dtag):
    return _get().go_to_dataset(dtag)

def get_info():
    return _get().get_info()

def get_info_inspect():
    return _get().get_info_inspect()

def status():
    return _get().status()

# ---- pandda.inspect-style UI extras -----------------------------------------
def is_loaded():
    return _get().is_loaded()

def progress():
    return _get().progress()

def get_map_state():
    return _get().get_map_state()

def get_placed():
    return _get().get_placed()

def set_placed(on):
    return _get().set_placed(on)

def next_unviewed():
    return _get().next_unviewed()

def next_modelled():
    return _get().next_modelled()

def open_next_ligand():
    return _get().open_next_ligand()

def open_ligand_here():
    return _get().open_ligand_here()

def reload_saved_model():
    return _get().reload_saved_model()

def reset_to_unfitted():
    return _get().reset_to_unfitted()

def load_unfitted_comparison():
    return _get().load_unfitted_comparison()

def get_site_name():
    return _get().get_site_name()

def set_site_name(s):
    return _get().set_site_name(s)

def get_site_comment():
    return _get().get_site_comment()

def set_site_comment(s):
    return _get().set_site_comment(s)

def write_html():
    return _get().write_html()

def summary_html():
    return _get().summary_html()
