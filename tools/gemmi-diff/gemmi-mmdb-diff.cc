// gemmi-mmdb-diff -- does reading a coordinate file through gemmi produce the
// same mmdb model as reading it through mmdb?
//
// Loads each file TWICE and diffs the resulting mmdb models:
//
//   path A  the RAW mmdb reader (ReadCoorFile + PDBCleanup). Deliberately not
//           get_atom_selection(): since Phase 2 that routes mmCIF through gemmi,
//           so using it would compare gemmi against gemmi.
//   path B  the PRODUCTION gemmi reader, coot::read_coords_with_gemmi() -- the
//           same function the application uses, not a copy of it.
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
#include "coot-utils/gemmi-coords.hh"   // the production gemmi read path
#include "coot-utils/gemmi-header.hh"   // pdb_header_records_from_mmcif -- adjustment (9)
#include "coot-utils/gemmi-write.hh"    // the production gemmi write path
#include "coot-utils/mmcif-document.hh" // the retained document
#include "coot-utils/coot-coord-utils.hh"   // normalise_link_blank_fields()
#include "coords/mmdb.h"       // write_atom_selection_file -- the PRODUCTION writer

#include <gemmi/read_cif.hpp>   // read_cif_gz, for the write-side re-read
#include <gemmi/mmread_gz.hpp> // read_structure_gz -- header-check reads the .cif itself

#include <dirent.h>            // corpus enumeration (--corpus / the default run)
#include <sys/stat.h>          // mkdir, for the write-side output dir

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <fstream>
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
   // Anisotropic ADPs, and CRUCIALLY the flag mmdb consumers test.
   //
   // Added 2026-08-20 because the ANISOU defect got past every gate: gemmi's
   // copy_to_mmdb set ASET_Anis_tFSigma (the flag for anisotropic SIGMAS) where
   // mmdb's convention for the tensor is ASET_Anis_tFac, and gemmi read the same
   // flag back, so it was SELF-CONSISTENT. File-level passthrough was perfect and
   // 1518 ADPs were invisible to every mmdb and Coot consumer. Comparing the
   // VALUES alone would not have caught it either -- the flag is the assertion.
   bool aniso_tfac = false;    // ASET_Anis_tFac  -- what consumers test
   bool aniso_sigma = false;   // ASET_Anis_tFSigma -- the sigmas, rarely set
   double u[6] = {0,0,0,0,0,0}; // u11 u22 u33 u12 u13 u23
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
   // LINK records whose BOTH partners resolve to a real atom using the matcher
   // Coot's bond drawing actually uses. A link that sits in the table but
   // resolves to nothing is silently inert: no bond drawn, no restraint made.
   // Counting links is not enough -- two separate bugs (unpadded atom names from
   // gemmi, blank-as-space insCode/altLoc from mmdb's CIF writer) both left the
   // count at 130 and the resolved count at 0.
   int n_links_resolved = 0;
   // Resolution, via mmdb's GetResolution(). Added 2026-08-20: mmdb's own
   // _refine.ls_d_res_high reader WORKS, so losing it on the gemmi path was a
   // genuine regression (1.0 -> -2.0) that adjustment (6) exists to fix -- and
   // this differential never checked it. NOTE GetResolution() CACHES and scans
   // the REMARK container, giving up at the first remark numbered above 2.
   double resolution = 0.0;
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

// Replicates Bond_lines_container::add_link_bond_templ (coords/Bond_lines.cc)
// EXACTLY, including its exact std::string == comparisons on insCode, atom name
// and altLoc. That strictness is the point: mmdb's own Residue::GetAtom() pads
// and trims internally and is tolerant, so checking with it reports links as
// fine when Coot cannot use them. Verify with the CONSUMER's matcher.
static bool link_partner_resolves(mmdb::Model *model_p, const char *chainID,
                                  int seqNum, const char *insCode,
                                  const char *atName, const char *aloc) {
   for (int ich = 0; ich < model_p->GetNumberOfChains(); ich++) {
      mmdb::Chain *chain_p = model_p->GetChain(ich);
      if (!chain_p) continue;
      if (std::string(chain_p->GetChainID()) != std::string(chainID)) continue;
      for (int ir = 0; ir < chain_p->GetNumberOfResidues(); ir++) {
         mmdb::Residue *res_p = chain_p->GetResidue(ir);
         if (!res_p) continue;
         if (res_p->GetSeqNum() != seqNum) continue;
         if (std::string(res_p->GetInsCode()) != std::string(insCode)) continue;
         for (int iat = 0; iat < res_p->GetNumberOfAtoms(); iat++) {
            mmdb::Atom *at = res_p->GetAtom(iat);
            if (!at || at->isTer()) continue;
            if (std::string(at->name) != std::string(atName)) continue;
            if (std::string(at->altLoc) != std::string(aloc)) continue;
            return true;
         }
      }
   }
   return false;
}

static void harvest(mmdb::Manager *mol, ModelSummary &s) {
   s.n_models = mol->GetNumberOfModels();

   char *sg = mol->GetSpaceGroup();
   if (sg) s.spacegroup = trim(sg);

   // Resolution. GetResolution() returns a negative value when it has nothing
   // (-1.0 after a failed REMARK scan, -2.0 unset), so the comparison below
   // treats "has a resolution" and "the value" as separate questions.
   s.resolution = mol->GetResolution();

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
      int n_links_here = model->GetNumberOfLinks();
      s.n_links += n_links_here;
      for (int ilink = 1; ilink <= n_links_here; ilink++) {
         mmdb::Link *link = model->GetLink(ilink);
         if (!link) continue;
         if (link_partner_resolves(model, link->chainID1, link->seqNum1,
                                   link->insCode1, link->atName1, link->aloc1) &&
             link_partner_resolves(model, link->chainID2, link->seqNum2,
                                   link->insCode2, link->atName2, link->aloc2))
            s.n_links_resolved++;
      }

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
               r.aniso_tfac  = (at->WhatIsSet & mmdb::ASET_Anis_tFac)   != 0;
               r.aniso_sigma = (at->WhatIsSet & mmdb::ASET_Anis_tFSigma) != 0;
               r.u[0] = at->u11; r.u[1] = at->u22; r.u[2] = at->u33;
               r.u[3] = at->u12; r.u[4] = at->u13; r.u[5] = at->u23;

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
   // Path A is the RAW MMDB READER, deliberately not get_atom_selection().
   //
   // Since Phase 2, get_atom_selection() routes mmCIF through gemmi -- so using
   // it here would compare gemmi against gemmi and the diff would be
   // meaningless. Reading with mmdb directly keeps this tool answering the
   // question it exists to answer.
   //
   // Flags are the ones get_atom_selection uses. The post-read fix-ups it also
   // applies are NOT replicated: atom_name_fix_ups() is file-static and
   // unreachable, and Phase 1 established that the fix-ups change nothing that
   // this comparison measures (gemmi with none of them equalled mmdb with all
   // of them across the corpus). PDBCleanup is kept because it assigns elements.
   mmdb::Manager *mol = new mmdb::Manager;
   mol->SetFlag(mmdb::MMDBF_IgnoreBlankLines | mmdb::MMDBF_IgnoreNonCoorPDBErrors |
                mmdb::MMDBF_IgnoreHash | mmdb::MMDBF_IgnoreRemarks |
                mmdb::MMDBF_IgnoreDuplSeqNum);
   if (mol->ReadCoorFile(path.c_str()) != 0) {
      s.error = "mmdb ReadCoorFile failed";
      delete mol;
      return s;
   }
   mol->PDBCleanup(mmdb::PDBCLEAN_ELEMENT);

   // ...with ONE exception to "no fix-ups": normalise_link_blank_fields() IS
   // reachable (unlike file-static atom_name_fix_ups) and IS part of the
   // production mmdb path, and it is the difference between every LINK record
   // resolving and none of them resolving on an mmdb-written mmCIF. Leaving it
   // out would make the link post-condition below report an already-fixed bug as
   // broken on every such file -- a false alarm is worse than no check.
   coot::util::normalise_link_blank_fields(mol);

   if (g_dump_chains) dump_chains("A (raw mmdb reader)", mol);
   harvest(mol, s);
   delete mol;
   return s;
}

static ModelSummary load_path_b(const std::string &path, bool setup_entities,
                                bool merge_chains, bool drop_hydrog_links) {
   ModelSummary s;

   // Path B calls the PRODUCTION reader, coot::read_coords_with_gemmi(), rather
   // than re-implementing it. That is the point: the tool then tests the shipped
   // code, and the five adjustments (chain merge, Hydrog filter, spacegroup
   // normalisation, no fabricated cell, zero-atom fallback) cannot drift between
   // tool and application.
   //
   // Consequence: --no-setup-entities / --no-merge-chains / --keep-hydrog-links
   // can no longer be honoured, because those choices now live inside the
   // production function. They are accepted and reported as ignored rather than
   // silently doing nothing.
   if (!setup_entities || !merge_chains || !drop_hydrog_links)
      printf("    note: --no-setup-entities/--no-merge-chains/--keep-hydrog-links are\n"
             "          ignored now that path B calls the production reader\n");

   std::string message;
   mmdb::Manager *mol = coot::read_coords_with_gemmi(path, &message);
   if (!mol) {
      s.error = "read_coords_with_gemmi: " + message;
      return s;
   }
   if (g_dump_chains) dump_chains("B (production gemmi reader)", mol);
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

   printf("    counts    A: %ld atoms (+%ld TER), %d model(s), %zu chain(s), "
          "%d link(s) (%d resolved)\n",
          A.n_real, A.n_ter, A.n_models, A.chains.size(), A.n_links,
          A.n_links_resolved);
   printf("              B: %ld atoms (+%ld TER), %d model(s), %zu chain(s), "
          "%d link(s) (%d resolved)\n",
          B.n_real, B.n_ter, B.n_models, B.chains.size(), B.n_links,
          B.n_links_resolved);

   if (A.n_reachable_by_id != A.n_real || B.n_reachable_by_id != B.n_real)
      printf("    reachable via GetChain(id)  A: %ld/%ld   B: %ld/%ld  <-- shortfall = "
             "atoms hidden from by-name chain lookup\n",
             A.n_reachable_by_id, A.n_real, B.n_reachable_by_id, B.n_real);

   if (A.n_real != B.n_real)   { clean = false; printf("    DIFF atom count\n"); }
   if (A.n_models != B.n_models) { clean = false; printf("    DIFF model count\n"); }
   if (A.n_links != B.n_links) { clean = false; printf("    DIFF link count\n"); }

   // POST-CONDITION, not a diff. A link whose partners do not resolve is inert:
   // no bond is drawn and no restraint is generated, however healthy the count
   // looks and however well the two paths agree with each other. Both of the link
   // bugs this project has hit were invisible to a count comparison -- gemmi's
   // unpadded LINK atom names (0 of 130 on 5E1N.cif) and mmdb's CIF writer
   // emitting a blank insCode/altLoc as " " (0 of 130 through a backup round
   // trip). Fail on either path independently.
   if (A.n_links > 0 && A.n_links_resolved != A.n_links) {
      clean = false;
      printf("    UNRESOLVED LINKS in A: only %d of %d resolve to atoms\n",
             A.n_links_resolved, A.n_links);
   }
   if (B.n_links > 0 && B.n_links_resolved != B.n_links) {
      clean = false;
      printf("    UNRESOLVED LINKS in B: only %d of %d resolve to atoms\n",
             B.n_links_resolved, B.n_links);
   }

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
   // Resolution. Compared as "has one" plus the value, because mmdb reports
   // absence with a NEGATIVE number rather than a flag.
   {
      bool a_has = A.resolution > 0.0, b_has = B.resolution > 0.0;
      if (a_has != b_has) {
         clean = false;
         printf("    DIFF resolution present  A=%d (%.3f) B=%d (%.3f)\n",
                (int)a_has, A.resolution, (int)b_has, B.resolution);
      } else if (a_has && std::fabs(A.resolution - B.resolution) > 1e-3) {
         clean = false;
         printf("    DIFF resolution  A=%.3f B=%.3f\n", A.resolution, B.resolution);
      }
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
      // The FLAG first, and separately from the values. gemmi setting
      // ASET_Anis_tFSigma where mmdb wants ASET_Anis_tFac left the tensors
      // present and correct but invisible to every consumer, so a value-only
      // comparison would have reported this file as clean.
      if (a.aniso_tfac != b.aniso_tfac)
         tally.note("aniso ADP flag (ASET_Anis_tFac)", k,
                    a.aniso_tfac ? "set" : "unset", b.aniso_tfac ? "set" : "unset");
      if (a.aniso_sigma != b.aniso_sigma)
         tally.note("aniso SIGMA flag (ASET_Anis_tFSigma)", k,
                    a.aniso_sigma ? "set" : "unset", b.aniso_sigma ? "set" : "unset");
      if (a.aniso_tfac && b.aniso_tfac) {
         static const char *un[6] = {"u11","u22","u33","u12","u13","u23"};
         for (int iu=0; iu<6; iu++)
            if (std::fabs(a.u[iu] - b.u[iu]) > 1e-5)
               tally.note(std::string("aniso ") + un[iu], k,
                          fnum(a.u[iu]), fnum(b.u[iu]));
      }
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

// mmdb can only set a model's space group if it can find syminfo.lib: it checks
// $SYMINFO and otherwise looks in the CURRENT DIRECTORY. The installed launcher
// exports SYMINFO (bin/bcoot), but this tool runs outside it, so without help
// its spacegroup comparison silently degrades to null-vs-null and would hide a
// real regression. Point SYMINFO at the install's copy unless already set.
static void ensure_syminfo() {
   if (getenv("SYMINFO")) {
      printf("SYMINFO (inherited): %s\n", getenv("SYMINFO"));
      return;
   }
#ifdef BANDICOOT_SYMINFO_DEFAULT
   const char *cand = BANDICOOT_SYMINFO_DEFAULT;
   if (FILE *f = fopen(cand, "r")) {
      fclose(f);
      setenv("SYMINFO", cand, 0);
      printf("SYMINFO (from install): %s\n", cand);
      return;
   }
   printf("SYMINFO: NOT FOUND at %s -- spacegroups will not be set, and the\n"
          "         spacegroup comparison below is therefore meaningless.\n", cand);
#else
   printf("SYMINFO: unset and no compiled-in default -- spacegroup comparison\n"
          "         is only meaningful from a directory containing syminfo.lib.\n");
#endif
}

// Enumerate the coordinate files of a corpus directory, in a DETERMINISTIC
// order so two runs are comparable line by line.
//
// Only .cif / .ent / .pdb. The maps (.mtz) are not coordinate files, and the
// SHELX .ins is left out ON PURPOSE: gemmi cannot read it at all (it throws
// "Unknown format"), so including it would add a permanent "failed to load"
// line to every run and push the baseline further from "all lines explained".
// See TRAPS.md A6 -- that trap wants its own check, not a noisy default.
static std::vector<std::string> corpus_files(const std::string &dir) {
   std::vector<std::string> out;
   DIR *d = opendir(dir.c_str());
   if (! d) return out;
   while (struct dirent *e = readdir(d)) {
      std::string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;      // skips .DS_Store too
      size_t dot = n.rfind('.');
      if (dot == std::string::npos) continue;
      std::string ext = n.substr(dot);
      for (char &c : ext) c = std::tolower(static_cast<unsigned char>(c));
      if (ext == ".cif" || ext == ".ent" || ext == ".pdb")
         out.push_back(dir + "/" + n);
   }
   closedir(d);
   std::sort(out.begin(), out.end());
   return out;
}

// ---------------------------------------------------------------------------
// WRITE-SIDE CHECK (Phase 3 gate)
//
// Read a file through the production reader, write it straight back out through
// the production writer with NO edits in between, and diff the categories of
// the input against the output.
//
// This is the axis the read-side diff structurally cannot see. The Phase 1 gate
// passed with "~250,000 atoms, ZERO differences" while measuring only atoms,
// and the metadata losses it could not see surfaced a day later in discussion
// rather than at the gate. This closes that axis for good: the next gemmi bump,
// the next per-category policy change and the next reader change cannot quietly
// reopen the class of loss, because this fails instead.
//
// WHAT "IDENTICAL" MEANS HERE: tag-and-value identity, NOT literal bytes. gemmi
// regenerates loop text with its own column spacing, so alignment and trailing
// whitespace are normalised on every write. That is not data loss, and chasing
// byte-identity would mean re-implementing gemmi's writer.
// ---------------------------------------------------------------------------

// Category name -> number of rows (loop) or tag-items (pairs).
static std::map<std::string, size_t> categories_of(const gemmi::cif::Block &b) {
   std::map<std::string, size_t> m;
   for (const gemmi::cif::Item &it : b.items) {
      std::string tag;
      size_t rows = 1;
      if (it.type == gemmi::cif::ItemType::Pair) {
         tag = it.pair[0];
      } else if (it.type == gemmi::cif::ItemType::Loop && !it.loop.tags.empty()) {
         tag = it.loop.tags[0];
         rows = it.loop.values.size() / it.loop.tags.size();
      }
      if (tag.empty()) continue;
      std::string::size_type dot = tag.find('.');
      m[dot == std::string::npos ? tag : tag.substr(0, dot)] += rows;
   }
   return m;
}

// Category -> tag -> column of values, built from either a loop or a set of
// pairs so the two representations compare equal. Values are unquoted.
typedef std::map<std::string, std::map<std::string, std::vector<std::string> > > CatValues;

static CatValues harvest_values(const gemmi::cif::Block &b) {
   CatValues out;
   for (const gemmi::cif::Item &it : b.items) {
      if (it.type == gemmi::cif::ItemType::Pair) {
         const std::string &tag = it.pair[0];
         std::string::size_type dot = tag.find('.');
         if (dot == std::string::npos) continue;
         out[tag.substr(0, dot)][tag.substr(dot + 1)]
            .push_back(gemmi::cif::as_string(it.pair[1]));
      } else if (it.type == gemmi::cif::ItemType::Loop && !it.loop.tags.empty()) {
         size_t ntags = it.loop.tags.size();
         size_t nrows = it.loop.values.size() / ntags;
         for (size_t c = 0; c < ntags; c++) {
            const std::string &tag = it.loop.tags[c];
            std::string::size_type dot = tag.find('.');
            if (dot == std::string::npos) continue;
            std::vector<std::string> &col = out[tag.substr(0, dot)][tag.substr(dot + 1)];
            for (size_t r = 0; r < nrows; r++)
               col.push_back(gemmi::cif::as_string(it.loop.values[r * ntags + c]));
         }
      }
   }
   return out;
}

// Equal as VALUES, not as text: "1" == "1.00" and "18.42" == "18.420", because
// gemmi writes the shortest representation while wwPDB pads to fixed decimals.
// Without this every atom row would report a difference and the check would be
// unreadable. A genuine coordinate change is far larger than the tolerance.
// Order numerically when both sides parse as numbers, lexicographically
// otherwise. Sorting numeric columns as TEXT pairs "18.42" with "18.420" in
// different slots and reports thousands of phantom coordinate changes -- the
// same class of mistake as comparing by index, one level down.
static bool value_less(const std::string &a, const std::string &b) {
   try {
      size_t pa = 0, pb = 0;
      double da = std::stod(a, &pa), db = std::stod(b, &pb);
      if (pa == a.size() && pb == b.size()) return da < db;
   } catch (...) {}
   return a < b;
}

static bool values_equal(const std::string &a, const std::string &b) {
   if (a == b) return true;
   try {
      size_t pa = 0, pb = 0;
      double da = std::stod(a, &pa), db = std::stod(b, &pb);
      if (pa == a.size() && pb == b.size())
         return std::fabs(da - db) <= 1e-4 * std::max(1.0, std::fabs(da));
   } catch (...) {}
   return false;
}

static bool is_mmcif_path(const std::string &p) {
   std::string n = p;
   if (n.size() > 3 && n.compare(n.size() - 3, 3, ".gz") == 0) n.erase(n.size() - 3);
   std::string::size_type dot = n.rfind('.');
   if (dot == std::string::npos) return false;
   std::string e = n.substr(dot);
   for (char &c : e) c = std::tolower(static_cast<unsigned char>(c));
   return e == ".cif" || e == ".mmcif" || e == ".mcif";
}

// Is this a COORDINATE mmCIF, i.e. a file whose document the writer preserves?
//
// Extension is not enough: a chemical-component definition is also ".cif" but
// carries _chem_comp_atom instead of _atom_site, and the read path deliberately
// keeps no document for it. Such a file belongs with the PDB inputs -- read it,
// but do not ask the mmCIF-preservation questions of it.
static bool is_coordinate_mmcif(const std::string &p) {
   if (! is_mmcif_path(p)) return false;
   return coot::cif_chem_comp_id(p).empty();
}

// returns: 0 clean, 1 something was lost
static int write_check(const std::vector<std::string> &files, const std::string &out_dir) {

   mkdir(out_dir.c_str(), 0755);
   printf("write-side check: read -> write with no edits, categories in vs out\n");
   printf("output written to %s\n\n", out_dir.c_str());

   int n_clean = 0, n_lossy = 0, n_skipped = 0, n_failed = 0, n_declined = 0;

   for (const std::string &in : files) {

      std::string base = in.substr(in.rfind('/') + 1);

      // A PDB input has no document to preserve, so "categories in vs out" is
      // not a question that means anything -- the writer synthesises, and the
      // input had no categories to compare against. Skip rather than pretend.
      //
      // A CHEMICAL-COMPONENT definition (AR6.cif, ADP.cif) is in exactly that
      // position: the read path deliberately does not retain a chem_comp
      // document, because handing one to update_mmcif_block() would have it edit
      // _atom_site categories that are not there. Reading one and writing
      // coordinate mmCIF is a CROSS-FORMAT conversion, like PDB -> mmCIF, so its
      // chem_comp categories are gone by construction and counting that as
      // "lossy" measures the wrong thing.
      if (! is_coordinate_mmcif(in)) { n_skipped++; continue; }

      printf("=== %s\n", base.c_str());

      std::shared_ptr<coot::mmcif_document_t> doc;
      std::string msg;
      mmdb::Manager *mol = coot::read_coords_with_gemmi(in, &msg, &doc);
      if (! mol) {
         // The reader DECLINING a file is not a write-side failure. A
         // small-molecule CIF legitimately yields zero atoms and falls back to
         // read-sm-cif.cc; making that fail this gate would leave the gate
         // permanently red and therefore useless. Read-side behaviour is the
         // read-side mode's job. Reported, and counted separately, so a NEW
         // decline is still visible as a changed count.
         printf("    read declined (%s) -- not a write-side concern\n\n", msg.c_str());
         n_declined++;
         continue;
      }

      std::string out = out_dir + "/" + base;
      if (! coot::write_coords_with_gemmi(mol, out, doc.get(), &msg)) {
         printf("    WRITE FAILED: %s\n\n", msg.c_str());
         n_failed++;
         delete mol;
         continue;
      }
      delete mol;

      std::map<std::string, size_t> before, after;
      size_t blocks_in = 0, blocks_out = 0;
      try {
         gemmi::cif::Document di = gemmi::read_cif_gz(in);
         gemmi::cif::Document dobj = gemmi::read_cif_gz(out);
         blocks_in  = di.blocks.size();
         blocks_out = dobj.blocks.size();
         before = categories_of(di.blocks.at(0));
         after  = categories_of(dobj.blocks.at(0));
      } catch (const std::exception &e) {
         printf("    RE-READ FAILED: %s\n\n", e.what());
         n_failed++;
         continue;
      }

      printf("    blocks %zu -> %zu   categories %zu -> %zu\n",
             blocks_in, blocks_out, before.size(), after.size());

      // --- column and value comparison (the axis that missed the label_*
      // and _atom_site_anisotrop losses: both categories were PRESENT with the
      // right ROW COUNT while columns went missing underneath) ---
      int n_tag_lost = 0, n_val_diff = 0, n_examples = 0;
      try {
         CatValues vb = harvest_values(gemmi::read_cif_gz(in).blocks.at(0));
         CatValues va = harvest_values(gemmi::read_cif_gz(out).blocks.at(0));
         for (CatValues::const_iterator c = vb.begin(); c != vb.end(); ++c) {
            CatValues::const_iterator c2 = va.find(c->first);
            if (c2 == va.end()) continue;          // whole-category loss already reported
            for (std::map<std::string, std::vector<std::string> >::const_iterator
                    t = c->second.begin(); t != c->second.end(); ++t) {
               std::map<std::string, std::vector<std::string> >::const_iterator
                  t2 = c2->second.find(t->first);
               if (t2 == c2->second.end()) {
                  printf("      TAG LOST: %s.%s\n", c->first.c_str(), t->first.c_str());
                  n_tag_lost++;
                  continue;
               }
               if (t->second.size() != t2->second.size()) continue;  // ROWS line covers it
               // Compare each column as a SORTED MULTISET, never positionally.
               // Row order legitimately differs: merge_chain_parts() concatenates
               // split chain parts on read, so a file whose author chain was
               // split comes back with its residues in a different order. A
               // positional compare reported 566,883 "changed" B factors on the
               // first run -- which is precisely the mistake this tool's own
               // header warns about for atoms ("never by index, or one ordering
               // difference looks like thousands of changed atoms"). Sorting
               // costs the ability to spot a permutation WITHIN one column,
               // which is not a failure mode any of this code can produce.
               std::vector<std::string> col_b = t->second, col_a = t2->second;
               std::sort(col_b.begin(), col_b.end(), value_less);
               std::sort(col_a.begin(), col_a.end(), value_less);
               // ACCEPTED DEVIATION: element symbol case. mmdb preserves the
               // file's spelling ("Cl"), gemmi normalises to upper ("CL").
               // Art's decision 2026-08-13: keep the normalisation, because it
               // is what wwPDB itself writes -- CA, SE, MG throughout the
               // corpus -- while mixed case comes from phenix. So it follows
               // the convention rather than breaking it, and the gate must not
               // fail on it or it stays permanently red on the SC1 files.
               bool element_col = (t->first == "type_symbol" || t->first == "symbol");
               for (size_t r = 0; r < col_b.size(); r++) {
                  if (element_col) {
                     std::string x = col_b[r], y = col_a[r];
                     for (char &ch : x) ch = std::toupper((unsigned char) ch);
                     for (char &ch : y) ch = std::toupper((unsigned char) ch);
                     if (x == y) continue;
                  }
                  if (! values_equal(col_b[r], col_a[r])) {
                     n_val_diff++;
                     if (n_examples < 3) {
                        printf("      VALUE   : %s.%s (sorted) \"%s\" -> \"%s\"\n",
                               c->first.c_str(), t->first.c_str(),
                               col_b[r].c_str(), col_a[r].c_str());
                        n_examples++;
                     }
                  }
               }
            }
         }
      } catch (const std::exception &e) {
         printf("      (value comparison failed: %s)\n", e.what());
      }
      if (n_val_diff > n_examples)
         printf("      VALUE   : ... %d differing cells in total\n", n_val_diff);

      bool lost = (n_tag_lost > 0) || (n_val_diff > 0);
      if (blocks_in != blocks_out) {
         printf("      BLOCKS LOST: %zu -> %zu\n", blocks_in, blocks_out);
         lost = true;
      }
      for (const auto &kv : before) {
         if (! after.count(kv.first)) {
            printf("      LOST    : %-36s (%zu rows)\n", kv.first.c_str(), kv.second);
            lost = true;
         }
      }
      for (const auto &kv : after)
         if (! before.count(kv.first))
            printf("      ADDED   : %-36s (%zu rows)\n", kv.first.c_str(), kv.second);
      for (const auto &kv : before) {
         std::map<std::string, size_t>::const_iterator j = after.find(kv.first);
         if (j != after.end() && j->second != kv.second)
            printf("      ROWS    : %-36s %zu -> %zu\n",
                   kv.first.c_str(), kv.second, j->second);
      }

      if (lost) n_lossy++; else n_clean++;
      printf("    ==> %s\n\n", lost ? "LOSSY" : "no categories lost");
   }

   printf("write-side summary: %d clean, %d lossy, %d write-failed, "
          "%d read-declined, %d skipped (not mmCIF)\n",
          n_clean, n_lossy, n_failed, n_declined, n_skipped);

   // Unlike the read-side diff, THIS exit status IS a gate: losing a category is
   // never expected or deliberate, so any non-zero here is a real regression.
   if (n_lossy || n_failed)
      printf("*** WRITE-SIDE GATE FAILED ***\n");
   return (n_lossy || n_failed) ? 1 : 0;
}


// ============================ ROUND-TRIP MODE ============================
//
// Three chains, each read -> write -> read, run over the corpus:
//
//   A. PDB   in -> mmCIF out -> mmCIF in
//   B. mmCIF in -> mmCIF out -> mmCIF in
//   C. mmCIF in -> PDB   out -> PDB   in
//
// WHY THIS EXISTS SEPARATELY FROM --write-check. That mode compares OUR OUTPUT
// against the ORIGINAL INPUT and stops there, so a defect that is stable across
// one hop but compounds over two -- or one that only appears when our own
// output is fed back in as an input -- is invisible to it. Chain B closes that
// gap: it re-reads what we wrote and writes it again, and the two outputs must
// be byte-identical.
//
// Chains A and C measure the cross-format directions, which is where the
// remaining known losses live. They are REPORTED but do NOT set the exit
// status, because several of their differences are permanent and deliberate
// (PDB cannot express a hydrogen-bond connection at all, and a PDB file has no
// categories to compare). Only chain B is a gate. Stating which is which is the
// same discipline the read-side mode needed: an exit status that is always
// non-zero stops being read.
//
// Deliberately NOT part of a default run: it writes six files per corpus entry
// and takes appreciably longer. Run it after a substantial change.

struct RoundTripLoad {
   mmdb::Manager *mol = nullptr;
   std::shared_ptr<coot::mmcif_document_t> doc;
   std::string error;
};

// Read by the same route the application would: mmCIF through the production
// gemmi reader (keeping the document, which the writer needs), PDB through
// mmdb with get_atom_selection's flags.
static RoundTripLoad rt_read(const std::string &path) {

   RoundTripLoad r;

   if (is_mmcif_path(path)) {
      std::string msg;
      r.mol = coot::read_coords_with_gemmi(path, &msg, &r.doc);
      if (! r.mol) r.error = msg.empty() ? "read failed" : msg;
      return r;
   }

   mmdb::Manager *mol = new mmdb::Manager;
   mol->SetFlag(mmdb::MMDBF_IgnoreBlankLines | mmdb::MMDBF_IgnoreNonCoorPDBErrors |
                mmdb::MMDBF_IgnoreHash | mmdb::MMDBF_IgnoreRemarks |
                mmdb::MMDBF_IgnoreDuplSeqNum);
   if (mol->ReadCoorFile(path.c_str()) != 0) {
      r.error = "mmdb ReadCoorFile failed";
      delete mol;
      return r;
   }
   mol->PDBCleanup(mmdb::PDBCLEAN_ELEMENT);
   coot::util::normalise_link_blank_fields(mol);
   r.mol = mol;
   return r;
}

// Write through write_atom_selection_file(), NOT straight to
// coot::write_coords_with_gemmi(). That is the whole point of a round-trip
// test: it must exercise what Save Coordinates and make_backup() actually call,
// including remove_wrong_cis_peptides() and the hydrogens/aniso options.
static bool rt_write(RoundTripLoad &in, const std::string &path, bool as_cif) {

   atom_selection_container_t asc = make_asc(in.mol);
   int status = write_atom_selection_file(asc, path, as_cif, mmdb::io::GZM_NONE,
                                          1 /* hydrogens */, 1 /* aniso */,
                                          0 /* conect */, in.doc.get());
   return status == 0;
}

static bool files_are_identical(const std::string &a, const std::string &b) {
   FILE *fa = fopen(a.c_str(), "rb");
   FILE *fb = fopen(b.c_str(), "rb");
   if (! fa || ! fb) { if (fa) fclose(fa); if (fb) fclose(fb); return false; }
   bool same = true;
   char ba[65536], bb[65536];
   for (;;) {
      size_t na = fread(ba, 1, sizeof(ba), fa);
      size_t nb = fread(bb, 1, sizeof(bb), fb);
      if (na != nb || memcmp(ba, bb, na) != 0) { same = false; break; }
      if (na == 0) break;
   }
   fclose(fa);
   fclose(fb);
   return same;
}

static size_t category_count(const std::string &path) {
   try {
      gemmi::cif::Document d = gemmi::read_cif_gz(path);
      if (d.blocks.empty()) return 0;
      return categories_of(d.blocks.at(0)).size();
   } catch (const std::exception &) {
      return 0;
   }
}

// Compare the categories of two mmCIF files, reporting losses only.
// Returns the number of categories lost.
static int lost_categories(const std::string &before_path, const std::string &after_path) {
   try {
      gemmi::cif::Document db = gemmi::read_cif_gz(before_path);
      gemmi::cif::Document da = gemmi::read_cif_gz(after_path);
      if (db.blocks.empty() || da.blocks.empty()) return 0;
      std::map<std::string, size_t> b = categories_of(db.blocks.at(0));
      std::map<std::string, size_t> a = categories_of(da.blocks.at(0));
      int lost = 0;
      for (const auto &kv : b)
         if (! a.count(kv.first)) {
            printf("        LOST CATEGORY : %s (%zu rows)\n", kv.first.c_str(), kv.second);
            lost++;
         }
      return lost;
   } catch (const std::exception &e) {
      printf("        (category comparison failed: %s)\n", e.what());
      return 0;
   }
}

// Does the file STATE a resolution, and did our conversion keep it?
//
// The model-vs-model comparison cannot answer this: it compares
// mmdb::GetResolution() on both sides, so "neither has one" reads as agreement.
// That is exactly how the phenix case hid -- a PDB with no REMARK 2 gives mmdb
// nothing, our mmCIF then had nothing, and the two nothings matched. So this
// asks the FILES instead, by text, which is also the only way to see a
// resolution that lives in REMARK 3 prose.
static double stated_resolution_of_pdb(const std::string &path) {

   std::ifstream f(path.c_str());
   if (! f) return -1.0;
   std::string line;
   double from_remark_3 = -1.0;
   while (std::getline(f, line)) {
      if (line.compare(0, 6, "ATOM  ") == 0 || line.compare(0, 6, "HETATM") == 0) break;
      if (line.compare(0, 10, "REMARK   2") == 0) {
         size_t p = line.find("RESOLUTION.");
         if (p != std::string::npos) {
            double v = std::atof(line.substr(p + 11).c_str());
            if (v > 0.0) return v;                      // the authoritative one
         }
      }
      if (line.compare(0, 10, "REMARK   3") == 0) {
         size_t p = line.find("RESOLUTION RANGE HIGH");
         if (p != std::string::npos) {
            size_t colon = line.find(':', p);
            if (colon != std::string::npos) {
               double v = std::atof(line.substr(colon + 1).c_str());
               if (v > 0.0 && from_remark_3 < 0.0) from_remark_3 = v;
            }
         }
      }
   }
   return from_remark_3;
}

static double stated_resolution_of_mmcif(const std::string &path) {

   std::ifstream f(path.c_str());
   if (! f) return -1.0;
   std::string line;
   while (std::getline(f, line)) {
      size_t p = line.find("_refine.ls_d_res_high");
      if (p == std::string::npos) continue;
      double v = std::atof(line.substr(p + 21).c_str());
      if (v > 0.0) return v;
   }
   return -1.0;
}

// returns 0 if chain B held everywhere, 1 otherwise
static int round_trip(const std::vector<std::string> &files, const std::string &out_dir) {

   mkdir(out_dir.c_str(), 0755);
   printf("round-trip check: read -> write -> read, three chains\n");
   printf("  A  PDB   -> mmCIF -> mmCIF     (reported, not a gate)\n");
   printf("  B  mmCIF -> mmCIF -> mmCIF     (THE GATE)\n");
   printf("  C  mmCIF -> PDB   -> PDB       (reported, not a gate)\n");
   printf("output written to %s\n\n", out_dir.c_str());

   int a_ok = 0, a_diff = 0, b_ok = 0, b_bad = 0, c_ok = 0, c_diff = 0, n_failed = 0;

   for (const std::string &in : files) {

      std::string base = in.substr(in.rfind('/') + 1);
      printf("=== %s\n", base.c_str());

      RoundTripLoad first = rt_read(in);
      if (! first.mol) {
         printf("    read failed (%s) -- skipped\n\n", first.error.c_str());
         continue;
      }
      ModelSummary m0;
      harvest(first.mol, m0);

      // Chem_comp inputs take the PDB-shaped path too: no document to preserve,
      // so the meaningful question is chain A (synthesise -> mmCIF -> mmCIF),
      // not chain B's byte-identity of a document that was never kept.
      if (! is_coordinate_mmcif(in)) {

         // ---- chain A: PDB in -> mmCIF out -> mmCIF in ----
         std::string hop1 = out_dir + "/" + base + ".A1.cif";
         if (! rt_write(first, hop1, true)) {
            printf("  A  WRITE FAILED\n\n");
            n_failed++;
            delete first.mol;
            continue;
         }
         RoundTripLoad back = rt_read(hop1);
         if (! back.mol) {
            printf("  A  RE-READ FAILED (%s)\n\n", back.error.c_str());
            n_failed++;
            delete first.mol;
            continue;
         }
         ModelSummary m2;
         harvest(back.mol, m2);
         printf("  A  PDB -> mmCIF -> mmCIF   (%zu categories written)\n",
                category_count(hop1));
         bool clean = compare(m0, m2);
         // Post-condition, not a model compare: a resolution the INPUT FILE
         // states must appear in our mmCIF as _refine.ls_d_res_high. Only PDB
         // inputs are asked -- a chem_comp file states no resolution and is not
         // expected to.
         if (! is_coordinate_mmcif(in) && in.rfind(".cif") != in.size() - 4) {
            double r_in  = stated_resolution_of_pdb(in);
            double r_out = stated_resolution_of_mmcif(hop1);
            if (r_in > 0.0 && r_out <= 0.0) {
               printf("     RESOLUTION LOST: input states %.2f, our mmCIF has no "
                      "_refine.ls_d_res_high\n", r_in);
               clean = false;
            } else if (r_in > 0.0 && std::fabs(r_in - r_out) > 0.02) {
               printf("     RESOLUTION CHANGED: input %.2f, our mmCIF %.2f\n", r_in, r_out);
               clean = false;
            }
         }
         printf("     ==> %s\n", clean ? "model preserved" : "MODEL DIFFERS");
         if (clean) a_ok++; else a_diff++;
         delete back.mol;

      } else {

         // ---- chain B: mmCIF in -> mmCIF out -> mmCIF in -> mmCIF out ----
         std::string hop1 = out_dir + "/" + base + ".B1.cif";
         std::string hop2 = out_dir + "/" + base + ".B2.cif";
         bool bad = false;
         if (! rt_write(first, hop1, true)) {
            printf("  B  WRITE FAILED\n");
            bad = true;
         } else {
            RoundTripLoad second = rt_read(hop1);
            if (! second.mol) {
               printf("  B  RE-READ FAILED (%s)\n", second.error.c_str());
               bad = true;
            } else {
               ModelSummary m1;
               harvest(second.mol, m1);
               if (! rt_write(second, hop2, true)) {
                  printf("  B  SECOND WRITE FAILED\n");
                  bad = true;
               } else {
                  printf("  B  mmCIF -> mmCIF -> mmCIF   (%zu -> %zu -> %zu categories)\n",
                         category_count(in), category_count(hop1), category_count(hop2));
                  if (lost_categories(in, hop1) > 0)   bad = true;
                  if (lost_categories(hop1, hop2) > 0) bad = true;
                  // THE point of this chain: our own output, read back and
                  // written again, must reproduce itself exactly. Anything that
                  // drifts per hop shows up here and nowhere else.
                  if (! files_are_identical(hop1, hop2)) {
                     printf("        HOP 1 AND HOP 2 DIFFER -- the round trip is not stable\n");
                     bad = true;
                  }
                  if (! compare(m0, m1)) {
                     printf("        MODEL DIFFERS between the input and our own output\n");
                     bad = true;
                  }
               }
               delete second.mol;
            }
         }
         printf("     ==> %s\n", bad ? "*** NOT STABLE ***" : "stable and lossless");
         if (bad) b_bad++; else b_ok++;

         // ---- chain C: mmCIF in -> PDB out -> PDB in ----
         std::string cpdb = out_dir + "/" + base + ".C1.pdb";
         if (! rt_write(first, cpdb, false)) {
            printf("  C  WRITE FAILED\n");
            n_failed++;
         } else {
            RoundTripLoad back = rt_read(cpdb);
            if (! back.mol) {
               printf("  C  RE-READ FAILED (%s)\n", back.error.c_str());
               n_failed++;
            } else {
               ModelSummary mc;
               harvest(back.mol, mc);
               printf("  C  mmCIF -> PDB -> PDB\n");
               bool clean = compare(m0, mc);
               printf("     ==> %s\n", clean ? "model preserved"
                      : "model differs (expected for some files -- PDB cannot "
                        "express everything mmCIF can)");
               if (clean) c_ok++; else c_diff++;
               delete back.mol;
            }
         }
      }

      printf("\n");
      delete first.mol;
   }

   printf("round-trip summary:\n");
   printf("  A  PDB->mmCIF->mmCIF : %d model preserved, %d differ\n", a_ok, a_diff);
   printf("  B  mmCIF round trip  : %d stable, %d NOT stable   <-- the gate\n", b_ok, b_bad);
   printf("  C  mmCIF->PDB->PDB   : %d model preserved, %d differ\n", c_ok, c_diff);
   if (n_failed)
      printf("  %d write/read failure(s)\n", n_failed);

   if (b_bad || n_failed)
      printf("*** ROUND-TRIP GATE FAILED (chain B) ***\n");
   return (b_bad || n_failed) ? 1 : 0;
}



// ===================================================================
// --header-check : does the mmCIF header synthesis agree with the PDB sibling?
//
// Asserts adjustment (9) -- pdb_header_records_from_mmcif() -- and it is the one
// boundary adjustment the read-side differential CANNOT assert. That mode
// compares gemmi against mmdb, and mmdb's own mmCIF header readers are keyed to
// NDB-era tag names PDBx abandoned, so they return nothing from a modern file:
// parity with mmdb would assert the ABSENCE of a header. The only available
// ground truth is the wwPDB's own PDB rendering of the same entry.
//
// This calls the PURE FUNCTION directly and compares its output, as text,
// against the sibling .pdb's header records. Deliberately NOT via a writer: the
// question is whether the SYNTHESIS is right, and routing through mmdb's
// containers plus WritePDBASCII would make a synthesis bug and a writer bug look
// identical. The function was made pure and portable for exactly this reason.
// ===================================================================

static std::string lowercased(const std::string &in) {
   std::string o = in;
   for (size_t i = 0; i < o.size(); i++) o[i] = tolower((unsigned char)o[i]);
   return o;
}

static std::string header_record_name(const std::string &line) {
   size_t n = line.find_first_of(" ");
   std::string r = line.substr(0, n == std::string::npos ? line.size() : n);
   if (r == "JRNL" || r == "REMARK") return r;   // grouped, not compared line-by-line
   return r;
}

// Read the header records of a PDB file as text, keyed by record name.
static std::map<std::string, std::vector<std::string> >
pdb_header_records_of_file(const std::string &path) {
   std::map<std::string, std::vector<std::string> > m;
   FILE *f = fopen(path.c_str(), "r");
   if (! f) return m;
   char buf[512];
   while (fgets(buf, sizeof buf, f)) {
      std::string line(buf);
      while (! line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                                line.back() == ' ')) line.pop_back();
      if (line.empty()) continue;
      std::string rec = header_record_name(line);
      // The records adjustment (9) synthesizes, and only those.
      static const char *of_interest[] = {"HEADER","TITLE","COMPND","SOURCE","KEYWDS",
                                          "EXPDTA","AUTHOR","REVDAT","JRNL","DBREF",
                                          "HETNAM","FORMUL","HELIX","SHEET", NULL};
      bool want = false;
      for (int i=0; of_interest[i]; i++) if (rec == of_interest[i]) { want = true; break; }
      if (! want) continue;
      m[rec].push_back(line);
   }
   fclose(f);
   return m;
}

// The PDB sibling of an mmCIF, by the two conventions the corpus uses:
// <stem>.pdb, and the wwPDB download name pdb<code>.ent.
static std::string pdb_sibling_of(const std::string &cif_path) {
   std::string dir = ".", base = cif_path;
   size_t slash = cif_path.rfind('/');
   if (slash != std::string::npos) { dir = cif_path.substr(0, slash); base = cif_path.substr(slash+1); }
   size_t dot = base.rfind('.');
   std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
   // a *_hierarchy.cif has the same entry as its plain sibling
   const std::string tail = "_hierarchy";
   if (stem.size() > tail.size() && stem.compare(stem.size()-tail.size(), tail.size(), tail) == 0)
      stem = stem.substr(0, stem.size()-tail.size());
   std::string cands[2] = { dir + "/" + stem + ".pdb", dir + "/pdb" + lowercased(stem) + ".ent" };
   for (int i=0; i<2; i++) {
      FILE *f = fopen(cands[i].c_str(), "r");
      if (f) { fclose(f); return cands[i]; }
   }
   return std::string();
}

static int run_header_check(const std::vector<std::string> &files) {

   int n_pairs = 0, n_clean = 0, n_diff = 0, n_nosib = 0;
   printf("header-check: synthesized mmCIF header vs the PDB sibling\n\n");

   for (size_t i = 0; i < files.size(); i++) {
      const std::string &in = files[i];
      if (! is_coordinate_mmcif(in)) continue;      // chem_comp and PDB inputs are not the subject
      std::string base = in.substr(in.rfind('/') + 1);
      std::string sib = pdb_sibling_of(in);
      if (sib.empty()) { n_nosib++; continue; }

      std::vector<std::string> synth;
      try {
         gemmi::cif::Document doc;
         gemmi::Structure st = gemmi::read_structure_gz(in, gemmi::CoorFormat::Unknown, &doc);
         if (doc.blocks.empty()) { n_nosib++; continue; }
         synth = coot::pdb_header_records_from_mmcif(st, &doc.blocks[0]);
      } catch (const std::exception &e) {
         printf("=== %s\n    READ FAILED: %s\n\n", base.c_str(), e.what());
         n_diff++;
         continue;
      }

      std::map<std::string, std::vector<std::string> > mine;
      for (size_t k = 0; k < synth.size(); k++)
         mine[header_record_name(synth[k])].push_back(synth[k]);
      std::map<std::string, std::vector<std::string> > theirs =
         pdb_header_records_of_file(sib);

      printf("=== %s   vs %s\n", base.c_str(), sib.substr(sib.rfind('/')+1).c_str());
      bool clean = true;
      std::set<std::string> recs;
      for (std::map<std::string, std::vector<std::string> >::const_iterator
              it = mine.begin(); it != mine.end(); ++it) recs.insert(it->first);
      for (std::map<std::string, std::vector<std::string> >::const_iterator
              it = theirs.begin(); it != theirs.end(); ++it) recs.insert(it->first);

      for (std::set<std::string>::const_iterator r = recs.begin(); r != recs.end(); ++r) {
         // REMARK is NOT comparable and never will be by this route. Our REMARK 3
         // is a deliberate SUMMARY of _refine, and the prose REMARK sections
         // (200/280/350/465/500) were decided against -- they are wwPDB's
         // renderings of typed categories, so composing them is reconstruction,
         // not translation. Comparing counts here would report a permanent
         // difference that means nothing.
         if (*r == "REMARK") continue;
         size_t a = theirs.count(*r) ? theirs[*r].size() : 0;   // deposition
         size_t b = mine.count(*r)   ? mine[*r].size()   : 0;   // ours
         if (a == b) continue;
         // REVDAT: ours carries every revision the mmCIF states; the PDB-format
         // file omits some. JRNL: our author separator is "; " not ",", so the
         // wrap can differ by a line. Both are deliberate -- reported, not failed.
         // EXPDTA: phenix.refine's PDB output writes none at all, while its
         // mmCIF does state _exptl.method -- so ours carries a record the
         // sibling simply never wrote. Also reported, not failed.
         const char *why = (*r == "REVDAT") ? "  (deliberate: mmCIF states more revisions)"
                         : (*r == "JRNL")   ? "  (deliberate: \"; \" author separator rewraps)"
                         : (*r == "EXPDTA" && a == 0)
                           ? "  (deliberate: sibling writes none, ours from _exptl.method)"
                         : "";
         printf("    %-7s deposition %2zu  ours %2zu%s\n", r->c_str(), a, b, why);
         if (why[0] == '\0') clean = false;
      }
      // Exact text for the records that must match character for character.
      const char *exact[] = {"DBREF", "FORMUL", "HELIX", "SHEET", NULL};
      for (int e = 0; exact[e]; e++) {
         std::string r = exact[e];
         if (! mine.count(r) || ! theirs.count(r)) continue;
         if (mine[r].size() != theirs[r].size()) continue;    // count line covers it
         std::vector<std::string> A = theirs[r], B = mine[r];
         // SHEET: compare columns 1-41 only. The two registration atoms
         // (42-70) are a DELIBERATE omission -- gemmi carries them only from
         // _pdbx_struct_sheet_hbond, which no corpus file has -- so including
         // them would make every sheet look wrong for a documented reason.
         // FORMUL: blank out columns 9-11 before comparing. We write the
         // component number RIGHT-JUSTIFIED IN 10-11 ON PURPOSE (TRAPS B12):
         // mmdb WRITES it at 9-10 but READS it at 10-11, so a string laid out to
         // match the deposition would be mis-read, and one laid out for the
         // reader does not match the deposition. The synthesized string is only
         // ever fed to mmdb, and mmdb's writer then emits 9-10 correctly -- so
         // the column is right where it matters and wrong here by construction.
         if (r == "FORMUL")
            for (size_t k = 0; k < A.size(); k++) {
               for (int c = 8; c < 11 && c < (int)A[k].size(); c++) A[k][c] = ' ';
               for (int c = 8; c < 11 && c < (int)B[k].size(); c++) B[k][c] = ' ';
            }
         if (r == "SHEET")
            for (size_t k = 0; k < A.size(); k++) {
               if (A[k].size() > 41) A[k] = A[k].substr(0, 41);
               if (B[k].size() > 41) B[k] = B[k].substr(0, 41);
               // and trim: the cut lands on a space in the deposition while our
               // synthesized line has already had its trailing blanks removed,
               // so without this every sheet differs by one invisible character.
               while (! A[k].empty() && A[k].back() == ' ') A[k].pop_back();
               while (! B[k].empty() && B[k].back() == ' ') B[k].pop_back();
            }
         std::sort(A.begin(), A.end()); std::sort(B.begin(), B.end());
         for (size_t k = 0; k < A.size(); k++) {
            if (A[k] == B[k]) continue;
            // Element/molecule-name CASE is a recorded deliberate deviation: the
            // file's own spelling is kept where wwPDB upper-cases everything.
            std::string a_up = A[k], b_up = B[k];
            for (size_t c = 0; c < a_up.size(); c++) a_up[c] = toupper((unsigned char)a_up[c]);
            for (size_t c = 0; c < b_up.size(); c++) b_up[c] = toupper((unsigned char)b_up[c]);
            if (a_up == b_up) continue;                       // case only
            printf("      %s TEXT DIFFERS\n        deposition: %s\n        ours      : %s\n",
                   r.c_str(), A[k].c_str(), B[k].c_str());
            clean = false;
         }
      }
      printf("    ==> %s\n\n", clean ? "AGREES with the deposition" : "DIFFERS");
      n_pairs++;
      if (clean) n_clean++; else n_diff++;
   }

   printf("header-check summary: %d pair(s), %d agree, %d differ, %d mmCIF with no PDB sibling\n",
          n_pairs, n_clean, n_diff, n_nosib);
   return n_diff == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
   ensure_syminfo();
   bool setup_entities = true;
   bool merge_chains = true;
   bool drop_hydrog_links = true;
   std::vector<std::string> files;
   std::string corpus;
   bool do_write_check = false;
   bool do_round_trip = false;
   bool do_header_check = false;
   for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "--no-setup-entities") setup_entities = false;
      else if (a == "--no-merge-chains") merge_chains = false;
      else if (a == "--keep-hydrog-links") drop_hydrog_links = false;
      else if (a == "--dump-chains") g_dump_chains = true;
      else if (a == "--corpus" && i + 1 < argc) corpus = argv[++i];
      else if (a == "--write-check") do_write_check = true;
      else if (a == "--round-trip") do_round_trip = true;
      else if (a == "--header-check") do_header_check = true;
      else if (a == "--examples" && i + 1 < argc) DiffTally::max_examples = atoi(argv[++i]);
      else files.push_back(a);
   }

   // With no files named, run the whole corpus. Precedence: an explicit
   // --corpus wins, then $BANDICOOT_SAMPLES, then the path baked in at build
   // time. Naming files explicitly still works and ignores all of it.
   if (files.empty()) {
      if (corpus.empty()) {
         if (const char *env = getenv("BANDICOOT_SAMPLES")) corpus = env;
#ifdef BANDICOOT_SAMPLES_DEFAULT
         else corpus = BANDICOOT_SAMPLES_DEFAULT;
#endif
      }
      if (! corpus.empty()) {
         files = corpus_files(corpus);
         if (files.empty())
            printf("corpus: NO coordinate files in %s\n"
                   "        The corpus lives outside the repo and is not a build input;\n"
                   "        see TRAPS.md for what it should contain.\n", corpus.c_str());
         else
            printf("corpus: %zu files from %s\n", files.size(), corpus.c_str());
      }
   }

   if (files.empty()) {
      fprintf(stderr, "usage: gemmi-mmdb-diff [--dump-chains] [--examples N]\n"
                      "                      [--no-setup-entities] [--no-merge-chains]\n"
                      "                      [--keep-hydrog-links] [--corpus DIR]\n"
                      "                      [--write-check] [--round-trip] [--header-check]\n"
                      "                      [<coord-file> ...]\n"
                      "\n"
                      "With no files, runs every .cif/.ent/.pdb in the corpus directory\n"
                      "(--corpus, else $BANDICOOT_SAMPLES, else the compiled-in default).\n");
      return 2;
   }

   if (do_round_trip) {
      const char *tmp = getenv("TMPDIR");
      std::string out_dir = std::string(tmp ? tmp : "/tmp") + "/bandicoot-round-trip";
      return round_trip(files, out_dir);
   }

   if (do_header_check)
      return run_header_check(files);

   if (do_write_check) {
      const char *tmp = getenv("TMPDIR");
      std::string out_dir = tmp ? std::string(tmp) : std::string("/tmp");
      if (out_dir.empty() || out_dir[out_dir.size() - 1] != '/') out_dir += "/";
      out_dir += "bandicoot-write-check";
      return write_check(files, out_dir);
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

   // On a full-corpus run, differences and failures are the EXPECTED state --
   // several are deliberate improvements over the mmdb reader and several are
   // mmdb bugs the gemmi path fixes. So the exit status below cannot be the
   // gate here; the gate is "does this summary still match the baseline, and
   // is every line still attributable to a catalogue entry?"
   if (! corpus.empty())
      printf("\nNOTE: differences are EXPECTED on a corpus run -- compare the summary\n"
             "      above against the recorded baseline in TRAPS.md, and treat any NEW\n"
             "      unexplained line as the signal.\n");

   return n_dirty || n_failed ? 1 : 0;
}
