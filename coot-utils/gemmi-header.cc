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

#include <mmdb2/mmdb_manager.h>

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

   // The 4-character entry id a PDB record can hold.
   //
   // wwPDB is moving to 12-character extended accession codes. For an entry
   // that also has a legacy id, the extended form carries it in the TAIL:
   //
   //     pdb_00001bna  ->  1bna
   //
   // so truncating from the head gives "pdb_", which is identical for every
   // extended entry and identifies nothing. Take the tail instead.
   //
   // An extended code whose eight characters do NOT begin with 0000 has no
   // 4-character equivalent at all, and the PDB format cannot express it. That
   // case is reported rather than fudged -- inventing four characters would
   // produce a plausible-looking id belonging to some other entry.
   std::string pdb_four_char_id(const std::string &entry_id) {

      std::string lower = entry_id;
      for (char &c : lower) c = std::tolower(static_cast<unsigned char>(c));

      if (lower.size() == 12 && lower.compare(0, 4, "pdb_") == 0) {
         if (lower.compare(4, 4, "0000") == 0) {
            // The legacy id, upper-cased. Extended codes are written lower case
            // (pdb_00005rsh), but a PDB entry id is upper case by convention, so
            // passing the tail through as-is would emit "1bna" where every
            // deposited file has "1BNA".
            std::string id = lower.substr(8);
            for (char &c : id) c = std::toupper(static_cast<unsigned char>(c));
            return id;
         }
         std::cout << "WARNING:: extended accession code " << entry_id
                   << " has no 4-character equivalent; the PDB header cannot "
                   << "carry it" << std::endl;
         return std::string();
      }
      return entry_id.substr(0, 4);
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
      l.put(63, pdb_four_char_id(id));
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

   // ---------------------------------------------------------------- entities
   //
   // COMPND and SOURCE are two views of one list: a MOL_ID per polymer entity,
   // numbered in file order, and the two records have to agree on that
   // numbering or they describe different molecules. FORMUL needs the same
   // table for a different reason -- its component number is the entity id,
   // and whether the record takes an asterisk depends on the entity being
   // water. So _entity is read once, here, and the record builders share it.
   struct entity_t {
      std::string id, type, src_method, description, ec, mutation, fragment;
      std::string synonym, details;
      std::vector<std::string> chains;   // author chain ids, taken from the model
      int mol_id;                        // COMPND / SOURCE MOL_ID; polymers only
      entity_t() : mol_id(0) {}
      // An absent _entity.type is treated as polymer rather than skipped: the
      // tag is optional, and a file that omits it still has a molecule.
      bool is_polymer() const { return type.empty() || type == "polymer"; }
      bool is_water()   const { return type == "water"; }
   };

   std::vector<entity_t> collect_entities(const gemmi::Structure &st,
                                          const gemmi::cif::Block *block) {

      std::vector<entity_t> entities;
      if (! block) return entities;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      for (auto row : b->find("_entity.", {"id", "?type", "?pdbx_description",
                                           "?src_method", "?pdbx_ec",
                                           "?pdbx_mutation", "?pdbx_fragment",
                                           "?details"})) {
         entity_t e;
         e.id = row.str(0);
         if (row.has(1) && ! is_null_value(row.str(1))) e.type        = row.str(1);
         if (row.has(2) && ! is_null_value(row.str(2))) e.description = row.str(2);
         if (row.has(3) && ! is_null_value(row.str(3))) e.src_method  = row.str(3);
         if (row.has(4) && ! is_null_value(row.str(4))) e.ec          = row.str(4);
         if (row.has(5) && ! is_null_value(row.str(5))) e.mutation    = row.str(5);
         if (row.has(6) && ! is_null_value(row.str(6))) e.fragment    = row.str(6);
         if (row.has(7) && ! is_null_value(row.str(7))) e.details     = row.str(7);
         entities.push_back(e);
      }
      if (entities.empty()) return entities;

      for (auto row : b->find("_entity_name_com.", {"entity_id", "name"}))
         for (entity_t &e : entities)
            if (e.id == row.str(0) && ! is_null_value(row.str(1)))
               e.synonym = row.str(1);

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

      int mol_id = 0;
      for (entity_t &e : entities) {
         auto it = entity_chains.find(e.id);
         if (it != entity_chains.end()) e.chains = it->second;
         if (e.is_polymer()) e.mol_id = ++mol_id;
      }
      return entities;
   }

   // The shared shape of COMPND and SOURCE: "TOKEN: value;" specifications, one
   // per line, semicolon terminated except the very last of the whole record.
   void emit_specifications(std::vector<std::string> &out, const char *record,
                            std::vector<std::string> lines) {

      if (lines.empty()) return;
      if (! lines.back().empty() && lines.back().back() == ';')
         lines.back().pop_back();

      // Each specification is its own line; only over-long ones are wrapped.
      std::vector<std::string> laid_out;
      for (size_t i = 0; i < lines.size(); i++) {
         std::vector<std::string> w = wrap_text(lines[i], i == 0 ? 70 : 69, 69);
         for (const std::string &s : w) laid_out.push_back(s);
      }
      emit_continued(out, record, laid_out);
   }

   // COMPND. Only polymer entities get a specification -- a PDB file describes
   // ligands in HETNAM and FORMUL, not COMPND, and inventing entries for waters
   // and ions would make the record say more than the file does.
   //
   // Token order follows the format description, which is also the order wwPDB
   // writes them in (verified against pdb3k0n.ent: MOL_ID, MOLECULE, CHAIN,
   // SYNONYM, EC, ENGINEERED).
   void add_compound(std::vector<std::string> &out,
                     const std::vector<entity_t> &entities) {

      std::vector<std::string> lines;
      for (const entity_t &e : entities) {
         if (! e.is_polymer()) continue;
         lines.push_back("MOL_ID: " + std::to_string(e.mol_id) + ";");
         if (! e.description.empty())
            lines.push_back("MOLECULE: " + e.description + ";");
         if (! e.chains.empty()) {
            std::string chains;
            for (size_t i = 0; i < e.chains.size(); i++) {
               if (i) chains += ", ";
               chains += e.chains[i];
            }
            lines.push_back("CHAIN: " + chains + ";");
         }
         if (! e.fragment.empty()) lines.push_back("FRAGMENT: " + e.fragment + ";");
         if (! e.synonym.empty())  lines.push_back("SYNONYM: " + e.synonym + ";");
         if (! e.ec.empty())       lines.push_back("EC: " + e.ec + ";");
         // _entity.src_method is an enum: man = genetically manipulated, which
         // is exactly what ENGINEERED reports; nat and syn are not.
         if (e.src_method == "man")  lines.push_back("ENGINEERED: YES;");
         if (! e.mutation.empty())   lines.push_back("MUTATION: YES;");
         // OTHER_DETAILS last, as in pdb2gew.ent ("FAD COFACTOR
         // NON-COVALENTLY BOUND TO THE ENZYME").
         if (! e.details.empty())    lines.push_back("OTHER_DETAILS: " + e.details + ";");
      }
      emit_specifications(out, "COMPND", lines);
   }

   // ------------------------------------------------------------------ source
   //
   // Three mmCIF categories describe where a polymer came from, and a file uses
   // whichever fits each entity: _entity_src_nat (isolated from the organism),
   // _entity_src_gen (expressed in a host) and _pdbx_entity_src_syn
   // (synthesized). The PDB folds all three into SOURCE's token vocabulary,
   // which is why the mapping is three tables rather than one.
   //
   // Table order IS emission order, and it follows the format description --
   // the natural source first, then the expression system. wwPDB writes them
   // that way too (pdb3k0n.ent: ORGANISM_SCIENTIFIC, ORGANISM_COMMON,
   // ORGANISM_TAXID, GENE, EXPRESSION_SYSTEM, EXPRESSION_SYSTEM_TAXID).
   struct src_tag_t { const char *tag; const char *token; };

   const src_tag_t src_nat_map[] = {
      { "pdbx_fragment",           "FRAGMENT" },
      { "pdbx_organism_scientific","ORGANISM_SCIENTIFIC" },
      { "common_name",             "ORGANISM_COMMON" },
      { "pdbx_ncbi_taxonomy_id",   "ORGANISM_TAXID" },
      { "strain",                  "STRAIN" },
      { "pdbx_variant",            "VARIANT" },
      { "pdbx_cell_line",          "CELL_LINE" },
      { "pdbx_atcc",               "ATCC" },
      { "pdbx_organ",              "ORGAN" },
      { "tissue",                  "TISSUE" },
      { "pdbx_cell",               "CELL" },
      { "pdbx_organelle",          "ORGANELLE" },
      { "pdbx_secretion",          "SECRETION" },
      { "pdbx_cellular_location",  "CELLULAR_LOCATION" },
      { "pdbx_plasmid_name",       "PLASMID" },
      { "details",                 "OTHER_DETAILS" },
      { nullptr, nullptr }
   };

   const src_tag_t src_gen_map[] = {
      { "pdbx_gene_src_fragment",          "FRAGMENT" },
      { "pdbx_gene_src_scientific_name",   "ORGANISM_SCIENTIFIC" },
      { "gene_src_common_name",            "ORGANISM_COMMON" },
      { "pdbx_gene_src_ncbi_taxonomy_id",  "ORGANISM_TAXID" },
      { "gene_src_strain",                 "STRAIN" },
      { "pdbx_gene_src_variant",           "VARIANT" },
      { "pdbx_gene_src_cell_line",         "CELL_LINE" },
      { "pdbx_gene_src_atcc",              "ATCC" },
      { "pdbx_gene_src_organ",             "ORGAN" },
      { "gene_src_tissue",                 "TISSUE" },
      { "pdbx_gene_src_cell",              "CELL" },
      { "pdbx_gene_src_organelle",         "ORGANELLE" },
      { "pdbx_gene_src_cellular_location", "CELLULAR_LOCATION" },
      { "pdbx_gene_src_gene",              "GENE" },
      { "pdbx_host_org_scientific_name",   "EXPRESSION_SYSTEM" },
      { "host_org_common_name",            "EXPRESSION_SYSTEM_COMMON" },
      { "pdbx_host_org_ncbi_taxonomy_id",  "EXPRESSION_SYSTEM_TAXID" },
      { "pdbx_host_org_strain",            "EXPRESSION_SYSTEM_STRAIN" },
      { "pdbx_host_org_variant",           "EXPRESSION_SYSTEM_VARIANT" },
      { "pdbx_host_org_cell_line",         "EXPRESSION_SYSTEM_CELL_LINE" },
      { "pdbx_host_org_atcc",              "EXPRESSION_SYSTEM_ATCC_NUMBER" },
      { "pdbx_host_org_organ",             "EXPRESSION_SYSTEM_ORGAN" },
      { "pdbx_host_org_tissue",            "EXPRESSION_SYSTEM_TISSUE" },
      { "pdbx_host_org_cell",              "EXPRESSION_SYSTEM_CELL" },
      { "pdbx_host_org_organelle",         "EXPRESSION_SYSTEM_ORGANELLE" },
      { "pdbx_host_org_cellular_location", "EXPRESSION_SYSTEM_CELLULAR_LOCATION" },
      { "pdbx_host_org_vector_type",       "EXPRESSION_SYSTEM_VECTOR_TYPE" },
      { "pdbx_host_org_vector",            "EXPRESSION_SYSTEM_VECTOR" },
      // plasmid_name in _entity_src_gen is the EXPRESSION plasmid; the natural
      // source's plasmid is _entity_src_nat.pdbx_plasmid_name, above.
      { "plasmid_name",                    "EXPRESSION_SYSTEM_PLASMID" },
      { "pdbx_host_org_gene",              "EXPRESSION_SYSTEM_GENE" },
      { "host_org_details",                "OTHER_DETAILS" },
      { nullptr, nullptr }
   };

   const src_tag_t src_syn_map[] = {
      { "pdbx_fragment",       "FRAGMENT" },
      { "organism_scientific", "ORGANISM_SCIENTIFIC" },
      { "organism_common_name","ORGANISM_COMMON" },
      { "ncbi_taxonomy_id",    "ORGANISM_TAXID" },
      { "strain",              "STRAIN" },
      { "details",             "OTHER_DETAILS" },
      { nullptr, nullptr }
   };

   // entity id -> its "TOKEN: value;" lines, for one of the three categories.
   void collect_source_tokens(std::map<std::string, std::vector<std::string> > &tokens,
                              const gemmi::cif::Block *block,
                              const char *category, const src_tag_t *map,
                              bool synthetic) {

      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      std::vector<std::string> wanted;
      wanted.push_back("entity_id");
      for (const src_tag_t *m = map; m->tag; m++)
         wanted.push_back(std::string("?") + m->tag);

      for (auto row : b->find(category, wanted)) {
         std::vector<std::string> &lines = tokens[row.str(0)];
         // SYNTHETIC has no tag of its own: the category IS the statement.
         if (synthetic) lines.push_back("SYNTHETIC: YES;");
         size_t i = 1;
         for (const src_tag_t *m = map; m->tag; m++, i++)
            if (row.has(i) && ! is_null_value(row.str(i)))
               lines.push_back(std::string(m->token) + ": " + trimmed(row.str(i)) + ";");
      }
   }

   void add_source(std::vector<std::string> &out, const gemmi::cif::Block *block,
                   const std::vector<entity_t> &entities) {

      if (! block) return;

      std::map<std::string, std::vector<std::string> > tokens;
      collect_source_tokens(tokens, block, "_entity_src_nat.",     src_nat_map, false);
      collect_source_tokens(tokens, block, "_entity_src_gen.",     src_gen_map, false);
      collect_source_tokens(tokens, block, "_pdbx_entity_src_syn.",src_syn_map, true);
      if (tokens.empty()) return;

      // MOL_ID order, not category order: SOURCE is read alongside COMPND.
      std::vector<std::string> lines;
      for (const entity_t &e : entities) {
         if (! e.is_polymer()) continue;
         auto it = tokens.find(e.id);
         if (it == tokens.end() || it->second.empty()) continue;
         lines.push_back("MOL_ID: " + std::to_string(e.mol_id) + ";");
         for (const std::string &s : it->second) lines.push_back(s);
      }
      emit_specifications(out, "SOURCE", lines);
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

   // ------------------------------------------------------------------ revdat
   //
   // REVDAT: modNum 8-10, date 14-22, entry id 24-27, modType 32, and four
   // record-name slots at 40-45 / 47-52 / 54-59 / 61-66 (mmdb reads them at
   // &S[39+i*7], which is the same thing).
   //
   // THE RECORD-NAME SLOTS ARE LEFT EMPTY, DELIBERATELY. mmCIF does say what
   // each revision changed -- _pdbx_audit_revision_group.group -- but as prose
   // ("Database references", "Refinement description", "Version format
   // compliance"), and those columns hold PDB RECORD names (JRNL, VERSN,
   // REMARK). There is no mapping between the two: 3K0N's revision 1.2 is
   // "Database references" in the mmCIF and "JRNL VERSN" in the deposited PDB.
   // Inventing one would be composition, not translation. Everything the
   // category does state -- which revision, when, and whether it was the
   // initial release -- is carried.
   //
   // Newest first, as wwPDB writes them.
   void add_revdat(std::vector<std::string> &out, const gemmi::Structure &st,
                   const gemmi::cif::Block *block) {

      if (! block) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      std::string id = pdb_four_char_id(st.get_info("_entry.id"));

      struct rev_t { int num; std::string date; int type; };
      std::vector<rev_t> revisions;

      for (auto row : b->find("_pdbx_audit_revision_history.",
                              {"ordinal", "?data_content_type", "?revision_date",
                               "?major_revision", "?minor_revision"})) {
         // A file may also carry revision histories for other content types
         // (chemical component definitions, structure factors); REVDAT is
         // about the coordinate entry.
         if (row.has(1) && ! is_null_value(row.str(1)) &&
             row.str(1) != "Structure model") continue;
         if (! row.has(2) || is_null_value(row.str(2))) continue;
         std::string date = pdb_date(row.str(2));
         if (date.empty()) continue;

         rev_t r;
         r.num  = std::atoi(row.str(0).c_str());
         r.date = date;
         // modType 0 marks the initial release, 1 any later revision. Keyed on
         // the version rather than the ordinal, which need not start at 1.
         bool have_version = row.has(3) && row.has(4) &&
            ! is_null_value(row.str(3)) && ! is_null_value(row.str(4));
         bool initial = have_version
            ? (std::atoi(row.str(3).c_str()) <= 1 && std::atoi(row.str(4).c_str()) == 0)
            : (r.num == 1);
         r.type = initial ? 0 : 1;
         revisions.push_back(r);
      }

      for (size_t i = revisions.size(); i > 0; i--) {
         const rev_t &r = revisions[i - 1];
         pdb_line_t l("REVDAT");
         l.put_int(8, 10, r.num);
         l.put(14, r.date);
         l.put(24, id);
         l.put_int(32, 32, r.type);
         out.push_back(l.str());
      }
   }

   // ------------------------------------------------------------------- dbref
   //
   // DBREF: entry id 8-11, chain 13, seq begin 15-18 + icode 19, seq end 21-24
   // + icode 25, database 27-32, accession 34-41, db id code 43-54, db seq
   // begin 56-60 + icode 61, db seq end 63-67 + icode 68. Confirmed against
   // mmdb's own reader (DBReference::ConvertPDBASCII), which parses exactly
   // those offsets.
   //
   // TWO THINGS mmdb DOES WITH THIS RECORD THAT CONSTRAIN US:
   //
   //  - it compares columns 8-11 against the entry id already stored and
   //    returns Error_WrongEntryID on a mismatch, so the id here has to be the
   //    same _entry.id that HEADER used (hence the same substr(0,4));
   //  - Model::ConvertPDBString calls GetChainCreate() for the chain in column
   //    13, so a DBREF naming a chain that is not in the model would INVENT an
   //    empty chain -- the same class of artifact as mmdb's phantom empty-id
   //    chain. Hence the guard against the model's own chain names.
   void add_dbref(std::vector<std::string> &out, const gemmi::Structure &st,
                  const gemmi::cif::Block *block) {

      if (! block) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      std::string id = pdb_four_char_id(st.get_info("_entry.id"));

      // ref id -> the database it points into. _struct_ref_seq carries the
      // alignment, _struct_ref the database identity.
      struct ref_t { std::string db_name, db_code, accession; };
      std::map<std::string, ref_t> refs;
      for (auto row : b->find("_struct_ref.", {"id", "?db_name", "?db_code",
                                               "?pdbx_db_accession"})) {
         ref_t r;
         if (row.has(1) && ! is_null_value(row.str(1))) r.db_name   = row.str(1);
         if (row.has(2) && ! is_null_value(row.str(2))) r.db_code   = row.str(2);
         if (row.has(3) && ! is_null_value(row.str(3))) r.accession = row.str(3);
         refs[row.str(0)] = r;
      }
      if (refs.empty()) return;

      std::set<std::string> model_chains;
      if (! st.models.empty())
         for (const gemmi::Chain &chain : st.models[0].chains)
            model_chains.insert(chain.name);

      size_t n_skipped = 0;
      for (auto row : b->find("_struct_ref_seq.",
                              {"ref_id", "?pdbx_strand_id",
                               "?pdbx_auth_seq_align_beg",
                               "?pdbx_auth_seq_align_end",
                               "?pdbx_seq_align_beg_ins_code",
                               "?pdbx_seq_align_end_ins_code",
                               "?pdbx_db_accession",
                               "?db_align_beg", "?db_align_end",
                               "?pdbx_db_align_beg_ins_code",
                               "?pdbx_db_align_end_ins_code"})) {

         auto it = refs.find(row.str(0));
         if (it == refs.end()) continue;

         std::string chain = row.has(1) ? trimmed(row.str(1)) : std::string();
         if (chain.empty() || is_null_value(chain)) continue;
         if (! model_chains.empty() && ! model_chains.count(chain)) {
            n_skipped++;
            continue;
         }
         // The PDB numbers DBREF in AUTHOR space, which is what
         // pdbx_auth_seq_align_* holds; seq_align_* is label numbering and
         // would silently mis-state the range.
         if (! row.has(2) || is_null_value(row.str(2))) continue;
         if (! row.has(3) || is_null_value(row.str(3))) continue;

         std::string accession = it->second.accession;
         if (row.has(6) && ! is_null_value(row.str(6))) accession = row.str(6);

         pdb_line_t l("DBREF");
         l.put(8, id);
         l.put(13, chain.substr(0, 1));
         l.put_right(15, 18, trimmed(row.str(2)));
         if (row.has(4) && ! is_null_value(row.str(4))) l.put(19, row.str(4));
         l.put_right(21, 24, trimmed(row.str(3)));
         if (row.has(5) && ! is_null_value(row.str(5))) l.put(25, row.str(5));
         l.put(27, it->second.db_name.substr(0, 6));
         l.put(34, accession.substr(0, 8));
         l.put(43, it->second.db_code.substr(0, 12));
         if (row.has(7) && ! is_null_value(row.str(7)))
            l.put_right(56, 60, trimmed(row.str(7)));
         if (row.has(9) && ! is_null_value(row.str(9))) l.put(61, row.str(9));
         if (row.has(8) && ! is_null_value(row.str(8)))
            l.put_right(63, 67, trimmed(row.str(8)));
         if (row.has(10) && ! is_null_value(row.str(10))) l.put(68, row.str(10));
         out.push_back(l.str());
      }
      if (n_skipped > 0)
         std::cout << "INFO:: " << n_skipped
                   << " DBREF row(s) name a chain that is not in the model"
                   << std::endl;
   }

   // --------------------------------------------------------- het compounds
   //
   // HETNAM: hetID 12-14, text from 16. FORMUL: component number 9-10, hetID
   // 13-15, asterisk 19, text from 20.
   //
   // WARNING: FORMUL's COMPONENT NUMBER IS WRITTEN AT 10-11 HERE, NOT AT THE 9-10 THE
   // FORMAT DESCRIPTION GIVES, AND THAT IS DELIBERATE: mmdb WRITES it at 9-10
   // (`sprintf("FORMUL  %2i  %3s    ")`) but READS it at &S[9] width 2, which
   // is columns 10-11 -- so mmdb misreads its own output, and every real
   // two-digit FORMUL, by one column (pdb1aon.ent's "FORMUL  22   MG" is
   // stored as component 2). Since these synthesized lines are only ever fed to
   // mmdb's reader and never written to a file, matching the reader is what
   // makes the value survive; mmdb's writer then puts it back at 9-10 where
   // wwPDB has it. One-digit numbers land in column 11 either way.
   //
   // WARNING: THE COMPONENT NUMBER IS THE SUBCHAIN ORDINAL, NOT THE ENTITY ID. This
   // was measured, after the entity id turned out to be wrong: the number is
   // the 1-based position of the component's first label_asym_id in
   // _struct_asym. Three depositions agree and the entity id matches none of
   // them beyond the trivial case --
   //
   //   5E1N  MSE 1, MPD 2, CA 4, HOH 9   (entity ids 1, 2, 3, 4)
   //   3NYD  3NY 3, ACT 4, SO4 7, HOH 11 (entity ids 2, 3, 4, 5)
   //   3K0N  HOH 2                       (entity id 2 -- agrees by luck)
   //
   // The subchains are what the numbering counts because a component with
   // several copies gets several subchains: 5E1N's five calciums are
   // _struct_asym D-H, so the next component starts at 9. _struct_asym is used
   // rather than first-appearance order in the coordinate loop because it also
   // lists subchains with no observed atoms, and wwPDB's numbering counts those.
   //
   // A component can belong to a POLYMER entity -- 5E1N's 8 selenomethionines
   // are in subchain A and the deposited PDB says
   // "FORMUL   1  MSE    8(C5 H11 N O2 SE)" -- so the mapping is per component
   // out of _atom_site, not per entity out of _pdbx_entity_nonpoly.
   //
   // The count is the number of such residues in model 1, counted from the
   // model itself. _entity.pdbx_number_of_molecules would agree for waters and
   // ligands but says nothing about a modified residue inside a polymer.
   // The two formats spell a formula's net charge differently, and the
   // difference is the last token: mmCIF signs it first and lets an unsigned
   // integer mean positive ("C2 H3 O2 -1", "Ca 2", "C6 H10 N3 O2 1"), the PDB
   // signs it last ("C2 H3 O2 1-", "CA 2+"). A trailing lone integer is
   // unambiguous -- every other token in a formula is an element symbol with an
   // optional count, so it cannot be a bare number -- which is what makes this
   // a rewrite rather than a guess. Element case is left as the file wrote it,
   // as everywhere else here; wwPDB upper-cases the whole record.
   std::string pdb_formula(const std::string &formula) {

      size_t sp = formula.find_last_of(' ');
      std::string last = (sp == std::string::npos) ? formula : formula.substr(sp + 1);
      if (last.empty()) return formula;

      char sign = '+';
      size_t digits_at = 0;
      if (last[0] == '-' || last[0] == '+') { sign = last[0]; digits_at = 1; }
      if (digits_at >= last.size()) return formula;
      for (size_t i = digits_at; i < last.size(); i++)
         if (! std::isdigit(static_cast<unsigned char>(last[i]))) return formula;

      std::string head = (sp == std::string::npos) ? std::string() : formula.substr(0, sp + 1);
      return head + last.substr(digits_at) + std::string(1, sign);
   }

   void add_het_compounds(std::vector<std::string> &out, const gemmi::Structure &st,
                          const gemmi::cif::Block *block,
                          const std::vector<entity_t> &entities) {

      if (! block || st.models.empty()) return;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      // Components present in the model, with how many copies.
      std::map<std::string, int> counts;
      for (const gemmi::Chain &chain : st.models[0].chains)
         for (const gemmi::Residue &res : chain.residues)
            counts[res.name]++;
      if (counts.empty()) return;

      // subchain -> its 1-based position, which is the component number.
      std::map<std::string, int> asym_ordinal;
      for (auto row : b->find("_struct_asym.", {"id"}))
         asym_ordinal.insert(std::make_pair(row.str(0),
                                            static_cast<int>(asym_ordinal.size()) + 1));

      // comp id -> its first subchain and entity, from the coordinate loop.
      // This has to run while _atom_site is still in the block, which is why
      // the whole header is synthesized at read time (gemmi-coords.cc
      // adjustment 9). If _struct_asym was missing, first-appearance order here
      // is the best available stand-in for it.
      std::map<std::string, std::string> comp_asym, comp_entity;
      std::vector<std::string> asym_order;
      for (auto row : b->find("_atom_site.", {"label_comp_id", "?label_asym_id",
                                              "?label_entity_id"})) {
         if (row.has(1) && ! is_null_value(row.str(1))) {
            comp_asym.insert(std::make_pair(row.str(0), row.str(1)));
            if (asym_ordinal.empty() &&
                std::find(asym_order.begin(), asym_order.end(), row.str(1)) ==
                asym_order.end())
               asym_order.push_back(row.str(1));
         }
         if (row.has(2) && ! is_null_value(row.str(2)))
            comp_entity.insert(std::make_pair(row.str(0), row.str(2)));
      }
      if (asym_ordinal.empty())
         for (size_t i = 0; i < asym_order.size(); i++)
            asym_ordinal[asym_order[i]] = static_cast<int>(i) + 1;

      std::map<std::string, bool> entity_is_water;
      for (const entity_t &e : entities) entity_is_water[e.id] = e.is_water();

      for (auto row : b->find("_chem_comp.", {"id", "?mon_nstd_flag", "?name",
                                              "?formula"})) {
         std::string comp = row.str(0);
         // mon_nstd_flag "y" marks a standard monomer, which a PDB file leaves
         // to SEQRES. Everything else -- ligands, ions, water, modified
         // residues -- is a het component and gets HETNAM/FORMUL.
         if (row.has(1) && row.str(1) == "y") continue;
         auto c = counts.find(comp);
         if (c == counts.end()) continue;          // declared but not present

         std::string name    = (row.has(2) && ! is_null_value(row.str(2)))
            ? trimmed(row.str(2)) : std::string();
         std::string formula = (row.has(3) && ! is_null_value(row.str(3)))
            ? trimmed(row.str(3)) : std::string();
         if (name.empty() && formula.empty()) continue;

         bool water = (comp == "HOH");
         auto e = comp_entity.find(comp);
         if (e != comp_entity.end()) {
            auto w = entity_is_water.find(e->second);
            if (w != entity_is_water.end() && w->second) water = true;
         }

         int comp_num = 0;
         auto a = comp_asym.find(comp);
         if (a != comp_asym.end()) {
            auto o = asym_ordinal.find(a->second);
            if (o != asym_ordinal.end()) comp_num = o->second;
         }
         // Last resort only: better a plausible number than none, and a file
         // with neither _struct_asym nor label_asym_id is already unusual.
         if (comp_num == 0 && e != comp_entity.end())
            comp_num = std::atoi(e->second.c_str());

         // No HETNAM for water: wwPDB does not write one (verified on 3K0N and
         // 5E1N, which name MSE, MPD and CA but never HOH), and "HOH WATER"
         // tells a reader nothing it did not know.
         if (! name.empty() && ! water) {
            // WRAPPED, because a chemical name routinely exceeds one line and
            // pdb_line_t silently clips at column 80. 6DMH's MER came out as
            // "...pyrrolidin-3-yl]sulfanyl" and the rest of the name was LOST;
            // wwPDB wraps the same name over three lines. Found 2026-08-20 by
            // --header-check against the deposition.
            //
            // Continuation number in 9-10, hetID repeated in 12-14, text from
            // column 16 (17 on continuations, which is what wwPDB does and what
            // gives the concatenated fragments a separating space, since
            // mmdb's ConvertHETNAM appends them). wrap_text hard-splits a token
            // longer than the field, which is exactly what a chemical name with
            // no spaces in it needs.
            std::vector<std::string> lines = wrap_text(name, 55, 54);
            for (size_t il = 0; il < lines.size(); il++) {
               pdb_line_t l("HETNAM");
               if (il > 0) l.put_int(9, 10, static_cast<int>(il) + 1);
               l.put_right(12, 14, comp.substr(0, 3));
               l.put(il == 0 ? 16 : 17, lines[il]);
               out.push_back(l.str());
            }
         }
         if (! formula.empty()) {
            pdb_line_t l("FORMUL");
            if (comp_num > 0) l.put_right(10, 11, std::to_string(comp_num));
            l.put_right(13, 15, comp.substr(0, 3));
            // The asterisk means water, and only water.
            if (water) l.put(19, "*");
            // A single molecule of the component is written BARE, with no
            // count and no parentheses: wwPDB writes "FORMUL      FAD    C27
            // H33 N9 O15 P2", not "1(C27 ...)". Found 2026-08-20 by
            // --header-check against pdb1aon.ent.
            std::string f = pdb_formula(formula);
            l.put(20, c->second == 1 ? f
                                     : std::to_string(c->second) + "(" + f + ")");
            out.push_back(l.str());
         }
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

   // Two indices over _atom_site: one keyed on (label_asym_id, label_seq_id),
   // one on (auth_asym_id, auth_seq_id). Both are needed because a file that
   // offers only the beg_label_*/end_label_* columns does not necessarily put
   // LABEL ids in them: phenix.refine writes AUTHOR numbering under the label
   // tag names. Measured on SC1_2_refine_031.cif -- _struct_sheet_range says
   // "VAL B 71", and in _atom_site auth B 71 is VAL while label B 71 is ALA.
   // Taking the tag name at its word slides every strand of chains B/C/D by
   // four residues, so which index to believe is settled by a vote over the
   // endpoint residue names -- see score_ss_columns() below.
   struct ss_index_t {
      label_map_t by_label;
      label_map_t by_auth;
      bool prefer_auth = false;   // set by the vote below
      bool empty() const { return by_label.empty() && by_auth.empty(); }
   };

   // The vote: score each reading of the label_* columns by how many endpoint
   // residue names it gets right, over every _struct_conf and
   // _struct_sheet_range row in the file, and then use the winner for ALL of
   // them. Deciding per row would split a strand whose two endpoints happen to
   // agree under both readings -- "VAL B 71 THR B 79" came out as "VAL B 71
   // THR B 83" that way, because THR sits at label 79 AND at auth 83.
   void score_ss_columns(gemmi::cif::Block *b, const char *cat,
                         const ss_index_t &idx, int *n_label, int *n_auth) {

      const char *pre[2] = { "beg", "end" };
      for (int k = 0; k < 2; k++) {
         std::vector<std::string> tags;
         tags.push_back(std::string(pre[k]) + "_label_asym_id");
         tags.push_back(std::string(pre[k]) + "_label_seq_id");
         tags.push_back(std::string("?") + pre[k] + "_label_comp_id");
         for (auto row : b->find(cat, tags)) {
            if (! row.has(2)) continue;
            std::string comp = row.str(2);
            if (is_null_value(comp)) continue;
            std::pair<std::string, std::string> key(row.str(0), row.str(1));
            auto il = idx.by_label.find(key);
            if (il != idx.by_label.end() && il->second.comp == comp) (*n_label)++;
            auto ia = idx.by_auth.find(key);
            if (ia != idx.by_auth.end() && ia->second.comp == comp) (*n_auth)++;
         }
      }
   }

   ss_index_t build_ss_index(const gemmi::cif::Block *block) {

      ss_index_t idx;
      if (! block) return idx;
      gemmi::cif::Block *b = const_cast<gemmi::cif::Block *>(block);

      for (auto row : b->find("_atom_site.", {"label_asym_id", "label_seq_id",
                                              "auth_asym_id", "auth_seq_id",
                                              "?pdbx_PDB_ins_code",
                                              "?auth_comp_id", "?label_comp_id"})) {
         ss_res_t r;
         r.chain = row.str(2);
         r.seq   = row.str(3);
         if (row.has(4) && ! is_null_value(row.str(4))) r.icode = row.str(4);
         if (row.has(5) && ! is_null_value(row.str(5)))      r.comp = row.str(5);
         else if (row.has(6) && ! is_null_value(row.str(6))) r.comp = row.str(6);

         std::string label_seq = row.str(1);
         if (! is_null_value(label_seq)) {
            std::pair<std::string, std::string> key(row.str(0), label_seq);
            // first atom of the residue wins
            if (idx.by_label.find(key) == idx.by_label.end()) idx.by_label[key] = r;
         }
         std::pair<std::string, std::string> akey(r.chain, r.seq);
         if (idx.by_auth.find(akey) == idx.by_auth.end()) idx.by_auth[akey] = r;
      }

      int n_label = 0, n_auth = 0;
      score_ss_columns(b, "_struct_conf.",        idx, &n_label, &n_auth);
      score_ss_columns(b, "_struct_sheet_range.", idx, &n_label, &n_auth);
      idx.prefer_auth = (n_auth > n_label);
      if (idx.prefer_auth)
         std::cout << "INFO:: secondary structure label_* columns hold AUTHOR "
                   << "numbering (" << n_auth << " residue names matched vs "
                   << n_label << " read as label ids)" << std::endl;
      return idx;
   }

   ss_res_t resolve_ss_res(const ss_index_t &idx, const std::string &asym,
                           const std::string &seq, const std::string &comp) {

      const label_map_t &first  = idx.prefer_auth ? idx.by_auth  : idx.by_label;
      const label_map_t &second = idx.prefer_auth ? idx.by_label : idx.by_auth;

      std::pair<std::string, std::string> key(asym, seq);
      const ss_res_t *hit = nullptr;
      auto it = first.find(key);
      if (it != first.end()) hit = &it->second;
      else {
         auto it2 = second.find(key);
         if (it2 != second.end()) hit = &it2->second;
      }

      ss_res_t r;
      if (hit) r = *hit;
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
      ss_index_t idx = build_ss_index(block);
      if (idx.empty()) return helices;
      size_t n_unmappable = 0;
      for (size_t i = 0; i < ids.size(); i++) {
         helix_rec_t rec;
         rec.start = resolve_ss_res(idx, label_rows_asym1[i], label_rows_seq1[i],
                                    label_rows_comp1[i]);
         rec.end   = resolve_ss_res(idx, label_rows_asym2[i], label_rows_seq2[i],
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
      ss_index_t idx = build_ss_index(block);
      if (idx.empty()) return strands;

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
         rec.start = resolve_ss_res(idx, row.str(2), row.str(3),
                                    row.has(4) ? row.str(4) : std::string());
         rec.end   = resolve_ss_res(idx, row.str(5), row.str(6),
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
         // RIGHT-justified in 12-14, as wwPDB writes it. Same bug as the sheet
         // id, found the same way (--header-check against pdb1aon.ent).
         l.put_right(12, 14, id.substr(0, 3));
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
         // RIGHT-justified in 12-14, as wwPDB writes it ("  B"), not left.
         // Found 2026-08-20 by --header-check comparing against pdb1aon.ent.
         l.put_right(12, 14, s.sheet_id.substr(0, 3));
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

   // In PDB record order. mmdb files each record into its own container and
   // its writer emits them in canonical order regardless, so this ordering is
   // for reading the list itself -- with ONE exception that is load-bearing and
   // lives at the call site: REMARK 2 must reach mmdb before REMARK 3, because
   // GetResolution() stops scanning at the first remark above 2.
   const std::vector<entity_t> entities = collect_entities(st, block);

   add_header(out, st);
   emit_wrapped(out, "TITLE",  st.get_info("_struct.title"));
   add_compound(out, entities);
   add_source(out, block, entities);
   emit_wrapped(out, "KEYWDS", st.get_info("_struct_keywords.text"));
   emit_wrapped(out, "EXPDTA", st.get_info("_exptl.method"));
   add_authors(out, block);
   add_revdat(out, st, block);
   add_journal(out, block);
   add_refinement_remarks(out, block);
   add_dbref(out, st, block);
   add_het_compounds(out, st, block, entities);
   add_helices(out, st, block);
   add_sheets(out, st, block);

   return out;
}


// ======================================================================
// THE OTHER DIRECTION: mmdb's PDB header -> mmCIF
//
// See gemmi-header.hh. copy_from_mmdb() is as thin as copy_to_mmdb() was, so
// PDB -> mmCIF lost the whole header until this existed.
// ======================================================================

namespace {

   // mmdb::Root keeps its Title protected, and Title keeps `author`, `title`
   // and `classification` protected with no getters -- so the same static_cast
   // that src/c-interface-widgets.cc has always used for the Remarks Browser is
   // needed here. Kept local rather than including coords/mmdb.h, which would
   // point this portable file at the GUI-side tree.
   class access_manager : public mmdb::Manager {
   public:
      const mmdb::Title *title_of() const { return &title; }
   };
   class access_title : public mmdb::Title {
   public:
      mmdb::TitleContainer *authors()        { return &author; }
      mmdb::TitleContainer *title_lines()    { return &title; }
      const char *classification_of() const  { return classification; }
   };

   access_title *title_of(mmdb::Manager *mol) {
      access_manager *am = static_cast<access_manager *>(mol);
      return static_cast<access_title *>(const_cast<mmdb::Title *>(am->title_of()));
   }

   // Container lines are stored as everything from column 11 on, so they arrive
   // padded to 80 and have to be trimmed before they become CIF values.
   std::vector<std::string> container_lines(mmdb::TitleContainer *c) {
      std::vector<std::string> v;
      if (! c) return v;
      for (int i = 0; i < c->Length(); i++) {
         mmdb::ContString *s =
            static_cast<mmdb::ContString *>(c->GetContainerClass(i));
         if (s && s->Line) {
            std::string line = trimmed(s->Line);
            if (! line.empty()) v.push_back(line);
         }
      }
      return v;
   }

   // PDB continuation lines are a single value split across records, so they
   // join with a space -- except where the previous line ended mid-hyphenation
   // or the next begins with punctuation.
   std::string join_continued(const std::vector<std::string> &lines) {
      std::string s;
      for (const std::string &l : lines) {
         if (! s.empty() && s.back() != '-') s += " ";
         s += l;
      }
      return s;
   }

   // "29-SEP-15" -> "2015-09-29". The century is the PDB's own problem and it
   // is solved the way every reader solves it: 70 and above is 19xx.
   std::string iso_date(const std::string &pdb_date) {
      static const char *months[12] = { "JAN","FEB","MAR","APR","MAY","JUN",
                                        "JUL","AUG","SEP","OCT","NOV","DEC" };
      if (pdb_date.size() < 9) return std::string();
      std::string dd = pdb_date.substr(0, 2);
      std::string mon = pdb_date.substr(3, 3);
      std::string yy = pdb_date.substr(7, 2);
      int mm = 0;
      for (int i = 0; i < 12; i++)
         if (mon == months[i]) { mm = i + 1; break; }
      if (mm == 0) return std::string();
      int y = std::atoi(yy.c_str());
      std::string yyyy = std::to_string(y >= 70 ? 1900 + y : 2000 + y);
      char buf[16];
      snprintf(buf, sizeof(buf), "%s-%02d-%s", yyyy.c_str(), mm, dd.c_str());
      return std::string(buf);
   }

   gemmi::AtomAddress ss_address(const char *chain, int seqnum, const char *icode,
                                 const char *resname) {
      gemmi::SeqId seqid;
      seqid.num = seqnum;
      seqid.icode = (icode && icode[0]) ? icode[0] : ' ';
      gemmi::ResidueId rid;
      rid.seqid = seqid;
      rid.name = resname ? resname : "";
      return gemmi::AtomAddress(chain ? chain : "", rid, "");
   }

   // A CIF value that is empty must be written as a null, never as nothing:
   // an empty token would shift every value after it on the row. Same lesson
   // as the extension-column bug in gemmi-coords.cc.
   std::string cif_value(const std::string &s) {
      if (s.empty()) return ".";
      return gemmi::cif::quote(s);
   }

   void add_loop(gemmi::cif::Block &block, const std::string &category,
                 const std::vector<std::string> &tags,
                 const std::vector<std::vector<std::string> > &rows) {
      if (rows.empty()) return;
      // The trailing dot is part of the PREFIX gemmi expects, not decoration:
      // init_loop("_audit_author", {"name"}) produces the tag
      // "_audit_authorname", which is a legal CIF token, parses without
      // complaint, and belongs to no category at all. Caught by reading the
      // output rather than by the build.
      gemmi::cif::Loop &loop = block.init_loop(category + ".", tags);
      for (const std::vector<std::string> &row : rows) {
         std::vector<std::string> values;
         values.reserve(row.size());
         for (const std::string &v : row) values.push_back(cif_value(v));
         loop.add_row(values);
      }
   }
}


void coot::transfer_pdb_header_to_gemmi(mmdb::Manager *mol, gemmi::Structure &st) {

   if (! mol) return;
   access_title *t = title_of(mol);
   if (! t) return;

   // --- the keys gemmi's writer turns into categories by itself ---

   std::string id = t->GetIDCode() ? trimmed(t->GetIDCode()) : std::string();
   if (! id.empty()) st.info["_entry.id"] = id;

   std::string title = join_continued(container_lines(t->title_lines()));
   if (! title.empty()) st.info["_struct.title"] = title;

   std::string classification = t->classification_of()
      ? trimmed(t->classification_of()) : std::string();
   if (! classification.empty())
      st.info["_struct_keywords.pdbx_keywords"] = classification;

   if (mmdb::KeyWords *kw = t->GetKeyWords()) {
      std::string words;
      for (int i = 0; i < kw->nKeyWords; i++) {
         if (! kw->KeyWord[i]) continue;
         std::string w = trimmed(kw->KeyWord[i]);
         if (w.empty()) continue;
         if (! words.empty()) words += ", ";
         words += w;
      }
      if (! words.empty()) st.info["_struct_keywords.text"] = words;
   }

   std::string method = join_continued(container_lines(t->GetExpData()));
   if (! method.empty()) st.info["_exptl.method"] = method;

   // HEADER's deposition date. mmdb stores it as DD-MMM-YYYY internally but
   // hands back the PDB spelling, so convert to the mmCIF one rather than
   // writing a date in a format no mmCIF reader expects.
   {
      char buf[128];
      mmdb::pstr s = nullptr;
      t->MakePDBHeaderString(buf);
      std::string header(buf);
      if (header.size() >= 59) {
         std::string d = iso_date(trimmed(header.substr(50, 9)));
         if (! d.empty())
            st.info["_pdbx_database_status.recvd_initial_deposition_date"] = d;
      }
      (void) s;
   }

   // --- secondary structure, which gemmi models properly ---
   //
   // Straight into st.helices / st.sheets so gemmi writes _struct_conf and
   // _struct_sheet* itself, with its own idea of the right tag set. Writing
   // those categories by hand would mean inventing label_* ids that a PDB file
   // does not have.
   mmdb::Model *model = mol->GetModel(1);
   if (! model) return;

   for (int i = 1; i <= model->GetNumberOfHelices(); i++) {
      mmdb::Helix *h = model->GetHelix(i);
      if (! h) continue;
      gemmi::Helix helix;
      helix.start = ss_address(h->initChainID, h->initSeqNum, h->initICode,
                               h->initResName);
      helix.end   = ss_address(h->endChainID, h->endSeqNum, h->endICode,
                               h->endResName);
      helix.set_helix_class_as_int(h->helixClass);
      if (h->length > 0) helix.length = h->length;
      st.helices.push_back(helix);
   }

   for (int is = 1; is <= model->GetNumberOfSheets(); is++) {
      mmdb::Sheet *sheet = model->GetSheet(is);
      if (! sheet) continue;
      gemmi::Sheet gs(sheet->sheetID ? sheet->sheetID : "");
      for (int istr = 0; istr < sheet->nStrands; istr++) {
         mmdb::Strand *strand = sheet->strand[istr];
         if (! strand) continue;
         gemmi::Sheet::Strand s;
         s.start = ss_address(strand->initChainID, strand->initSeqNum,
                              strand->initICode, strand->initResName);
         s.end   = ss_address(strand->endChainID, strand->endSeqNum,
                              strand->endICode, strand->endResName);
         s.sense = strand->sense;
         s.name  = std::to_string(strand->strandNo);
         gs.strands.push_back(s);
      }
      if (! gs.strands.empty()) st.sheets.push_back(gs);
   }
}


// The resolution, from wherever the PDB header actually states it.
//
// mmdb answers GetResolution() from REMARK 2 -- but REMARK 2 is a wwPDB
// DEPOSITION record, and a refinement program's output does not have to carry
// one. phenix.refine's does not: SC1_2_refine_031.pdb has no REMARK 2 at all
// and states the resolution only inside its REMARK 3 refinement account,
// "RESOLUTION RANGE HIGH (ANGSTROMS) : 2.80" -- the same wwPDB template refmac
// uses, and the same line we synthesize in the other direction.
//
// So the u80 fix covered wwPDB files and missed every phenix one: converting a
// phenix PDB to mmCIF still produced a file with no resolution anywhere. Found
// 2026-08-20 by Art, in the Header Browser, on the converted file -- "you can
// see resolution bins in refinement information" is that REMARK 3, preserved as
// text in _pdbx_database_remark while the typed value was missing.
//
// The BIN table's "BIN  RESOLUTION RANGE  COMPL. ..." header does not match:
// the search is for RANGE HIGH specifically.
double coot::pdb_header_resolution(mmdb::Manager *mol) {

   if (! mol) return -1.0;
   double r = mol->GetResolution();
   if (r > 0.0) return r;

   mmdb::TitleContainer *rc = mol->GetRemarks();
   if (! rc) return -1.0;
   for (int i = 0; i < rc->Length(); i++) {
      mmdb::Remark *rem = static_cast<mmdb::Remark *>(rc->GetContainerClass(i));
      if (! rem || rem->remarkNum != 3 || ! rem->remark) continue;
      std::string line(rem->remark);
      size_t p = line.find("RESOLUTION RANGE HIGH");
      if (p == std::string::npos) continue;
      size_t colon = line.find(':', p);
      if (colon == std::string::npos) continue;
      double v = std::atof(trimmed(line.substr(colon + 1)).c_str());
      if (v > 0.0) return v;
   }
   return -1.0;
}

void coot::add_pdb_header_categories(mmdb::Manager *mol, gemmi::cif::Block &block) {

   if (! mol) return;
   access_title *t = title_of(mol);
   if (! t) return;

   // --- _audit_author, from AUTHOR ---
   //
   // PDB writes one comma-separated list; mmCIF wants one row per author. The
   // spelling is left exactly as the file had it ("J.S.FRASER"): rewriting it
   // into "Fraser, J.S." means guessing where the surname starts, which is a
   // guess about a real person's name.
   {
      std::vector<std::vector<std::string> > rows;
      std::string all = join_continued(container_lines(t->authors()));
      std::string name;
      int ordinal = 0;
      for (size_t i = 0; i <= all.size(); i++) {
         if (i == all.size() || all[i] == ',') {
            std::string n = trimmed(name);
            if (! n.empty())
               rows.push_back({ n, std::to_string(++ordinal) });
            name.clear();
         } else {
            name += all[i];
         }
      }
      add_loop(block, "_audit_author", { "name", "pdbx_ordinal" }, rows);
   }

   // --- _citation and _citation_author, from JRNL ---
   //
   // The JRNL sub-records are already structured -- AUTH / TITL / REF / REFN /
   // PMID / DOI -- so this is a parse rather than a reconstruction. Lines
   // arrive as stored: two leading spaces, the sub-record name in columns 3-6,
   // its own continuation number, then the text.
   {
      // Parse by TOKEN, not by column. The container has already stripped the
      // record's leading columns and trimmed the line, so the sub-record name
      // no longer sits where the PDB spec says it does -- a fixed offset here
      // ate the first character of every field ("IDDEN ALTERNATIVE
      // STRUCTURES...", "ATURE", PubMed "9956261"). Found by reading the
      // output; it compiled and looked structurally right.
      std::map<std::string, std::vector<std::string> > sub;
      for (const std::string &line : container_lines(t->GetJournal())) {
         size_t p = 0;
         while (p < line.size() && ! std::isspace((unsigned char) line[p])) p++;
         std::string key = line.substr(0, p);
         if (key.empty()) continue;
         while (p < line.size() && std::isspace((unsigned char) line[p])) p++;
         // An optional continuation number follows the sub-record name on
         // every line after the first. It is a number followed by a SPACE --
         // which is what distinguishes it from a value that happens to start
         // with digits, like PMID or a DOI.
         size_t q = p;
         while (q < line.size() && std::isdigit((unsigned char) line[q])) q++;
         if (q > p && q < line.size() && std::isspace((unsigned char) line[q])) {
            p = q;
            while (p < line.size() && std::isspace((unsigned char) line[p])) p++;
         }
         sub[key].push_back(trimmed(line.substr(p)));
      }

      if (! sub.empty()) {
         // Values stay in the PDB's upper case. Converting "HIDDEN ALTERNATIVE
         // STRUCTURES..." to title case would be inventing capitalisation the
         // input never had, and getting it wrong on every proper noun and
         // chemical name in the corpus.
         std::string title = join_continued(sub["TITL"]);
         std::string ref   = join_continued(sub["REF"]);
         std::string refn  = join_continued(sub["REFN"]);
         std::string pmid  = join_continued(sub["PMID"]);
         std::string doi   = join_continued(sub["DOI"]);

         // REF is column-structured in the record we were given, but the
         // container has already stripped the leading columns, so parse by
         // shape instead: "<journal> V. <volume> <page> <year>".
         std::string journal, volume, page, year;
         {
            size_t v = ref.find("V.");
            if (v != std::string::npos) {
               journal = trimmed(ref.substr(0, v));
               std::istringstream iss(ref.substr(v + 2));
               iss >> volume >> page >> year;
            } else {
               journal = ref;
            }
         }
         std::string issn;
         {
            size_t i = refn.find("ISSN");
            if (i != std::string::npos) issn = trimmed(refn.substr(i + 4));
         }

         std::vector<std::string> tags = { "id", "title", "journal_abbrev",
                                           "journal_volume", "page_first", "year",
                                           "journal_id_ISSN",
                                           "pdbx_database_id_PubMed",
                                           "pdbx_database_id_DOI" };
         std::vector<std::vector<std::string> > rows;
         rows.push_back({ "primary", title, journal, volume, page, year,
                          issn, pmid, doi });
         add_loop(block, "_citation", tags, rows);

         std::vector<std::vector<std::string> > authors;
         std::string all = join_continued(sub["AUTH"]);
         std::string name;
         int ordinal = 0;
         for (size_t i = 0; i <= all.size(); i++) {
            if (i == all.size() || all[i] == ',') {
               std::string n = trimmed(name);
               if (! n.empty())
                  authors.push_back({ "primary", n, std::to_string(++ordinal) });
               name.clear();
            } else {
               name += all[i];
            }
         }
         add_loop(block, "_citation_author",
                  { "citation_id", "name", "ordinal" }, authors);
      }
   }

   // --- _pdbx_database_remark, from the REMARK cards ---
   //
   // wwPDB's own holding category for PDB remarks that have no typed mmCIF
   // home. Without it the whole REMARK 3 refinement account, REMARK 200 data
   // collection and the rest simply vanish on conversion -- which is most of
   // the bytes of a PDB header. Text is joined per remark number, so the
   // browser and any reader get it back the way it was written.
   {
      std::map<int, std::vector<std::string> > remarks;
      if (mmdb::TitleContainer *rc = mol->GetRemarks()) {
         for (int i = 0; i < rc->Length(); i++) {
            mmdb::Remark *r = static_cast<mmdb::Remark *>(rc->GetContainerClass(i));
            if (! r) continue;
            remarks[r->remarkNum].push_back(r->remark ? r->remark : "");
         }
      }
      std::vector<std::vector<std::string> > rows;
      for (const auto &kv : remarks) {
         std::string text;
         for (const std::string &line : kv.second) {
            if (! text.empty()) text += "\n";
            text += line;
         }
         // REMARK 2 is the resolution, which _refine.ls_d_res_high already
         // carries in any file that has one; keeping it here too is harmless
         // and keeps the account complete.
         rows.push_back({ std::to_string(kv.first), text });
      }
      add_loop(block, "_pdbx_database_remark", { "id", "text" }, rows);
   }

   // --- _refine.ls_d_res_high, from REMARK 2 ---
   //
   // mmdb parses REMARK 2 into a resolution, and the read path takes the
   // resolution back out of _refine.ls_d_res_high (adjustment (6); gemmi
   // populates st.resolution from it -- trap D5). Without this the number
   // survives PDB -> mmdb and then dies at mmdb -> mmCIF, so converting a PDB
   // to mmCIF loses the resolution silently. Found 2026-08-20 by --round-trip
   // chain A once it learned to compare the resolution: ten of the fourteen
   // PDB inputs reported "A=1 (1.390) B=0 (-2.000)".
   //
   // Guarded on the category being absent: this is the synthesis branch, so
   // there is normally no _refine at all, but a block that already has one is
   // better left alone than given a second, conflicting item.
   if (! block.has_mmcif_category("_refine")) {
      double reso = coot::pdb_header_resolution(mol);
      if (reso > 0.0) {
         char buf[32];
         snprintf(buf, sizeof(buf), "%.2f", reso);
         // pdbx_refine_id is dictionary-mandatory, and its value is the
         // experimental method -- which we know only if EXPDTA said so. An
         // absent tag is better than an invented "X-RAY DIFFRACTION" on a file
         // that was refined against something else.
         std::string method = join_continued(container_lines(t->GetExpData()));
         if (! method.empty())
            block.set_pair("_refine.pdbx_refine_id", cif_value(method));
         block.set_pair("_refine.ls_d_res_high", buf);
      }
   }

   // --- _entity.pdbx_description, from COMPND ---
   //
   // gemmi writes _entity with ids and types but no description, because
   // copy_from_mmdb gave it none. COMPND has them, keyed by MOL_ID, and the
   // CHAIN: token maps a MOL_ID to author chains -- which is how an entity row
   // is matched back, since a synthesized entity's subchains are its chains.
   {
      gemmi::cif::Table entities = block.find_mmcif_category("_entity");
      if (! entities.ok() || ! entities.loop_item) return;

      std::map<std::string, std::string> chain_description;   // chain -> molecule
      std::string molecule;
      std::vector<std::string> chains;
      auto flush = [&]() {
         for (const std::string &c : chains)
            if (! molecule.empty()) chain_description[c] = molecule;
         molecule.clear();
         chains.clear();
      };
      for (const std::string &line : container_lines(t->GetCompound())) {
         std::string spec = line;
         if (! spec.empty() && spec.back() == ';') spec.pop_back();
         size_t colon = spec.find(':');
         if (colon == std::string::npos) continue;
         std::string key = trimmed(spec.substr(0, colon));
         std::string value = trimmed(spec.substr(colon + 1));
         if (key == "MOL_ID") flush();
         else if (key == "MOLECULE") molecule = value;
         else if (key == "CHAIN") {
            std::string c;
            for (size_t i = 0; i <= value.size(); i++) {
               if (i == value.size() || value[i] == ',') {
                  std::string cc = trimmed(c);
                  if (! cc.empty()) chains.push_back(cc);
                  c.clear();
               } else c += value[i];
            }
         }
      }
      flush();
      if (chain_description.empty()) return;

      gemmi::cif::Loop &loop = entities.loop_item->loop;
      int id_col = loop.find_tag("_entity.id");
      if (id_col < 0) return;
      int desc_col = loop.find_tag("_entity.pdbx_description");
      if (desc_col < 0) {
         // add the column, defaulting to a null rather than to nothing
         std::vector<std::string> values;
         size_t ncol = loop.tags.size();
         for (size_t r = 0; r < loop.length(); r++) {
            for (size_t c = 0; c < ncol; c++) values.push_back(loop.val(r, c));
            values.push_back(".");
         }
         loop.tags.push_back("_entity.pdbx_description");
         loop.values = values;
         desc_col = static_cast<int>(loop.tags.size()) - 1;
      }

      // A synthesized entity id is its chain name for a PDB-derived structure,
      // so the lookup is direct; anything that does not match keeps its null.
      for (size_t r = 0; r < loop.length(); r++) {
         std::string id = gemmi::cif::as_string(loop.val(r, (size_t) id_col));
         std::map<std::string, std::string>::const_iterator it =
            chain_description.find(id);
         if (it != chain_description.end())
            loop.values[r * loop.tags.size() + desc_col] =
               gemmi::cif::quote(it->second);
      }
   }
}
