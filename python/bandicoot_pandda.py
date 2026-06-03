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
        self.reset_event()

    def reset_event(self):
        self.xtal = self.event = self.site = self.bdc = None
        self.x = self.y = self.z = None
        self.resolution = self.r_free = self.r_work = None
        self.pdb = self.emap = self.zmap = self.ligcif = None
        self.merged = False
        self.mol = {"protein": None, "emap": None, "zmap": None, "ligand": None,
                    "xray": None, "average": None}

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

        self.order = list(range(1, len(self.elist)))   # all events, file order
        self.pos = 0
        self._ensure_view_setup()
        return self._load_pos()

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
        elif c.startswith("sort alpha") and "dtag" in self.col:
            k = self.col["dtag"]
            rows.sort(key=lambda i: self.elist[i][k])
        elif c.startswith("unviewed"):
            rows = [i for i in rows if self._row(i, "viewed") != "True"]
        elif c.startswith("interesting"):
            rows = [i for i in rows if self._row(i, "interesting") == "True"]
        elif c.startswith("not modelled"):
            rows = [i for i in rows if not self._is_modelled(i)]
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
        sites = []                              # unique site ids, in view order
        for ri in self.order:
            s = self.elist[ri][k]
            if s not in sites:
                sites.append(s)
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
        """Read the ligand dictionary + coords; return its imol (or None)."""
        cif = self._find_ligand_cif()
        if not cif:
            return None
        pdb = cif[:-4] + ".pdb" if cif.endswith(".cif") else None
        if not pdb or not os.path.isfile(pdb):
            return None
        self.ligcif = cif
        coot.read_cif_dictionary(cif)
        imol = coot.handle_read_draw_molecule_with_recentre(pdb, 0)
        self.mol["ligand"] = imol
        self._set_ligand_occupancy(imol)
        return imol

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
        imol = coot.handle_read_draw_molecule_with_recentre(path, 0)
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            return self._msg("PanDDA: no coordinates read from %s" % os.path.basename(path))
        self.mol["ligand"] = imol
        self._set_ligand_occupancy(imol)
        coot.graphics_draw()
        return self.status()

    def ligand_from_smiles(self, smiles, tlc):
        """Build a 3D ligand from a SMILES string by running acedrg directly.

        Coot's own new_molecule_by_smiles_string goes through acedrg_env(), which
        does os.environ['CBIN'] (KeyError when CCP4 vars aren't exported) and uses
        a ';' PATH separator -- so we bypass it. The SBGrid acedrg wrapper sets up
        its own CCP4 environment; we only need it on PATH."""
        if not self.elist:
            return self._msg("PanDDA: load a PanDDA folder and pick an event first")
        smiles = (smiles or "").strip()
        tlc = ((tlc or "").strip() or "LIG")[:3]
        if not smiles:
            return self._msg("PanDDA: empty SMILES string")
        import subprocess
        try:
            from shutil import which
        except Exception:
            which = None
        acedrg = (which("acedrg") if which else None)
        if not acedrg:
            return self._msg("PanDDA: acedrg not on PATH "
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
            return self._msg("PanDDA: could not run acedrg (%s)" % e)
        pdb, cif = stub + ".pdb", stub + ".cif"
        if proc.returncode != 0 or not os.path.isfile(pdb):
            if proc.stdout:
                print("acedrg output (tail):\n"
                      + proc.stdout.decode("utf-8", "replace")[-500:])
            return self._msg("PanDDA: acedrg could not build %s from that SMILES "
                             "(see stdout)" % tlc)
        if os.path.isfile(cif):
            coot.read_cif_dictionary(cif)
        imol = coot.handle_read_draw_molecule_with_recentre(pdb, 0)
        if imol is None or imol < 0 or not coot.is_valid_model_molecule(imol):
            return self._msg("PanDDA: acedrg built %s but Coot could not read it"
                             % os.path.basename(pdb))
        self.mol["ligand"] = imol
        self._set_ligand_occupancy(imol)
        # a freshly-built ligand has no meaningful pose -> drop it on the event
        self._centre_molecule_on_view(imol)
        coot.graphics_draw()
        return self.status()

    def place_ligand(self):
        if not self.elist:
            return self._msg("PanDDA: no folder loaded")
        imol = self.mol.get("ligand")
        if imol is None or imol < 0:
            imol = self._load_ligand()              # lazy-load on first Place
            if imol is None or imol < 0:
                return self._msg("PanDDA: no ligand file (ligand_files/*.cif) for %s" % self.xtal)
        self._centre_molecule_on_view(imol)
        coot.graphics_draw()
        return self.status()

    def merge_ligand(self):
        lig = self.mol.get("ligand")
        prot = self.mol.get("protein")
        if lig is None or lig < 0:
            return self._msg("PanDDA: no ligand to merge (Place Ligand first)")
        if prot is None or prot < 0:
            return self._msg("PanDDA: no model loaded to merge into")
        coot.merge_molecules_py([lig], prot)
        coot.close_molecule(lig)
        self.mol["ligand"] = None
        self.merged = True
        coot.graphics_draw()
        return self.status()

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
        self.mol["protein"] = coot.handle_read_draw_molecule_with_recentre(pdb, 0)

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

def status():
    return _get().status()
