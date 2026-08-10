// gemmi-mmdb-diff -- does reading a coordinate file through gemmi produce the
// same mmdb model as reading it through mmdb?
//
// Loads each file TWICE and diffs the resulting mmdb models:
//
//   path A  Bandicoot's reader: get_atom_selection() -- the real one, linked
//           from libcoot-coord-utils, so the four post-read fix-ups and the
//           #9 _struct_ncs_oper retry are all in play.
//   path B  gemmi::read_structure_file -> gemmi::copy_to_mmdb.
//
// The comparison is between two in-memory mmdb::Manager models, not between
// two files: everything downstream of get_atom_selection() sees only that
// model, so if the models agree, nothing downstream can tell the paths apart.
//
// Written for the v0.2 Phase 1 gate (see
// ~/sw/bandicoot-project/notes/v0.2-gemmi-notes/ for that report), and kept
// because it re-earns its cost every time gemmi moves: re-run it after a gemmi
// upgrade and the diff tells you immediately whether the upgrade changed the
// model we hand to the rest of Coot. NOT part of the build -- it links against
// an installed Bandicoot; see build.sh next to this file.
//
// Exit status: 0 if every file compared identical, 1 otherwise.

#include <gemmi/mmread.hpp>    // read_structure_file
#include <gemmi/mmdb.hpp>      // copy_to_mmdb
#include <gemmi/polyheur.hpp>  // setup_entities (free function in 0.7.x)

#include <mmdb2/mmdb_manager.h>

#include "coot-utils/atom-selection-container.hh"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------- tolerances
// Coordinates are written to 3 d.p. in both PDB and mmCIF, so anything at or
// below half an ulp of that is formatting, not a difference.
static const double TOL_XYZ    = 5e-4;
static const double TOL_OCC    = 5e-3;
static const double TOL_B      = 5e-3;
static const double TOL_CHARGE = 5e-3;
static const double TOL_CELL   = 1e-3;

// ------------------------------------------------------------------- helpers
static std::string trim(const std::string &s) {
   size_t b = s.find_first_not_of(" \t");
   if (b == std::string::npos) return "";
   size_t e = s.find_last_not_of(" \t");
   return s.substr(b, e - b + 1);
}

static std::string quote(const std::string &s) { return "\"" + s + "\""; }

static std::string fnum(double v) {
   char buf[64];
   snprintf(buf, sizeof buf, "%.4f", v);
   return std::string(buf);
}

// One atom, flattened out of the mmdb hierarchy.
struct AtomRec {
   int model = 0;
   std::string chain, inscode, resname, name_raw, altloc_raw, element_raw, segid;
   int seqnum = 0;
   double x = 0, y = 0, z = 0, occ = 0, b = 0, charge = 0;
   bool het = false;
};

// Model-wide facts that are not per-atom.
struct ModelSummary {
   bool ok = false;
   std::string error;
   long n_real = 0;       // atoms excluding TER
   long n_ter = 0;        // TER pseudo-atoms (mmdb stores these in the atom table)
   int n_models = 0;
   int n_links = 0;
   // (model number, chain id) in file order. Model number is part of the entry
   // because the same id legitimately recurs across models in an NMR ensemble;
   // only a repeat WITHIN one model is a duplicate.
   std::vector<std::pair<int, std::string> > chains;
   std::string spacegroup;
   bool have_cell = false;
   double a = 0, b = 0, c = 0, alpha = 0, beta = 0, gamma = 0;
   std::map<std::string, AtomRec> atoms;   // keyed as below
   // Atoms reachable via Model::GetChain(chainID) -- the by-name lookup. When
   // a model holds two chains with the same id, that lookup returns only the
   // first, so this falls below n_real and the shortfall is the number of
   // atoms invisible to any by-name chain lookup.
   long n_reachable_by_id = 0;
   size_t n_hydrog_dropped = 0;   // Hydrog connections kept out of the LINK table
};

// Key deliberately uses the TRIMMED atom name and altloc: mmdb and gemmi are
// documented to pad differently (gemmi pads from the left, mmdb from both
// sides), and if padding were part of the key every atom would come out
// unmatched and the real per-field differences would be invisible. Padding is
// compared as a value instead. The trailing occurrence index disambiguates the
// genuinely duplicate keys some files contain.
static std::string atom_key(const AtomRec &r, int occurrence) {
   std::ostringstream o;
   o << r.model << "/" << r.chain << "/" << r.seqnum << r.inscode << "/"
     << trim(r.name_raw) << "/" << trim(r.altloc_raw);
   if (occurrence) o << "#" << occurrence;
   return o.str();
}

static void harvest(mmdb::Manager *mol, ModelSummary &s) {
   s.n_models = mol->GetNumberOfModels();

   char *sg = mol->GetSpaceGroup();
   if (sg) s.spacegroup = trim(sg);

   mmdb::realtype cell_a, cell_b, cell_c, al, be, ga, vol;
   int ncode = 0;
   // NOTE the return convention: mmdb's GetCell returns 1 when a cell IS set and
   // 0 when it is not -- the opposite of the usual 0-is-success. Getting this
   // backwards reports every cell-less file as having a cell and vice versa.
   if (mol->GetCell(cell_a, cell_b, cell_c, al, be, ga, vol, ncode) != 0) {
      s.have_cell = true;
      s.a = cell_a; s.b = cell_b; s.c = cell_c;
      s.alpha = al; s.beta = be; s.gamma = ga;
   }

   std::map<std::string, int> seen;   // key -> times already used

   for (int imod = 1; imod <= s.n_models; imod++) {
      mmdb::Model *model = mol->GetModel(imod);
      if (!model) continue;           // mmdb permits gaps in model numbering
      s.n_links += model->GetNumberOfLinks();

      int nchains = model->GetNumberOfChains();
      for (int ich = 0; ich < nchains; ich++) {
         mmdb::Chain *chain = model->GetChain(ich);
         if (!chain) continue;
         s.chains.push_back(std::make_pair(
            imod, chain->GetChainID() ? std::string(chain->GetChainID()) : std::string()));

         int nres = chain->GetNumberOfResidues();
         for (int ires = 0; ires < nres; ires++) {
            mmdb::Residue *res = chain->GetResidue(ires);
            if (!res) continue;

            int natoms = res->GetNumberOfAtoms();
            for (int iat = 0; iat < natoms; iat++) {
               mmdb::Atom *at = res->GetAtom(iat);
               if (!at) continue;

               // TER entries live in mmdb's atom table but are not atoms.
               // Counting them makes every polymer chain look one atom long.
               if (at->isTer()) { s.n_ter++; continue; }

               AtomRec r;
               r.model       = imod;
               r.chain       = chain->GetChainID() ? chain->GetChainID() : "";
               r.seqnum      = res->GetSeqNum();
               r.inscode     = res->GetInsCode() ? res->GetInsCode() : "";
               r.resname     = res->GetResName() ? res->GetResName() : "";
               r.name_raw    = at->name;
               r.altloc_raw  = at->altLoc;
               r.element_raw = at->element;
               r.segid       = at->segID;
               r.x = at->x; r.y = at->y; r.z = at->z;
               r.occ = at->occupancy; r.b = at->tempFactor; r.charge = at->charge;
               r.het = at->Het;

               std::string base = atom_key(r, 0);
               int occ_index = seen[base]++;
               s.atoms[atom_key(r, occ_index)] = r;
               s.n_real++;
            }
         }
      }

      // By-name reachability, per model: walk the DISTINCT ids and count what
      // GetChain(id) can actually see.
      std::set<std::string> ids;
      for (int ich = 0; ich < nchains; ich++) {
         mmdb::Chain *chain = model->GetChain(ich);
         if (chain && chain->GetChainID()) ids.insert(chain->GetChainID());
      }
      for (const std::string &id : ids) {
         mmdb::Chain *chain = model->GetChain(id.c_str());
         if (!chain) continue;
         int nres = chain->GetNumberOfResidues();
         for (int ires = 0; ires < nres; ires++) {
            mmdb::Residue *res = chain->GetResidue(ires);
            if (!res) continue;
            int natoms = res->GetNumberOfAtoms();
            for (int iat = 0; iat < natoms; iat++) {
               mmdb::Atom *at = res->GetAtom(iat);
               if (at && !at->isTer()) s.n_reachable_by_id++;
            }
         }
      }
   }
   // A reader that returns an empty model has not succeeded in any useful
   // sense, and treating it as success is actively misleading: gemmi's
   // mmCIF reader accepts a small-molecule CIF and yields ZERO atoms without
   // throwing, which would otherwise show up here as a clean pass.
   if (s.n_real == 0) {
      s.ok = false;
      s.error = "read returned a model with 0 atoms";
      return;
   }
   s.ok = true;
}

// Print each chain's composition, so a duplicated id can be read as "polymer /
// ligand / water" rather than just a repeated letter.
static void dump_chains(const char *label, mmdb::Manager *mol) {
   printf("    chain composition, %s:\n", label);
   for (int imod = 1; imod <= mol->GetNumberOfModels(); imod++) {
      mmdb::Model *model = mol->GetModel(imod);
      if (!model) continue;
      if (mol->GetNumberOfModels() > 1 && imod > 1) break;   // model 1 is enough
      for (int ich = 0; ich < model->GetNumberOfChains(); ich++) {
         mmdb::Chain *chain = model->GetChain(ich);
         if (!chain) continue;
         int nres = chain->GetNumberOfResidues();
         std::map<std::string, int> kinds;
         long natoms = 0;
         for (int ires = 0; ires < nres; ires++) {
            mmdb::Residue *res = chain->GetResidue(ires);
            if (!res) continue;
            kinds[res->GetResName() ? res->GetResName() : ""]++;
            for (int iat = 0; iat < res->GetNumberOfAtoms(); iat++) {
               mmdb::Atom *at = res->GetAtom(iat);
               if (at && !at->isTer()) natoms++;
            }
         }
         std::string summary;
         int shown = 0;
         for (const auto &kv : kinds) {
            if (shown++ >= 4) { summary += "..."; break; }
            summary += kv.first + "x" + std::to_string(kv.second) + " ";
         }
         printf("      [%2d] id=%-3s %5d res %7ld atoms  %s\n", ich,
                quote(chain->GetChainID() ? chain->GetChainID() : "").c_str(),
                nres, natoms, summary.c_str());
      }
   }
}

// ------------------------------------------------------------- the two paths
static bool g_dump_chains = false;

static ModelSummary load_path_a(const std::string &path) {
   ModelSummary s;
   // Same arguments the GUI load uses (src/c-interface.cc:961), except verbose
   // off to keep the report readable: allow_duplseqnum=true,
   // convert_to_v2_name_flag=false.
   atom_selection_container_t asc = get_atom_selection(path, true, false, false);
   if (!asc.read_success || !asc.mol) {
      s.error = "get_atom_selection failed: " + asc.read_error_message;
      return s;
   }
   if (g_dump_chains) dump_chains("A (mmdb reader)", asc.mol);
   harvest(asc.mol, s);
   return s;
}

static ModelSummary load_path_b(const std::string &path, bool setup_entities,
                                bool merge_chains, bool drop_hydrog_links) {
   ModelSummary s;
   mmdb::Manager *mol = nullptr;
   try {
      gemmi::Structure st = gemmi::read_structure_file(path);
      if (setup_entities) gemmi::setup_entities(st);

      // copy_to_mmdb() does CreateChain() per gemmi::Chain and never merges, so
      // a file where one author chain holds polymer + ligands + waters becomes
      // several mmdb chains sharing an id -- and mmdb's GetChain(id) finds only
      // the first, hiding the rest from every by-name lookup (12% of the atoms
      // in 3nyd). Merging the parts first is gemmi's own remedy: with the
      // default min_sep=0 it concatenates residues WITHOUT renumbering
      // (model.hpp:1080), and copy_from_mmdb already applies it in the reverse
      // direction (mmdb.hpp:460). Must run AFTER setup_entities, which is what
      // creates the split in the first place.
      if (merge_chains) st.merge_chain_parts();

      // struct_conn policy (Art, 2026-08-10): transfer real covalent
      // connectivity into mmdb's LINK table, but NOT hydrogen bonds.
      // gemmi's transfer_links_to_mmdb (mmdb.hpp:71) copies every connection
      // and ignores con.type, and Coot's fill_links()
      // (ideal/link-restraints.cc:39) then hands every LINK to the refinement
      // with no filtering of its own. A phenix-refined file can carry hundreds
      // of Hydrog connections (660 in SC1_2_refine_036.cif), and an H-bond at
      // ~2.9 A must not become a link restraint pulling toward ~1.4 A.
      // Bandicoot's RSR deliberately does not restrain H-bonds: helix/sheet
      // networks are covered by secondary-structure restraints, and a user who
      // wants a specific one can add it with "Make Link".
      // Fidelity is not lost by this: Phase 3's verbatim passthrough preserves
      // the whole struct_conn category, Hydrog rows included, on write.
      if (drop_hydrog_links) {
         size_t before = st.connections.size();
         std::vector<gemmi::Connection> keep;
         keep.reserve(before);
         for (const gemmi::Connection &con : st.connections)
            if (con.type != gemmi::Connection::Hydrog)
               keep.push_back(con);
         s.n_hydrog_dropped = before - keep.size();
         st.connections = std::move(keep);
      }

      mol = new mmdb::Manager();
      gemmi::copy_to_mmdb(st, mol);
   } catch (const std::exception &e) {
      s.error = std::string("gemmi threw: ") + e.what();
      delete mol;
      return s;
    }
   if (g_dump_chains) dump_chains("B (gemmi->copy_to_mmdb)", mol);
   harvest(mol, s);
   delete mol;
   return s;
}

// ---------------------------------------------------------------- the report
// Per-field difference tally, remembering the first few concrete examples so
// the report says WHAT differs, not just how often. Insertion order preserved
// so the output reads in a stable order.
class DiffTally {
public:
   static size_t max_examples;   // --examples N
   void note(const std::string &field, const std::string &key,
             const std::string &av, const std::string &bv) {
      if (!counts.count(field)) order.push_back(field);
      counts[field]++;
      // Also tally the distinct A-vs-B value pairs: one line per *kind* of
      // difference is what tells you whether a class is one quirk or a rule.
      shapes[field][av + " -> " + bv]++;   // call sites already quote strings
      if (examples[field].size() < max_examples)
         examples[field].push_back(key + ": A=" + av + " B=" + bv);
   }
   bool empty() const { return order.empty(); }
   void print() const {
      for (const std::string &f : order) {
         printf("      %-22s %8ld\n", f.c_str(), counts.at(f));
         // Distinct value-pairs first: 60 differences all of one shape is a
         // rule; 60 differences of 40 shapes is something else entirely.
         const std::map<std::string, long> &sh = shapes.at(f);
         printf("        %zu distinct shape(s):\n", sh.size());
         size_t shown = 0;
         for (const auto &kv : sh) {
            if (shown++ >= max_examples) {
               printf("          ... and %zu more shape(s)\n", sh.size() - shown + 1);
               break;
            }
            printf("          %-28s x%ld\n", kv.first.c_str(), kv.second);
         }
         for (const std::string &ex : examples.at(f))
            printf("        e.g. %s\n", ex.c_str());
      }
   }
private:
   std::vector<std::string> order;
   std::map<std::string, long> counts;
   std::map<std::string, std::vector<std::string> > examples;
   std::map<std::string, std::map<std::string, long> > shapes;
};

size_t DiffTally::max_examples = 3;

static void print_sample_keys(const char *label,
                              const std::vector<std::string> &keys) {
   printf("    %s: %zu\n", label, keys.size());
   size_t n = keys.size() < 5 ? keys.size() : 5;
   for (size_t i = 0; i < n; i++) printf("        %s\n", keys[i].c_str());
   if (keys.size() > n) printf("        ... and %zu more\n", keys.size() - n);
}

// Returns true if the two models are identical within tolerance.
static bool compare(const ModelSummary &A, const ModelSummary &B) {
   bool clean = true;

   printf("    counts    A: %ld atoms (+%ld TER), %d model(s), %zu chain(s), %d link(s)\n",
          A.n_real, A.n_ter, A.n_models, A.chains.size(), A.n_links);
   printf("              B: %ld atoms (+%ld TER), %d model(s), %zu chain(s), %d link(s)\n",
          B.n_real, B.n_ter, B.n_models, B.chains.size(), B.n_links);

   if (A.n_reachable_by_id != A.n_real || B.n_reachable_by_id != B.n_real)
      printf("    reachable via GetChain(id)  A: %ld/%ld   B: %ld/%ld  <-- shortfall = "
             "atoms hidden from by-name chain lookup\n",
             A.n_reachable_by_id, A.n_real, B.n_reachable_by_id, B.n_real);

   if (A.n_real != B.n_real)   { clean = false; printf("    DIFF atom count\n"); }
   if (A.n_models != B.n_models) { clean = false; printf("    DIFF model count\n"); }
   if (A.n_links != B.n_links) { clean = false; printf("    DIFF link count\n"); }

   // Chain comparison, split into the three questions that have different
   // consequences: is the SET of ids the same (a missing chain is fatal), does
   // either side repeat an id (duplicates break GetChain(id) lookups), and is
   // the ORDER the same (cosmetic -- affects chain ordering in the UI).
   {
      // Multiplicity keyed by (model, id): a repeat within one model.
      std::map<std::pair<int, std::string>, int> ca, cb;
      for (const auto &c : A.chains) ca[c]++;
      for (const auto &c : B.chains) cb[c]++;

      std::set<std::pair<int, std::string> > sa, sb;
      for (const auto &kv : ca) sa.insert(kv.first);
      for (const auto &kv : cb) sb.insert(kv.first);
      if (sa != sb) {
         clean = false;
         std::string only_a, only_b;
         for (const auto &c : sa)
            if (!sb.count(c)) only_a += "model " + std::to_string(c.first) + ":" + quote(c.second) + " ";
         for (const auto &c : sb)
            if (!sa.count(c)) only_b += "model " + std::to_string(c.first) + ":" + quote(c.second) + " ";
         printf("    DIFF chain id set\n");
         if (!only_a.empty()) printf("      only in A: %s\n", only_a.c_str());
         if (!only_b.empty()) printf("      only in B: %s\n", only_b.c_str());
      }

      for (int pass = 0; pass < 2; pass++) {
         const std::map<std::pair<int, std::string>, int> &m = pass ? cb : ca;
         std::string dups;
         for (const auto &kv : m)
            if (kv.second > 1) {
               char buf[96];
               snprintf(buf, sizeof buf, "model %d:%s x%d ", kv.first.first,
                        quote(kv.first.second).c_str(), kv.second);
               dups += buf;
            }
         if (!dups.empty()) {
            clean = false;
            printf("    DUPLICATE chain ids within a model, in %s: %s\n",
                   pass ? "B" : "A", dups.c_str());
         }
      }

      if (ca == cb && A.chains != B.chains) {
         clean = false;
         std::string oa, ob;
         for (const auto &c : A.chains) oa += quote(c.second) + " ";
         for (const auto &c : B.chains) ob += quote(c.second) + " ";
         printf("    DIFF chain ORDER only (same ids, same multiplicities)\n"
                "      A: %s\n      B: %s\n", oa.c_str(), ob.c_str());
      }
   }

   if (A.spacegroup != B.spacegroup) {
      clean = false;
      printf("    DIFF spacegroup  A=%s B=%s\n",
             quote(A.spacegroup).c_str(), quote(B.spacegroup).c_str());
   }
   if (A.have_cell != B.have_cell) {
      clean = false;
      printf("    DIFF cell present  A=%d B=%d\n", (int)A.have_cell, (int)B.have_cell);
   } else if (A.have_cell) {
      if (fabs(A.a - B.a) > TOL_CELL || fabs(A.b - B.b) > TOL_CELL ||
          fabs(A.c - B.c) > TOL_CELL || fabs(A.alpha - B.alpha) > TOL_CELL ||
          fabs(A.beta - B.beta) > TOL_CELL || fabs(A.gamma - B.gamma) > TOL_CELL) {
         clean = false;
         printf("    DIFF cell\n      A: %s %s %s  %s %s %s\n      B: %s %s %s  %s %s %s\n",
                fnum(A.a).c_str(), fnum(A.b).c_str(), fnum(A.c).c_str(),
                fnum(A.alpha).c_str(), fnum(A.beta).c_str(), fnum(A.gamma).c_str(),
                fnum(B.a).c_str(), fnum(B.b).c_str(), fnum(B.c).c_str(),
                fnum(B.alpha).c_str(), fnum(B.beta).c_str(), fnum(B.gamma).c_str());
      }
   }

   // Match atoms by key rather than by index: a single ordering difference
   // would otherwise report every atom as changed.
   std::vector<std::string> only_a, only_b;
   long matched = 0;
   DiffTally tally;

   for (const auto &kv : A.atoms) {
      auto it = B.atoms.find(kv.first);
      if (it == B.atoms.end()) { only_a.push_back(kv.first); continue; }
      matched++;
      const AtomRec &a = kv.second;
      const AtomRec &b = it->second;
      const std::string &k = kv.first;

      if (a.resname != b.resname)
         tally.note("residue name", k, quote(a.resname), quote(b.resname));
      if (a.name_raw != b.name_raw)
         tally.note("atom name padding", k, quote(a.name_raw), quote(b.name_raw));
      if (a.altloc_raw != b.altloc_raw)
         tally.note("altLoc", k, quote(a.altloc_raw), quote(b.altloc_raw));
      if (a.element_raw != b.element_raw)
         tally.note("element", k, quote(a.element_raw), quote(b.element_raw));
      if (a.segid != b.segid)
         tally.note("segID", k, quote(a.segid), quote(b.segid));
      if (a.het != b.het)
         tally.note("het flag", k, a.het ? "true" : "false", b.het ? "true" : "false");
      if (fabs(a.x - b.x) > TOL_XYZ || fabs(a.y - b.y) > TOL_XYZ ||
          fabs(a.z - b.z) > TOL_XYZ)
         tally.note("coordinates", k,
                    fnum(a.x) + "," + fnum(a.y) + "," + fnum(a.z),
                    fnum(b.x) + "," + fnum(b.y) + "," + fnum(b.z));
      if (fabs(a.occ - b.occ) > TOL_OCC)
         tally.note("occupancy", k, fnum(a.occ), fnum(b.occ));
      if (fabs(a.b - b.b) > TOL_B)
         tally.note("B factor", k, fnum(a.b), fnum(b.b));
      if (fabs(a.charge - b.charge) > TOL_CHARGE)
         tally.note("charge", k, fnum(a.charge), fnum(b.charge));
   }
   for (const auto &kv : B.atoms)
      if (!A.atoms.count(kv.first)) only_b.push_back(kv.first);

   if (B.n_hydrog_dropped)
      printf("    dropped %zu Hydrog connection(s) from B's LINK table (preserved on write)\n",
             B.n_hydrog_dropped);
   printf("    matched   %ld atoms\n", matched);
   if (!only_a.empty()) { clean = false; print_sample_keys("only in A", only_a); }
   if (!only_b.empty()) { clean = false; print_sample_keys("only in B", only_b); }

   if (!tally.empty()) {
      clean = false;
      printf("    field differences among matched atoms:\n");
      tally.print();
   }
   return clean;
}

int main(int argc, char **argv) {
   bool setup_entities = true;
   bool merge_chains = true;
   bool drop_hydrog_links = true;
   std::vector<std::string> files;
   for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--no-setup-entities") setup_entities = false;
      else if (a == "--no-merge-chains") merge_chains = false;
      else if (a == "--keep-hydrog-links") drop_hydrog_links = false;
      else if (a == "--dump-chains") g_dump_chains = true;
      else if (a == "--examples" && i + 1 < argc) DiffTally::max_examples = atoi(argv[++i]);
      else files.push_back(a);
   }
   if (files.empty()) {
      fprintf(stderr, "usage: gemmi-mmdb-diff [--dump-chains] [--examples N]\n"
                      "                      [--no-setup-entities] [--no-merge-chains]\n"
                      "                      [--keep-hydrog-links]\n"
                      "                      <coord-file> ...\n");
      return 2;
   }

   printf("read-side diff: mmdb reader (A) vs gemmi->copy_to_mmdb (B)\n");
   printf("path B: setup_entities %s, merge_chain_parts %s, drop Hydrog links %s\n\n",
          setup_entities ? "on" : "off", merge_chains ? "on" : "off",
          drop_hydrog_links ? "on" : "off");

   int n_clean = 0, n_dirty = 0, n_failed = 0;
   for (const std::string &f : files) {
      printf("=== %s\n", f.c_str());
      ModelSummary A = load_path_a(f);
      ModelSummary B = load_path_b(f, setup_entities, merge_chains, drop_hydrog_links);
      if (!A.ok || !B.ok) {
         n_failed++;
         if (!A.ok) printf("    path A FAILED: %s\n", A.error.c_str());
         if (!B.ok) printf("    path B FAILED: %s\n", B.error.c_str());
         printf("\n");
         continue;
      }
      bool clean = compare(A, B);
      printf("    ==> %s\n\n", clean ? "IDENTICAL" : "DIFFERENCES");
      if (clean) n_clean++; else n_dirty++;
   }

   printf("summary: %d identical, %d with differences, %d failed to load\n",
          n_clean, n_dirty, n_failed);
   return n_dirty || n_failed ? 1 : 0;
}
