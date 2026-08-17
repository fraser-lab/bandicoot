/* coot-utils/gemmi-header.cc
 *
 * See gemmi-header.hh for why this exists and how it delivers.
 *
 * Every record below is laid out in the PDB's fixed columns, because mmdb
 * parses them by column: Helix::ConvertPDBASCII reads the initial residue name
 * from &S[15] and the helix class from &S[38], and a record that merely LOOKS
 * right is silently mis-parsed rather than rejected. The column numbers in the
 * comments are 1-based, as the PDB format description gives them.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include "gemmi-header.hh"

#include <gemmi/model.hpp>
#include <gemmi/cifdoc.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace {

   // ---------------------------------------------------------------- layout

   // A PDB record is 80 fixed columns. Build it as 80 spaces and drop text in
   // at 1-based column numbers, so the code reads like the format description.
   class pdb_line_t {
      std::string s;
   public:
      explicit pdb_line_t(const char *record_name) : s(80, ' ') {
         put(1, record_name);
      }
      void put(size_t col, const std::string &text) {
         for (size_t i = 0; i < text.size() && (col - 1 + i) < s.size(); i++)
            s[col - 1 + i] = text[i];
      }
      // right-justified in [first,last], as PDB integers are
      void put_right(size_t first, size_t last, const std::string &text) {
         if (text.size() >= (last - first + 1)) put(first, text);
         else put(last - text.size() + 1, text);
      }
      void put_int(size_t first, size_t last, int value) {
         put_right(first, last, std::to_string(value));
      }
      std::string str() const {
         std::string t = s;
         while (! t.empty() && t.back() == ' ') t.pop_back();
         return t;
      }
   };

   bool is_blank(const std::string &s) {
      for (char c : s) if (! std::isspace(static_cast<unsigned char>(c))) return false;
      return true;
   }

   // mmCIF nulls are "." and "?"; both mean "no value here" and neither should
   // reach a PDB record as literal text.
   bool is_null_value(const std::string &s) {
      return s.empty() || s == "." || s == "?" || is_blank(s);
   }

   std::string trimmed(const std::string &s) {
      size_t b = s.find_first_not_of(" \t\n\r");
      if (b == std::string::npos) return std::string();
      size_t e = s.find_last_not_of(" \t\n\r");
      return s.substr(b, e - b + 1);
   }

   // Greedy word wrap. Widths differ between the first line and the
   // continuations because the continuation number eats into the text field.
   std::vector<std::string> wrap_text(const std::string &text,
                                      size_t first_width, size_t cont_width) {
      std::vector<std::string> lines;
      std::istringstream iss(text);
      std::string word, line;
      size_t width = first_width;
      while (iss >> word) {
         if (line.empty()) {
            line = word;
         } else if (line.size() + 1 + word.size() <= width) {
            line += " ";
            line += word;
         } else {
            lines.push_back(line);
            line = word;
            width = cont_width;
         }
         // a single word longer than the field: let it overflow into its own
         // line rather than losing characters
         while (line.size() > width) {
            lines.push_back(line.substr(0, width));
            line = line.substr(width);
            width = cont_width;
         }
      }
      if (! line.empty()) lines.push_back(line);
      return lines;
   }

   // TITLE / COMPND / KEYWDS / EXPDTA / AUTHOR all share one shape: columns
   // 9-10 hold the continuation number (blank on the first line) and the text
   // runs from column 11. wwPDB shifts continuation text one column right, and
   // mmdb keeps everything from column 11 verbatim, so following that keeps
   // our output indistinguishable from a real PDB file's.
   void emit_continued(std::vector<std::string> &out, const char *record,
                       const std::vector<std::string> &lines) {
      for (size_t i = 0; i < lines.size(); i++) {
         pdb_line_t l(record);
         if (i == 0) {
            l.put(11, lines[i]);
         } else {
            l.put_int(9, 10, static_cast<int>(i) + 1);
            l.put(12, lines[i]);
         }
         out.push_back(l.str());
      }
   }

   void emit_wrapped(std::vector<std::string> &out, const char *record,
                     const std::string &text) {
      if (is_null_value(text)) return;
      emit_continued(out, record, wrap_text(trimmed(text), 70, 69));
   }

   // JRNL sub-records: "JRNL" in 1-4, the sub-record name in 13-16, its own
   // continuation number in 17-18, text from column 20.
   void emit_jrnl(std::vector<std::string> &out, const char *sub,
                  const std::string &text) {
      if (is_null_value(text)) return;
      std::vector<std::string> lines = wrap_text(trimmed(text), 61, 60);
      for (size_t i = 0; i < lines.size(); i++) {
         pdb_line_t l("JRNL");
         l.put(13, sub);
         if (i > 0) l.put_int(17, 18, static_cast<int>(i) + 1);
         l.put(20, lines[i]);
         out.push_back(l.str());
      }
   }

   void emit_remark(std::vector<std::string> &out, int number,
                    const std::string &text) {
      pdb_line_t l("REMARK");
      l.put_int(8, 10, number);
      if (! text.empty()) l.put(12, text);
      out.push_back(l.str());
   }

   // ---------------------------------------------------------------- values

   std::string block_value(const gemmi::cif::Block *block, const std::string &tag) {
      if (! block) return std::string();
      if (const std::string *v = block->find_value(tag)) {
         std::string s = gemmi::cif::as_string(*v);
         return is_null_value(s) ? std::string() : trimmed(s);
      }
      return std::string();
   }

   // "2015-09-29" -> "29-SEP-15", which is what HEADER's columns 51-59 want and
   // what mmdb's Date9to11 parses. Anything else is left out rather than
   // guessed at.
   std::string pdb_date(const std::string &iso) {
      if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return std::string();
      static const char *months[12] = { "JAN","FEB","MAR","APR","MAY","JUN",
                                        "JUL","AUG","SEP","OCT","NOV","DEC" };
      int mm = std::atoi(iso.substr(5, 2).c_str());
      if (mm < 1 || mm > 12) return std::string();
      return iso.substr(8, 2) + "-" + months[mm - 1] + "-" + iso.substr(2, 2);
   }

   // Names are joined with "; " rather than the PDB's "," because the mmCIF
   // spelling is itself "Surname, Initials": comma-joining "Lin, J." and
   // "Wilson, M.A." produces a list that cannot be read back apart. The
   // alternative -- rewriting each name into the PDB's "J.LIN" form -- would
   // mean uppercasing and re-ordering real people's names on a guess about
   // where the surname ends ("van den Bedem"), so the file's own spelling is
   // kept and only the separator differs.
   std::string join_names(const std::vector<std::string> &names) {
      std::string s;
      for (size_t i = 0; i < names.size(); i++) {
         if (i) s += "; ";
         s += names[i];
      }
      return s;
   }

   // ------------------------------------------------------- title / authors

   void add_header(std::vector<std::string> &out, const gemmi::Structure &st) {

      std::string classification = st.get_info("_struct_keywords.pdbx_keywords");
      std::string date = pdb_date(
         st.get_info("_pdbx_database_status.recvd_initial_deposition_date"));
      std::string id = st.get_info("_entry.id");

      if (classification.empty() && date.empty() && id.empty()) return;

      pdb_line_t l("HEADER");
      l.put(11, classification.substr(0, 40));
      l.put(51, date);
      l.put(63, id.substr(0, 4));
      out.push_back(l.str());
   }

   void add_authors(std::vector<std::string> &out, const gemmi::cif::Block *block) {
      if (! block) return;
      std::vector<std::string> names;
      for (auto row : const_cast<gemmi::cif::Block *>(block)->find("_audit_author.",
                                                                   {"name"}))
         if (! is_null_value(row.str(0))) names.push_back(row.str(0));
      emit_wrapped(out, "AUTHOR", join_names(names));
   }

   // COMPND, in the PDB's token form: one specification per line, semicolon
   // terminated except the last. Only polymer entities get one -- a PDB file
   // describes ligands in HETNAM, not COMPND, and inventing entries for waters
   // and ions would make the panel say more than the file does.
   void add_compound(std::vector<std::string> &out, const gemmi::Structure &st,
                     const gemmi::cif::Block *block) {

      if (! block) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      // entity id -> auth chain ids, from the model itself (the subchain of
      // each residue is its label_asym_id, and an Entity lists its subchains).
      std::map<std::string, std::string> subchain_to_entity;
      for (const gemmi::Entity &ent : st.entities)
         for (const std::string &sub : ent.subchains)
            subchain_to_entity[sub] = ent.name;

      std::map<std::string, std::vector<std::string> > entity_chains;
      if (! st.models.empty()) {
         for (const gemmi::Chain &chain : st.models[0].chains) {
            for (const gemmi::Residue &res : chain.residues) {
               auto it = subchain_to_entity.find(res.subchain);
               if (it == subchain_to_entity.end()) continue;
               std::vector<std::string> &v = entity_chains[it->second];
               if (std::find(v.begin(), v.end(), chain.name) == v.end())
                  v.push_back(chain.name);
            }
         }
      }

      // synonyms, keyed by entity id
      std::map<std::string, std::string> synonyms;
      for (auto row : b->find("_entity_name_com.", {"entity_id", "name"}))
         if (! is_null_value(row.str(1))) synonyms[row.str(0)] = row.str(1);

      std::vector<std::string> lines;
      int mol_id = 0;
      for (auto row : b->find("_entity.", {"id", "?type", "?pdbx_description"})) {
         std::string type = row.has(1) ? row.str(1) : std::string();
         if (! type.empty() && type != "polymer") continue;
         std::string id = row.str(0);
         std::string description = row.has(2) ? row.str(2) : std::string();

         std::vector<std::string> spec;
         spec.push_back("MOL_ID: " + std::to_string(++mol_id) + ";");
         if (! is_null_value(description))
            spec.push_back("MOLECULE: " + description + ";");
         auto ec = entity_chains.find(id);
         if (ec != entity_chains.end() && ! ec->second.empty()) {
            std::string chains;
            for (size_t i = 0; i < ec->second.size(); i++) {
               if (i) chains += ", ";
               chains += ec->second[i];
            }
            spec.push_back("CHAIN: " + chains + ";");
         }
         auto sy = synonyms.find(id);
         if (sy != synonyms.end())
            spec.push_back("SYNONYM: " + sy->second + ";");

         for (const std::string &s : spec) lines.push_back(s);
      }

      if (lines.empty()) return;
      // the very last specification is not semicolon terminated
      if (! lines.back().empty() && lines.back().back() == ';')
         lines.back().pop_back();

      // Each specification is its own line; only over-long ones are wrapped.
      std::vector<std::string> laid_out;
      for (size_t i = 0; i < lines.size(); i++) {
         std::vector<std::string> w = wrap_text(lines[i], i == 0 ? 70 : 69, 69);
         for (const std::string &s : w) laid_out.push_back(s);
      }
      emit_continued(out, "COMPND", laid_out);
   }

   void add_journal(std::vector<std::string> &out, const gemmi::cif::Block *block) {

      if (! block) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      // "primary" is the deposited entry's own publication; other rows are
      // cited references and are not what JRNL means.
      gemmi::cif::Table t = b->find("_citation.",
                                    {"id", "?title", "?journal_abbrev",
                                     "?journal_volume", "?page_first", "?year",
                                     "?journal_id_ISSN", "?pdbx_database_id_PubMed",
                                     "?pdbx_database_id_DOI"});
      if (! t.ok()) return;

      for (size_t r = 0; r < t.length(); r++) {
         gemmi::cif::Table::Row row = t[r];
         if (row.str(0) != "primary") continue;

         std::vector<std::string> names;
         for (auto ar : b->find("_citation_author.", {"citation_id", "name",
                                                      "?ordinal"}))
            if (ar.str(0) == "primary" && ! is_null_value(ar.str(1)))
               names.push_back(ar.str(1));
         emit_jrnl(out, "AUTH", join_names(names));

         if (row.has(1)) emit_jrnl(out, "TITL", row.str(1));

         // REF is column-structured: journal 20-47, "V." 50-51, volume 52-55,
         // first page 57-61, year 63-66.
         std::string journal = row.has(2) ? row.str(2) : std::string();
         std::string volume  = row.has(3) ? row.str(3) : std::string();
         std::string page    = row.has(4) ? row.str(4) : std::string();
         std::string year    = row.has(5) ? row.str(5) : std::string();
         if (! is_null_value(journal) || ! is_null_value(volume)) {
            std::vector<std::string> jlines = wrap_text(trimmed(journal), 28, 28);
            for (size_t i = 0; i < jlines.size(); i++) {
               pdb_line_t l("JRNL");
               l.put(13, "REF");
               if (i > 0) l.put_int(17, 18, static_cast<int>(i) + 1);
               l.put(20, jlines[i]);
               if (i == 0) {
                  if (! is_null_value(volume)) {
                     l.put(50, "V.");
                     l.put_right(52, 55, trimmed(volume));
                  }
                  if (! is_null_value(page))  l.put_right(57, 61, trimmed(page));
                  if (! is_null_value(year))  l.put_right(63, 66, trimmed(year));
               }
               out.push_back(l.str());
            }
            if (jlines.empty()) {
               pdb_line_t l("JRNL");
               l.put(13, "REF");
               if (! is_null_value(volume)) { l.put(50, "V."); l.put_right(52, 55, trimmed(volume)); }
               if (! is_null_value(page))  l.put_right(57, 61, trimmed(page));
               if (! is_null_value(year))  l.put_right(63, 66, trimmed(year));
               out.push_back(l.str());
            }
         }

         if (row.has(6) && ! is_null_value(row.str(6))) {
            pdb_line_t l("JRNL");
            l.put(13, "REFN");
            l.put(36, "ISSN");
            l.put(41, trimmed(row.str(6)));
            out.push_back(l.str());
         }
         if (row.has(7)) emit_jrnl(out, "PMID", row.str(7));
         if (row.has(8)) emit_jrnl(out, "DOI",  row.str(8));
         break;
      }
   }

   // REMARK 3, deliberately a SUMMARY and not a translation of the PDB's
   // refinement block. The numbers people open the browser for are the
   // resolution range, the R factors and how many reflections they came from;
   // reproducing all of REMARK 3 would mean mapping dozens of _refine and
   // _refine_ls_restr tags for no added information.
   //
   // Ordering matters: mmdb's GetResolution() scans the REMARK container and
   // gives up at the first remark numbered above 2, so REMARK 2 must already
   // be in place before any of this is added.
   void add_refinement_remarks(std::vector<std::string> &out,
                               const gemmi::cif::Block *block) {

      if (! block) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      std::string program;
      for (auto row : b->find("_software.", {"name", "?version", "?classification"})) {
         std::string cls = row.has(2) ? row.str(2) : std::string();
         if (cls != "refinement") continue;
         program = row.str(0);
         if (row.has(1) && ! is_null_value(row.str(1)))
            program += " (" + row.str(1) + ")";
         break;
      }

      std::string res_high = block_value(block, "_refine.ls_d_res_high");
      std::string res_low  = block_value(block, "_refine.ls_d_res_low");
      std::string r_work   = block_value(block, "_refine.ls_R_factor_R_work");
      if (r_work.empty()) r_work = block_value(block, "_refine.ls_R_factor_obs");
      std::string r_free   = block_value(block, "_refine.ls_R_factor_R_free");
      std::string n_refl   = block_value(block, "_refine.ls_number_reflns_obs");
      std::string b_mean   = block_value(block, "_refine.B_iso_mean");

      if (program.empty() && res_high.empty() && r_work.empty()) return;

      emit_remark(out, 3, "");
      emit_remark(out, 3, "REFINEMENT.");
      if (! program.empty())
         emit_remark(out, 3, "  PROGRAM     : " + program);
      if (! res_high.empty() || ! res_low.empty()) {
         emit_remark(out, 3, "");
         emit_remark(out, 3, " DATA USED IN REFINEMENT.");
         if (! res_high.empty())
            emit_remark(out, 3, "  RESOLUTION RANGE HIGH (ANGSTROMS) : " + res_high);
         if (! res_low.empty())
            emit_remark(out, 3, "  RESOLUTION RANGE LOW  (ANGSTROMS) : " + res_low);
         if (! n_refl.empty())
            emit_remark(out, 3, "  NUMBER OF REFLECTIONS             : " + n_refl);
      }
      if (! r_work.empty() || ! r_free.empty()) {
         emit_remark(out, 3, "");
         emit_remark(out, 3, " FIT TO DATA USED IN REFINEMENT.");
         if (! r_work.empty())
            emit_remark(out, 3, "  R VALUE     (WORKING SET) : " + r_work);
         if (! r_free.empty())
            emit_remark(out, 3, "  FREE R VALUE              : " + r_free);
      }
      if (! b_mean.empty()) {
         emit_remark(out, 3, "");
         emit_remark(out, 3, " B VALUES.");
         emit_remark(out, 3, "  MEAN B VALUE      (OVERALL, A**2) : " + b_mean);
      }
   }

   // -------------------------------------------------- secondary structure

   // One residue end of a helix or strand, already in AUTHOR space -- which is
   // the only space mmdb has.
   struct ss_res_t {
      std::string comp, chain, seq, icode;
      bool ok() const { return ! chain.empty() && ! seq.empty(); }
   };

   struct helix_rec_t {
      ss_res_t start, end;
      std::string id;
      int helix_class = 0;
      int length = 0;
   };

   struct strand_rec_t {
      std::string sheet_id;
      ss_res_t start, end;
      int sense = 0;
      size_t n_in_sheet = 0;
      size_t index_in_sheet = 0;
   };

   std::string seqid_str(const gemmi::AtomAddress &a) {
      return a.res_id.seqid.num.has_value()
         ? std::to_string(a.res_id.seqid.num.value) : std::string();
   }

   std::string icode_str(const gemmi::AtomAddress &a) {
      char c = a.res_id.seqid.icode;
      if (c == ' ' || c == '\0') return std::string();
      return std::string(1, c);
   }

   ss_res_t from_address(const gemmi::AtomAddress &a) {
      ss_res_t r;
      r.comp  = a.res_id.name;
      r.chain = a.chain_name;
      r.seq   = seqid_str(a);
      r.icode = icode_str(a);
      return r;
   }

   // label (asym_id, seq_id) -> author identity, read out of _atom_site.
   //
   // Needed because gemmi's _struct_conf / _struct_sheet_range readers key on
   // the beg_auth_*/end_auth_* columns and return NOTHING when a file gives
   // only the label_* ones -- measured: st.helices is 0 for a label-only file
   // whose _struct_conf holds 35 HELX_P rows, and 1 for the same file with the
   // auth columns added. phenix.refine writes label-only mmCIF, so this is not
   // an exotic case: every SC1_2_refine_*.cif in the corpus loses all of its
   // secondary structure that way, while its PDB sibling keeps 35 helices.
   //
   // The mapping is unambiguous and it is in the file itself, so rather than
   // accept the loss we resolve it here. This must run while _atom_site is
   // still in the block -- i.e. before the read path strips it (which is why
   // the records are synthesized at read time, not at first use).
   typedef std::map<std::pair<std::string, std::string>, ss_res_t> label_map_t;

   label_map_t build_label_map(const gemmi::cif::Block *block) {

      label_map_t m;
      if (! block) return m;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      for (auto row : b->find("_atom_site.", {"label_asym_id", "label_seq_id",
                                              "auth_asym_id", "auth_seq_id",
                                              "?pdbx_PDB_ins_code",
                                              "?auth_comp_id", "?label_comp_id"})) {
         std::string label_seq = row.str(1);
         if (is_null_value(label_seq)) continue;
         std::pair<std::string, std::string> key(row.str(0), label_seq);
         if (m.find(key) != m.end()) continue;      // first atom of the residue
         ss_res_t r;
         r.chain = row.str(2);
         r.seq   = row.str(3);
         if (row.has(4) && ! is_null_value(row.str(4))) r.icode = row.str(4);
         if (row.has(5) && ! is_null_value(row.str(5)))      r.comp = row.str(5);
         else if (row.has(6) && ! is_null_value(row.str(6))) r.comp = row.str(6);
         m[key] = r;
      }
      return m;
   }

   ss_res_t lookup_label(const label_map_t &m, const std::string &asym,
                         const std::string &seq, const std::string &comp) {
      ss_res_t r;
      auto it = m.find(std::make_pair(asym, seq));
      if (it != m.end()) r = it->second;
      if (r.comp.empty() && ! is_null_value(comp)) r.comp = comp;
      return r;
   }

   std::vector<helix_rec_t> collect_helices(const gemmi::Structure &st,
                                            const gemmi::cif::Block *block) {

      std::vector<helix_rec_t> helices;

      // gemmi drops _struct_conf.pdbx_PDB_helix_id, so take the file's own
      // helix labels back out of the document when the row count agrees --
      // "AA1" reads better than a serial number, and matches the PDB sibling.
      std::vector<std::string> ids;
      std::vector<std::string> label_rows_asym1, label_rows_seq1, label_rows_comp1;
      std::vector<std::string> label_rows_asym2, label_rows_seq2, label_rows_comp2;
      std::vector<int> label_rows_class, label_rows_length;

      if (block) {
         gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);
         for (auto row : b->find("_struct_conf.",
                                 {"conf_type_id", "?pdbx_PDB_helix_id",
                                  "?beg_label_asym_id", "?beg_label_seq_id",
                                  "?beg_label_comp_id", "?end_label_asym_id",
                                  "?end_label_seq_id", "?end_label_comp_id",
                                  "?pdbx_PDB_helix_class",
                                  "?pdbx_PDB_helix_length"})) {
            if (row.str(0).compare(0, 4, "HELX") != 0) continue;
            ids.push_back(row.has(1) && ! is_null_value(row.str(1))
                          ? row.str(1) : std::string());
            label_rows_asym1.push_back(row.has(2) ? row.str(2) : std::string());
            label_rows_seq1 .push_back(row.has(3) ? row.str(3) : std::string());
            label_rows_comp1.push_back(row.has(4) ? row.str(4) : std::string());
            label_rows_asym2.push_back(row.has(5) ? row.str(5) : std::string());
            label_rows_seq2 .push_back(row.has(6) ? row.str(6) : std::string());
            label_rows_comp2.push_back(row.has(7) ? row.str(7) : std::string());
            label_rows_class.push_back(row.has(8) && ! is_null_value(row.str(8))
                                       ? std::atoi(row.str(8).c_str()) : 0);
            label_rows_length.push_back(row.has(9) && ! is_null_value(row.str(9))
                                        ? std::atoi(row.str(9).c_str()) : 0);
         }
      }

      if (! st.helices.empty()) {
         // The normal path: gemmi resolved them, so use its typed answer.
         bool ids_usable = (ids.size() == st.helices.size());
         for (size_t i = 0; i < st.helices.size(); i++) {
            const gemmi::Helix &h = st.helices[i];
            helix_rec_t rec;
            rec.start = from_address(h.start);
            rec.end   = from_address(h.end);
            rec.id    = ids_usable ? ids[i] : std::string();
            rec.helix_class = (h.pdb_helix_class != gemmi::Helix::UnknownHelix)
               ? static_cast<int>(h.pdb_helix_class) : 0;
            rec.length = h.length > 0 ? h.length : 0;
            helices.push_back(rec);
         }
         return helices;
      }

      // The label-only fallback.
      if (ids.empty()) return helices;
      label_map_t m = build_label_map(block);
      if (m.empty()) return helices;
      size_t n_unmappable = 0;
      for (size_t i = 0; i < ids.size(); i++) {
         helix_rec_t rec;
         rec.start = lookup_label(m, label_rows_asym1[i], label_rows_seq1[i],
                                  label_rows_comp1[i]);
         rec.end   = lookup_label(m, label_rows_asym2[i], label_rows_seq2[i],
                                  label_rows_comp2[i]);
         rec.id    = ids[i];
         rec.helix_class = label_rows_class[i];
         rec.length      = label_rows_length[i];
         if (rec.start.ok() && rec.end.ok()) helices.push_back(rec);
         else n_unmappable++;
      }
      std::cout << "INFO:: secondary structure resolved from label_* ids: "
                << helices.size() << " helices";
      if (n_unmappable > 0)
         // Not an error: a helix may legitimately end on a residue with no
         // observed atoms (three do in SC1_2_refine_036), and there is nothing
         // to map it to. Said out loud rather than dropped quietly, because a
         // count that differs from the PDB sibling's would otherwise look like
         // a bug.
         std::cout << ", " << n_unmappable
                   << " skipped (an end residue is not in the model)";
      std::cout << std::endl;
      return helices;
   }

   std::vector<strand_rec_t> collect_strands(const gemmi::Structure &st,
                                             const gemmi::cif::Block *block) {

      std::vector<strand_rec_t> strands;

      size_t n_gemmi_strands = 0;
      for (const gemmi::Sheet &sheet : st.sheets) n_gemmi_strands += sheet.strands.size();

      if (n_gemmi_strands > 0) {
         for (const gemmi::Sheet &sheet : st.sheets) {
            for (size_t i = 0; i < sheet.strands.size(); i++) {
               const gemmi::Sheet::Strand &s = sheet.strands[i];
               strand_rec_t rec;
               rec.sheet_id       = sheet.name;
               rec.start          = from_address(s.start);
               rec.end            = from_address(s.end);
               rec.sense          = (i == 0) ? 0 : s.sense;
               rec.n_in_sheet     = sheet.strands.size();
               rec.index_in_sheet = i;
               strands.push_back(rec);
            }
         }
         return strands;
      }

      // Label-only fallback, exactly as for the helices: gemmi registers the
      // sheets (from _struct_sheet) but no strands, because
      // _struct_sheet_range gave it only label ids.
      if (! block) return strands;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);
      label_map_t m = build_label_map(block);
      if (m.empty()) return strands;

      // strand ordering/sense: _struct_sheet_order says how range_id_2 sits
      // relative to range_id_1. PDB's sense column means the same thing about
      // the PRECEDING strand, so key it on range_id_2.
      std::map<std::pair<std::string, std::string>, int> sense_of;
      for (auto row : b->find("_struct_sheet_order.",
                              {"sheet_id", "range_id_2", "?sense"})) {
         int sense = 0;
         if (row.has(2)) {
            std::string s = row.str(2);
            if (s == "parallel")           sense =  1;
            else if (s == "anti-parallel") sense = -1;
         }
         sense_of[std::make_pair(row.str(0), row.str(1))] = sense;
      }

      std::map<std::string, size_t> seen_in_sheet;
      std::vector<strand_rec_t> collected;
      for (auto row : b->find("_struct_sheet_range.",
                              {"sheet_id", "id",
                               "beg_label_asym_id", "beg_label_seq_id",
                               "?beg_label_comp_id",
                               "end_label_asym_id", "end_label_seq_id",
                               "?end_label_comp_id"})) {
         strand_rec_t rec;
         rec.sheet_id = row.str(0);
         rec.start = lookup_label(m, row.str(2), row.str(3),
                                  row.has(4) ? row.str(4) : std::string());
         rec.end   = lookup_label(m, row.str(5), row.str(6),
                                  row.has(7) ? row.str(7) : std::string());
         if (! rec.start.ok() || ! rec.end.ok()) continue;
         rec.index_in_sheet = seen_in_sheet[rec.sheet_id]++;
         auto it = sense_of.find(std::make_pair(rec.sheet_id, row.str(1)));
         rec.sense = (rec.index_in_sheet == 0) ? 0
            : (it != sense_of.end() ? it->second : 0);
         collected.push_back(rec);
      }
      for (strand_rec_t &rec : collected)
         rec.n_in_sheet = seen_in_sheet[rec.sheet_id];
      if (! collected.empty())
         std::cout << "INFO:: secondary structure resolved from label_* ids: "
                   << collected.size() << " strands in " << seen_in_sheet.size()
                   << " sheets" << std::endl;
      return collected;
   }

   // HELIX: serial 8-10, id 12-14, init resname 16-18, chain 20, seq 22-25,
   // icode 26, end resname 28-30, chain 32, seq 34-37, icode 38, class 39-40,
   // comment 41-70, length 72-76.
   void add_helices(std::vector<std::string> &out, const gemmi::Structure &st,
                    const gemmi::cif::Block *block) {

      std::vector<helix_rec_t> helices = collect_helices(st, block);

      for (size_t i = 0; i < helices.size(); i++) {
         const helix_rec_t &h = helices[i];
         if (! h.start.ok() || ! h.end.ok()) continue;

         pdb_line_t l("HELIX");
         l.put_int(8, 10, static_cast<int>(i) + 1);
         std::string id = h.id.empty() ? std::to_string(i + 1) : h.id;
         l.put(12, id.substr(0, 3));
         l.put(16, h.start.comp.substr(0, 3));
         l.put(20, h.start.chain.substr(0, 1));   // mmdb reads one char
         l.put_right(22, 25, h.start.seq);
         l.put(26, h.start.icode);
         l.put(28, h.end.comp.substr(0, 3));
         l.put(32, h.end.chain.substr(0, 1));
         l.put_right(34, 37, h.end.seq);
         l.put(38, h.end.icode);
         if (h.helix_class > 0) l.put_int(39, 40, h.helix_class);
         if (h.length > 0) l.put_int(72, 76, h.length);
         out.push_back(l.str());
      }
   }

   // SHEET: strand 8-10, sheet id 12-14, n strands 15-16, init resname 18-20,
   // chain 22, seq 23-26, icode 27, end resname 29-31, chain 33, seq 34-37,
   // icode 38, sense 39-40. The two registration atoms (columns 42-70) are
   // deliberately left out: gemmi carries them only when the file has
   // _pdbx_struct_sheet_hbond, no corpus file does, and nothing in Coot reads
   // them.
   void add_sheets(std::vector<std::string> &out, const gemmi::Structure &st,
                   const gemmi::cif::Block *block) {

      for (const strand_rec_t &s : collect_strands(st, block)) {
         if (! s.start.ok() || ! s.end.ok()) continue;

         pdb_line_t l("SHEET");
         // mmdb rejects strand number 0 outright (Error_WrongStrandNo), and it
         // uses the number as an index into the sheet's strand array, so this
         // is 1-based per sheet rather than a running total.
         l.put_int(8, 10, static_cast<int>(s.index_in_sheet) + 1);
         l.put(12, s.sheet_id.substr(0, 3));
         l.put_int(15, 16, static_cast<int>(s.n_in_sheet));
         l.put(18, s.start.comp.substr(0, 3));
         l.put(22, s.start.chain.substr(0, 1));
         l.put_right(23, 26, s.start.seq);
         l.put(27, s.start.icode);
         l.put(29, s.end.comp.substr(0, 3));
         l.put(33, s.end.chain.substr(0, 1));
         l.put_right(34, 37, s.end.seq);
         l.put(38, s.end.icode);
         l.put_int(39, 40, s.sense);
         out.push_back(l.str());
      }
   }
}  // anonymous namespace


std::vector<std::string>
coot::pdb_header_records_from_mmcif(const gemmi::Structure &st,
                                    const gemmi::cif::Block *block) {

   std::vector<std::string> out;

   add_header(out, st);
   emit_wrapped(out, "TITLE",  st.get_info("_struct.title"));
   add_compound(out, st, block);
   emit_wrapped(out, "KEYWDS", st.get_info("_struct_keywords.text"));
   emit_wrapped(out, "EXPDTA", st.get_info("_exptl.method"));
   add_authors(out, block);
   add_journal(out, block);
   add_refinement_remarks(out, block);
   add_helices(out, st, block);
   add_sheets(out, st, block);

   return out;
}
