/* src/c-interface-build.cc
 * 
 * Copyright 2002, 2003, 2004, 2005, 2006, 2007 The University of York
 * Author: Paul Emsley
 * Copyright 2007 by Paul Emsley
 * Copyright 2007 by the University of Oxford
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */


#ifdef USE_PYTHON
#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include "compat/coot-sysdep.h"

#include <string>
#include <vector>

#include <gtk/gtk.h>
#include "graphics-info.h"
// Including python needs to come after graphics-info.h, because
// something in Python.h (2.4 - chihiro) is redefining FF1 (in
// ssm_superpose.h) to be 0x00004000 (Grrr).
// BL says:: and (2.3 - dewinter), i.e. is a Mac - Python issue
// since the follwing two include python graphics-info.h is moved up
#include "c-interface.h"
#include "c-interface-gtk-widgets.h"

#include "generic-display-objects-c.h"

void do_rot_trans_adjustments(GtkWidget *dialog) { 
   graphics_info_t g;
   g.do_rot_trans_adjustments(dialog);
}

short int delete_item_widget_is_being_shown() {
   short int r = 0; 
   if (graphics_info_t::delete_item_widget != NULL) {
      r = 1;
   }
   return r;
}

short int delete_item_widget_keep_active_on() {
   short int r = 0;
   if (delete_item_widget_is_being_shown()) { 
      GtkWidget *checkbutton = lookup_widget(graphics_info_t::delete_item_widget,
					     "delete_item_keep_active_checkbutton");
      if (GTK_TOGGLE_BUTTON(checkbutton)->active) {
	 r = 1;
      }
   }
   return r;
}

void store_delete_item_widget_position() {

   gint upositionx, upositiony;
   gdk_window_get_root_origin (graphics_info_t::delete_item_widget->window,
			       &upositionx, &upositiony);
   graphics_info_t::delete_item_widget_x_position = upositionx;
   graphics_info_t::delete_item_widget_y_position = upositiony;
   gtk_widget_destroy(graphics_info_t::delete_item_widget);
   clear_delete_item_widget();
}

void clear_delete_item_widget() {

   graphics_info_t::delete_item_widget = NULL;
}


void store_delete_item_widget(GtkWidget *widget) {
   graphics_info_t::delete_item_widget = widget;
}



/*  find the molecule that the single map dialog applies to and set
    the contour level and redraw */
void single_map_properties_apply_contour_level_to_map(GtkWidget *w) {

   int imol = GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(w)));

   if (is_valid_map_molecule(imol)) { 
      GtkToggleButton *toggle_button =
	 GTK_TOGGLE_BUTTON(lookup_widget(w, "single_map_properties_sigma_radiobutton"));

      GtkWidget *entry = lookup_widget(w, "single_map_properties_contour_level_entry");
      const char *txt = gtk_entry_get_text(GTK_ENTRY(entry));
      float level = atof(txt);
      if (toggle_button->active) {
	 set_contour_level_in_sigma(imol, level);
      } else {
	 set_contour_level_absolute(imol, level);
      }
   }
}

#include "remarks-browser-gtk-widgets.hh"
#include "coot-utils/mmcif-header-view.hh"
#include "coords/mmdb.h"   // coot::get_title, for the window heading

// ---------------------------------------------------------------- mmCIF header browser
//
// The category-driven view. Everything about WHAT to show lives in
// coot-utils/mmcif-header-view.cc (portable, survives the toolkit rewrite);
// this is only layout.
//
// Laid out to match the PDB header browser above it -- a titled frame per
// category, all of them open, the window scrolling -- because to most users a
// structure is a structure and the format is an implementation detail (Art,
// 2026-08-17). What differs is deliberate: mmCIF values keep their own mixed
// case, which is easier to read than PDB's all-caps, while the title is
// upper-cased to match.

namespace {

   struct header_panel {
      const coot::header_category_t *cat;
      GtkWidget *frame;
      GtkWidget *text_view;
   };

   struct header_dialog_state {
      std::vector<coot::header_category_t> view;
      std::vector<header_panel> panels;
      GtkWidget *filter_entry;
      GtkWidget *show_all_button;
   };

   std::string pad_to(const std::string &s, size_t w) {
      std::string t = s;
      while (t.size() < w) t += " ";
      return t;
   }

   // Panel colours, per FAMILY rather than per category, so an unfamiliar
   // category still lands in a sensible group. Kept in the same register as the
   // PDB browser's REMARK panels -- pale washes, dark text -- because the point
   // is to let the eye group the window at a glance, not to decorate it.
   //
   // Heterogeneity is deliberately the one warm colour: it is the only family
   // here that Bandicoot itself is about.
   GdkColor header_family_colour(coot::header_family_t f) {
      GdkColor c;
      c.pixel = 65535;
      switch (f) {
      case coot::header_family_t::bibliography:                     // cream
         c.red = 65535; c.green = 63500; c.blue = 58000; break;
      case coot::header_family_t::entity:                           // pale pink
         c.red = 65535; c.green = 61000; c.blue = 62500; break;
      case coot::header_family_t::experiment:                       // pale green
         c.red = 58500; c.green = 65000; c.blue = 58500; break;
      case coot::header_family_t::refinement:                       // light cyan
         c.red = 57000; c.green = 64500; c.blue = 65535; break;
      case coot::header_family_t::symmetry:                         // pale lavender
         c.red = 61500; c.green = 60000; c.blue = 65535; break;
      case coot::header_family_t::annotation:                       // pale slate
         c.red = 60000; c.green = 63000; c.blue = 64500; break;
      case coot::header_family_t::heterogeneity:                    // warm apricot
         c.red = 65535; c.green = 60000; c.blue = 51000; break;
      case coot::header_family_t::other:
      default:                                                      // near-white
         c.red = 64500; c.green = 64500; c.blue = 64500; break;
      }
      return c;
   }

   std::string lowered(const std::string &s) {
      std::string t = s;
      for (char &c : t) c = std::tolower(static_cast<unsigned char>(c));
      return t;
   }

   bool contains(const std::string &hay_lower, const std::string &needle_lower) {
      return needle_lower.empty() ||
             hay_lower.find(needle_lower) != std::string::npos;
   }

   // A field with no value carries no information -- only the fact that the tag
   // was declared -- and a wwPDB entry declares a great many it does not fill
   // (_refine alone has 66 tags and fills about half). Showing them puts a
   // column of blanks in the way of the numbers someone opened the window for.
   bool has_value(const std::string &v) {
      if (v.empty()) return false;
      for (char c : v)
         if (! std::isspace(static_cast<unsigned char>(c))) return true;
      return false;
   }

   // Build the text for one category, keeping only what matches the filter.
   // Returns false when nothing matches, so the caller can hide the frame.
   //
   // The filter deliberately works AT FIELD LEVEL. Matching whole categories
   // was the first cut and it read as broken: typing "resolution" matched
   // _refine, which contains resolution items, and then showed all 66 of its
   // fields -- most of them about something else entirely.
   bool category_content(const coot::header_category_t &c,
                         const std::string &needle, std::string *out) {

      out->clear();
      const std::string cat_hay = lowered(c.name + " " + c.label);
      const bool whole_category_matches = ! needle.empty() && contains(cat_hay, needle);

      if (! c.is_loop) {

         // Which fields to show, and how wide the label column has to be. Two
         // passes so the alignment is computed from what is actually displayed
         // rather than from the whole category.
         std::vector<size_t> keep;
         for (size_t i = 0; i < c.column_tags.size(); i++) {
            const std::string &v = c.rows.empty() ? std::string() : c.rows[0][i];
            if (! has_value(v)) continue;
            if (! needle.empty() && ! whole_category_matches) {
               std::string hay = lowered(c.column_labels[i] + " " + c.column_tags[i] +
                                         " " + v);
               if (! contains(hay, needle)) continue;
            }
            keep.push_back(i);
         }
         if (keep.empty()) return false;

         size_t wl = 0;
         for (size_t i : keep) wl = std::max(wl, c.column_labels[i].size());
         for (size_t i : keep) {
            const std::string &v = c.rows[0][i];
            *out += " " + pad_to(c.column_labels[i], wl) + "   " + v + "\n";
         }
         return true;
      }

      // A loop is a table, and a table's unit is the row. If the category or a
      // column heading matches, the whole table is what was asked for;
      // otherwise keep the rows that match and drop the rest.
      bool column_matches = false;
      if (! needle.empty())
         for (size_t i = 0; i < c.column_labels.size() && ! column_matches; i++)
            column_matches = contains(lowered(c.column_labels[i] + " " +
                                              c.column_tags[i]), needle);

      std::vector<size_t> keep_rows;
      for (size_t r = 0; r < c.rows.size(); r++) {
         if (needle.empty() || whole_category_matches || column_matches) {
            keep_rows.push_back(r);
            continue;
         }
         bool row_matches = false;
         for (const std::string &cell : c.rows[r])
            if (contains(lowered(cell), needle)) { row_matches = true; break; }
         if (row_matches) keep_rows.push_back(r);
      }
      if (keep_rows.empty()) return false;

      // Drop columns that are empty in every row being shown: a wwPDB loop
      // routinely declares columns it never fills, and an empty column is a
      // stretch of whitespace between two things the reader wants to compare.
      size_t ncol = c.column_tags.size();
      std::vector<size_t> cols;
      for (size_t i = 0; i < ncol; i++)
         for (size_t r : keep_rows)
            if (i < c.rows[r].size() && has_value(c.rows[r][i])) { cols.push_back(i); break; }
      if (cols.empty()) return false;

      std::vector<size_t> w(ncol, 0);
      for (size_t i : cols) {
         w[i] = c.column_labels[i].size();
         for (size_t r : keep_rows)
            if (i < c.rows[r].size()) w[i] = std::max(w[i], c.rows[r][i].size());
         if (w[i] > 44) w[i] = 44;
      }
      for (size_t i : cols) *out += " " + pad_to(c.column_labels[i], w[i]);
      *out += "\n";
      for (size_t i : cols) *out += " " + std::string(w[i], '-');
      *out += "\n";
      for (size_t r : keep_rows) {
         for (size_t i : cols)
            *out += " " + pad_to(i < c.rows[r].size() ? c.rows[r][i] : std::string(), w[i]);
         *out += "\n";
      }
      return true;
   }

   void header_dialog_refresh(header_dialog_state *st) {

      const char *txt = gtk_entry_get_text(GTK_ENTRY(st->filter_entry));
      std::string needle = lowered(txt ? txt : "");
      bool show_all =
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(st->show_all_button));

      for (header_panel &p : st->panels) {
         std::string body;
         bool any = category_content(*p.cat, needle, &body);
         bool wanted = any && (show_all || ! p.cat->body);
         if (! wanted) {
            gtk_widget_hide(p.frame);
            continue;
         }
         GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(p.text_view));
         gtk_text_buffer_set_text(b, body.c_str(), -1);
         gtk_widget_show(p.frame);
      }
   }

   void on_header_filter_changed(GtkEditable *, gpointer data) {
      header_dialog_refresh(static_cast<header_dialog_state *>(data));
   }

   void on_header_show_all_toggled(GtkToggleButton *, gpointer data) {
      header_dialog_refresh(static_cast<header_dialog_state *>(data));
   }

   void free_header_dialog_state(gpointer p) {
      delete static_cast<header_dialog_state *>(p);
   }
}

void mmcif_header_dialog(int imol) {

   if (! graphics_info_t::use_graphics_interface_flag) return;
   if (! is_valid_model_molecule(imol)) return;

   const coot::mmcif_document_t *doc = graphics_info_t::molecules[imol].get_mmcif_document();

   header_dialog_state *st = new header_dialog_state;
   st->view = coot::mmcif_header_view(doc);
   if (st->view.empty()) {
      delete st;
      info_dialog("WARNING:: No header information");
      return;
   }

   GtkWidget *d = gtk_dialog_new();
   gtk_window_set_title(GTK_WINDOW(d), "Coot Header Browser");
   gtk_object_set_data(GTK_OBJECT(d), "remarks_dialog", d);
   g_object_set_data_full(G_OBJECT(d), "header-state", st, free_header_dialog_state);

   GtkWidget *vbox = GTK_DIALOG(d)->vbox;

   // Title upper-cased, to sit where the PDB browser's HEADER-derived title
   // sits and read the same way. The VALUES below keep the file's own case.
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   std::string title = mol ? coot::get_title(mol) : std::string();
   if (! title.empty() && title != "Not available") {
      for (char &c : title) c = std::toupper(static_cast<unsigned char>(c));
      std::string markup = "<b>" + title + "</b>";
      GtkWidget *label = gtk_label_new(markup.c_str());
      gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
      gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
      gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
      gtk_widget_show(label);
   }

   GtkWidget *controls = gtk_hbox_new(FALSE, 4);
   GtkWidget *filter_label = gtk_label_new("Filter:");
   st->filter_entry = gtk_entry_new();
   // Off by default: the per-atom and per-residue tables are the bulk of an
   // mmCIF and are not header information. Reachable, never silently absent.
   st->show_all_button = gtk_check_button_new_with_label("Show all categories");
   gtk_box_pack_start(GTK_BOX(controls), filter_label, FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(controls), st->filter_entry, TRUE, TRUE, 4);
   gtk_box_pack_start(GTK_BOX(controls), st->show_all_button, FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(vbox), controls, FALSE, FALSE, 2);
   gtk_widget_show_all(controls);

   GtkWidget *vbox_inner = gtk_vbox_new(FALSE, 2);
   GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),
					 GTK_WIDGET(vbox_inner));
   gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_window), TRUE, TRUE, 2);
   gtk_widget_show(scrolled_window);
   gtk_widget_show(vbox_inner);

   st->panels.reserve(st->view.size());
   for (const coot::header_category_t &c : st->view) {

      // The frame title is the human wording; the category name is the fallback
      // when we have no better word for it, which is also how a reader learns
      // the name of something new.
      std::string frame_title = c.label.empty() ? c.name : c.label;

      GtkWidget *frame = gtk_frame_new(frame_title.c_str());
      gtk_box_pack_start(GTK_BOX(vbox_inner), frame, FALSE, FALSE, 1);

      GtkWidget *text_view = gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
      gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
      // Fixed width, because a loop only reads as a table when its columns line
      // up, and the pair view aligns its values the same way.
      PangoFontDescription *fd = pango_font_description_from_string("monospace 10");
      gtk_widget_modify_font(text_view, fd);
      pango_font_description_free(fd);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_NONE);
      GdkColor colour = header_family_colour(c.family);
      gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);
      gtk_container_add(GTK_CONTAINER(frame), text_view);
      gtk_widget_show(text_view);

      header_panel p;
      p.cat = &c;
      p.frame = frame;
      p.text_view = text_view;
      st->panels.push_back(p);
   }

   g_signal_connect(G_OBJECT(st->filter_entry), "changed",
		    G_CALLBACK(on_header_filter_changed), st);
   g_signal_connect(G_OBJECT(st->show_all_button), "toggled",
		    G_CALLBACK(on_header_show_all_toggled), st);

   header_dialog_refresh(st);

   GtkWidget *close_button = gtk_button_new_with_label("  Close   ");
   GtkWidget *aa = GTK_DIALOG(d)->action_area;
   gtk_box_pack_start(GTK_BOX(aa), close_button, FALSE, FALSE, 2);
   gtk_signal_connect(GTK_OBJECT(close_button), "clicked",
		      GTK_SIGNAL_FUNC(on_remarks_dialog_close_button_clicked), NULL);
   gtk_widget_show(close_button);
   gtk_widget_set_usize(d, 640, 560);
   gtk_widget_show(d);
}
/*! \brief a gui dialog showing remarks header info (for a model molecule). */
void remarks_dialog(int imol) {

   if (graphics_info_t::use_graphics_interface_flag) {
      if (is_valid_model_molecule(imol)) {
	 mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
	 if (mol) {

	    // An mmCIF molecule is shown as the CATEGORIES IT ACTUALLY HAS,
	    // not as a fixed set of panels. See mmcif-header-view.hh for why:
	    // a hand-built panel per field cannot show a category nobody
	    // anticipated, and accommodating new categories is the whole point
	    // of the format. A PDB molecule has no document and keeps the
	    // REMARK view below, which is the right thing for a file that
	    // genuinely has REMARK cards and no categories.
	    if (graphics_info_t::molecules[imol].get_mmcif_document()) {
	       mmcif_header_dialog(imol);
	       return;
	    }

	    GtkWidget *d = gtk_dialog_new();
	    gtk_window_set_title(GTK_WINDOW(d), "Coot Header Browser");
	    gtk_object_set_data(GTK_OBJECT(d), "remarks_dialog", d);
	    GtkWidget *vbox = GTK_DIALOG(d)->vbox;
	    GtkWidget *vbox_inner = gtk_vbox_new(FALSE, 2);
	    GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),
						  GTK_WIDGET(vbox_inner));
	    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_window), TRUE, TRUE, 2);
	    gtk_widget_show(scrolled_window);
	    gtk_widget_show(vbox_inner);

	    // Whether anything at all went into the window. Before v0.2 the
	    // dialog was thrown away whenever the molecule had no REMARK cards,
	    // taking the Compound/Author/Journal/Links panels with it -- which
	    // was invisible while mmCIF filled none of them, and wrong now that
	    // it fills all four. An NMR entry (2RSF) is the plain case: full
	    // bibliographic header, no resolution, no REMARKs.
	    bool have_content = false;

	    if (remarks_browser_fill_compound_info(mol, vbox_inner)) have_content = true;

	    if (remarks_browser_fill_author_info(mol, vbox_inner))   have_content = true;

	    if (remarks_browser_fill_journal_info(mol, vbox_inner))  have_content = true;

	    if (remarks_browser_fill_link_info(mol, vbox_inner))     have_content = true;

	    mmdb::TitleContainer *tc_p = mol->GetRemarks();
	    int l = tc_p->Length();
	    std::map<int, std::vector<std::string> > remarks;
	    for (int i=0; i<l; i++) { 
	       mmdb::Remark *cr = static_cast<mmdb::Remark *> (tc_p->GetContainerClass(i));
	       int rn = cr->remarkNum;
	       std::string s = cr->remark;
	       remarks[rn].push_back(s);
	    }
	    if (remarks.empty() && ! have_content) {
	       info_dialog("WARNING:: No header information");
	       gtk_widget_destroy(d);   // it was never shown; do not leak it
	    } else {

	       std::map<int, std::vector<std::string> >::const_iterator it;
	       for (it=remarks.begin(); it != remarks.end(); it++) {
		  std::string remark_name = "REMARK ";
		  remark_name += coot::util::int_to_string(it->first);
		  GtkWidget *frame = gtk_frame_new(remark_name.c_str());
		  gtk_box_pack_start(GTK_BOX(vbox_inner), frame, FALSE, FALSE, 1);
		  gtk_widget_show(frame);
		  // std::cout << "REMARK number " << it->first << std::endl;
		  GtkTextBuffer *text_buffer = gtk_text_buffer_new(NULL);
		  GtkWidget *text_view = gtk_text_view_new();
		  gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(text_view),
						       GTK_TEXT_WINDOW_RIGHT, 10);
		  gtk_widget_set_usize(GTK_WIDGET(text_view), 400, -1);
		  gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(text_view));
		  gtk_widget_show(GTK_WIDGET(text_view));
		  gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), text_buffer);
		  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

		  GdkColor colour = remark_number_to_colour(it->first); 
		  gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);

		  GtkTextIter end_iter;
		  for (unsigned int itext=0; itext<it->second.size(); itext++) { 
		     gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
		     std::string s = it->second[itext];
		     s += "\n";
		     gtk_text_buffer_insert(text_buffer, &end_iter, s.c_str(), -1);
		  }
	       }


	       GtkWidget *close_button = gtk_button_new_with_label("  Close   ");
	       GtkWidget *aa = GTK_DIALOG(d)->action_area;
	       gtk_box_pack_start(GTK_BOX(aa), close_button, FALSE, FALSE, 2);
	       
	       gtk_signal_connect(GTK_OBJECT(close_button), "clicked",
				  GTK_SIGNAL_FUNC(on_remarks_dialog_close_button_clicked), NULL);
	       gtk_widget_show(close_button);
	       gtk_widget_set_usize(d, 500, 400);
	       gtk_widget_show(d);
	    }
	 } 
      }
   }
}

bool remarks_browser_fill_compound_info(mmdb::Manager *mol, GtkWidget *vbox) {

   std::string title = coot::get_title(mol);
   std::vector<std::string> compound_lines = coot::get_compound_lines(mol);

   if (!title.empty()) {
      title = std::string("<b>") + title;
      title += "</b>";
      GtkWidget *label = gtk_label_new(title.c_str());
      gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
      gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
      gtk_widget_show(label);
   }

   if (compound_lines.size() > 0) {
      std::string compound_label = "Compound";
      GtkWidget *frame = gtk_frame_new(compound_label.c_str());
      gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 1);
      gtk_widget_show(frame);
      std::string s;
      for (std::size_t i=0; i<compound_lines.size(); i++) {
	 s += compound_lines[i];
	 s += "\n"; // needed?
      }
      GtkTextBuffer *text_buffer = gtk_text_buffer_new(NULL);
      GtkWidget *text_view = gtk_text_view_new();
      gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(text_view),
					   GTK_TEXT_WINDOW_RIGHT, 10);
      gtk_widget_set_usize(GTK_WIDGET(text_view), 400, -1);
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(text_view));
      gtk_widget_show(GTK_WIDGET(text_view));
      gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), text_buffer);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

      GdkColor colour;
      colour.red   = 65535;
      colour.green = 63535;
      colour.blue  = 63535; 
      colour.pixel = 65535; 
      gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);

      GtkTextIter end_iter;
      for (unsigned int itext=0; itext<compound_lines.size(); itext++) { 
	 gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
	 std::string s = compound_lines[itext];
	 s += "\n";
	 gtk_text_buffer_insert(text_buffer, &end_iter, s.c_str(), -1);
      }
   }
   return (! title.empty()) || (! compound_lines.empty());
}

bool remarks_browser_fill_author_info(mmdb::Manager *mol, GtkWidget *vbox) {

   std::vector<std::string> author_lines;

   access_mol *am = static_cast<access_mol *>(mol); // causes indent problem

   const mmdb::Title *tt = am->GetTitle();
   mmdb::Title *ttmp = const_cast<mmdb::Title *>(tt);
   access_title *at = static_cast<access_title *> (ttmp);
   mmdb::TitleContainer *author_container = at->GetAuthor();
   unsigned int al = author_container->Length();
   for (unsigned int i=0; i<al; i++) {
      mmdb::Author *a_line = mmdb::PAuthor(author_container->GetContainerClass(i));
      if (a_line) {
 	 std::string line(a_line->Line);
	 author_lines.push_back(line);
      }
   }
   // std::cout << "---------------- have " << author_lines.size() << " author lines" << std::endl;
   if (author_lines.size() > 0) {
      GtkWidget *frame = gtk_frame_new("Author");
      gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 1);
      gtk_widget_show(frame);

      GtkTextBuffer *text_buffer = gtk_text_buffer_new(NULL);
      GtkWidget *text_view = gtk_text_view_new();
      gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(text_view),
					   GTK_TEXT_WINDOW_RIGHT, 10);
      gtk_widget_set_usize(GTK_WIDGET(text_view), 400, -1);
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(text_view));
      gtk_widget_show(GTK_WIDGET(text_view));
      gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), text_buffer);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

      GdkColor colour;
      colour.red   = 63535;
      colour.green = 59535;
      colour.blue  = 53535;
      colour.pixel = 65535;
      gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);

      for (unsigned int ij=0; ij<author_lines.size(); ij++) {
	 GtkTextIter end_iter;
	 gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
	 std::string s = author_lines[ij];
	 s += "\n";
	 gtk_text_buffer_insert(text_buffer, &end_iter, s.c_str(), -1);
      }
   }
   return ! author_lines.empty();
}

bool remarks_browser_fill_journal_info(mmdb::Manager *mol, GtkWidget *vbox) {

   std::vector<std::string> journal_lines;

   access_mol *am = static_cast<access_mol *>(mol); // causes indent problem

   const mmdb::Title *tt = am->GetTitle();
   mmdb::Title *ttmp = const_cast<mmdb::Title *>(tt);
   access_title *at = static_cast<access_title *> (ttmp);
   mmdb::TitleContainer *journal_container = at->GetJournal();
   int jl = journal_container->Length();
   unsigned int al = journal_container->Length();
   for (unsigned int i=0; i<al; i++) {
      mmdb::Journal *j_line = mmdb::PJournal(journal_container->GetContainerClass(i));
      if (j_line) {
	 std::string line(j_line->Line);
	 journal_lines.push_back(line);
      }
   }
   // std::cout << "---------------- have " << journal_lines.size() << " journal_lines" << std::endl;
   if (journal_lines.size() > 0) {
      GtkWidget *frame = gtk_frame_new("Journal");
      gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 1);
      gtk_widget_show(frame);

      GtkTextBuffer *text_buffer = gtk_text_buffer_new(NULL);
      GtkWidget *text_view = gtk_text_view_new();
      gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(text_view),
					   GTK_TEXT_WINDOW_RIGHT, 10);
      gtk_widget_set_usize(GTK_WIDGET(text_view), 400, -1);
      gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(text_view));
      gtk_widget_show(GTK_WIDGET(text_view));
      gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), text_buffer);
      gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

      GdkColor colour;
      colour.red   = 45535;
      colour.green = 49535;
      colour.blue  = 53535;
      colour.pixel = 65535;
      gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);

      for (unsigned int ij=0; ij<journal_lines.size(); ij++) {
	 GtkTextIter end_iter;
	 gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
	 std::string s = journal_lines[ij];
	 s += "\n";
	 gtk_text_buffer_insert(text_buffer, &end_iter, s.c_str(), -1);
      }
   }
   return ! journal_lines.empty();
}

bool remarks_browser_fill_link_info(mmdb::Manager *mol, GtkWidget *vbox) {

   int imod = 1;
   mmdb::Model *model_p = mol->GetModel(imod);
   if (model_p) {
      int n_links = model_p->GetNumberOfLinks();
      mmdb::LinkContainer *links = model_p->GetLinks();
      std::cout << "   Model "  << imod << " had " << n_links << " links\n";

      float link_dist = -1;

      if (n_links > 0) {
	 GtkWidget *frame = gtk_frame_new("Links");
	 gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 1);
	 gtk_widget_show(frame);

	 GtkTextBuffer *text_buffer = gtk_text_buffer_new(NULL);
	 GtkWidget *text_view = gtk_text_view_new();
	 gtk_text_buffer_create_tag (text_buffer, "monospace",
				     "family", "monospace", NULL);

	 gtk_text_view_set_border_window_size(GTK_TEXT_VIEW(text_view),
					      GTK_TEXT_WINDOW_RIGHT, 10);
	 gtk_widget_set_usize(GTK_WIDGET(text_view), 400, -1);
	 gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(text_view));
	 gtk_widget_show(GTK_WIDGET(text_view));
	 gtk_text_view_set_buffer(GTK_TEXT_VIEW(text_view), text_buffer);
	 gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

	 GdkColor colour;
	 colour.red   = 45535;
	 colour.green = 53535;
	 colour.blue  = 63535;
	 colour.pixel = 65535;
	 gtk_widget_modify_base(GTK_WIDGET(text_view), GTK_STATE_NORMAL, &colour);

	 for (int ilink=0; ilink<n_links; ilink++) {
	    mmdb::Link *link_p = model_p->GetLink(ilink);
	    if (link_p) {
	       std::string s = "LINK ";

#ifdef MMDB_HAS_LINK_DISTANCE
               link_dist = link_p->dist;
#endif
	       std::string rn1 = link_p->resName1;
	       std::string rn2 = link_p->resName2;

	       // for alignment of monospaced text
	       if (rn1.length() == 1) rn1 += "  ";
	       if (rn1.length() == 2) rn1 += " ";
	       if (rn2.length() == 1) rn2 += "  ";
	       if (rn2.length() == 2) rn2 += " ";

	       s += link_p->atName1;
	       s += " ";
	       s += link_p->aloc1;
	       s += " ";
	       s += rn1;
	       s += " ";
	       s += coot::util::int_to_string(link_p->seqNum1);
	       s += " ";
	       s += link_p->insCode1;
	       s += " ";
	       s += link_p->atName2;
	       s += " ";
	       s += link_p->aloc2;
	       s += " ";
	       s += rn2;
	       s += " ";
	       s += coot::util::int_to_string(link_p->seqNum2);
	       s += " ";
	       s += link_p->insCode2;
	       // symm code
	       s += " ";
	       s += coot::util::float_to_string_using_dec_pl(link_dist, 3);
	       s += "\n";

	       GtkTextIter end_iter;
	       gtk_text_buffer_get_end_iter(text_buffer, &end_iter);
	       // gtk_text_buffer_insert(text_buffer, &end_iter, s.c_str(), -1);

	       gtk_text_buffer_insert_with_tags_by_name(text_buffer, &end_iter,
							s.c_str(), -1,
							"monospace", NULL);
	    }
	 }
	 return true;
      }
   }
   return false;
}


void
on_remarks_dialog_close_button_clicked     (GtkButton *button,
					    gpointer         user_data)
{
   GtkWidget *window = lookup_widget(GTK_WIDGET(button), "remarks_dialog");
   gtk_widget_destroy(window);
}


GdkColor remark_number_to_colour(int remark_number) {

   GdkColor colour;
   colour.red   = 65535;
   colour.green = 65535;
   colour.blue  = 65535; 
   colour.pixel = 65535; 
   if (remark_number == 2) { 
      colour.blue  = 60000;
   }
   if (remark_number == 3) { 
      colour.red   = 60000;
   }
   if (remark_number == 4) { 
      colour.green  = 60000;
   }
   if (remark_number == 5) { 
      colour.green = 62000;
      colour.blue  = 62000;
   }
   if (remark_number == 280) {
      colour.green  = 61000;
      colour.red    = 62500;
   }
   if (remark_number == 350) {
      colour.green  = 61000;
      colour.blue   = 61500;
   }
   if (remark_number == 465) { 
      colour.blue   = 60000;
      colour.green  = 60000;
   }
   return colour;
} 


void on_simple_text_dialog_close_button_pressed( GtkWidget *button,
						 GtkWidget *dialog) {
   gtk_widget_destroy(dialog);
}


void simple_text_dialog(const std::string &dialog_title, const std::string &text,
			int geom_x, int geom_y) {

   if (graphics_info_t::use_graphics_interface_flag) {

      GtkWidget *d = gtk_dialog_new();
      gtk_object_set_data(GTK_OBJECT(d), "simple_text_dialog", d);
      gtk_window_set_title (GTK_WINDOW (d), _(dialog_title.c_str()));
      GtkWidget *vbox = GTK_DIALOG(d)->vbox;
      GtkWidget *vbox_inner = gtk_vbox_new(FALSE, 2);
      GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window),
					    GTK_WIDGET(vbox_inner));
      gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(scrolled_window), TRUE, TRUE, 2);
      gtk_widget_show(scrolled_window);
      gtk_widget_show(vbox_inner);
      
      GtkWidget *text_widget = gtk_text_view_new ();
      gtk_widget_show (text_widget);
      gtk_container_add (GTK_CONTAINER (vbox_inner), text_widget);
      gtk_text_view_set_editable (GTK_TEXT_VIEW (text_widget), FALSE);
      gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_widget), GTK_WRAP_WORD);
      gtk_text_buffer_set_text (gtk_text_view_get_buffer(GTK_TEXT_VIEW (text_widget)),
				text.c_str(), -1);
      gtk_window_set_default_size(GTK_WINDOW(d), geom_x, geom_y);

      GtkWidget *close_button = gtk_dialog_add_button(GTK_DIALOG(d), "Close", 2);
      gtk_widget_show(close_button);

       g_signal_connect(G_OBJECT(close_button), "clicked",
 		       G_CALLBACK(on_simple_text_dialog_close_button_pressed),
 		       (gpointer) d);
      
      gtk_widget_show(d);

   }
}

void clear_generic_objects_dialog_pointer() {

   graphics_info_t g;
   g.generic_objects_dialog = NULL;
} 

/* Donna's request to do the counts in the Mutate Residue range dialog */
void mutate_molecule_dialog_check_counts(GtkWidget *res_no_1_widget, GtkWidget *res_no_2_widget,
					 GtkWidget *text_widget, GtkWidget *label_widget) {

   if (false) {
      std::cout << "res_no_1_widget " << res_no_1_widget << std::endl;
      std::cout << "res_no_2_widget " << res_no_2_widget << std::endl;
      std::cout << "text_widget " << text_widget << std::endl;
      std::cout << "label_widget " << label_widget << std::endl;
   }
   if (res_no_1_widget && res_no_2_widget) {
      if (text_widget && label_widget) {
	 std::string rn_1_str = gtk_entry_get_text(GTK_ENTRY(res_no_1_widget));
	 std::string rn_2_str = gtk_entry_get_text(GTK_ENTRY(res_no_2_widget));
	 GtkTextBuffer* tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_widget));
	 GtkTextIter startiter;
	 GtkTextIter enditer;
	 char *txt = NULL;
	 gtk_text_buffer_get_iter_at_offset(tb, &startiter, 0);
	 gtk_text_buffer_get_iter_at_offset(tb, &enditer, -1);
	 txt = gtk_text_buffer_get_text(tb, &startiter, &enditer, 0);

	 // if (txt) {
	 if (true) {
	    std::string sequence_str(txt);

	    try {

	       int t1_int = coot::util::string_to_int(rn_1_str);
	       int t2_int = coot::util::string_to_int(rn_2_str);
	       int counts = t2_int - t1_int;
	       int res_no_counts = counts + 1;

	       std::string res_no_diff_count_str("-");
	       std::string sequence_count_str("-");

	       if (counts >= 0)
		  res_no_diff_count_str = coot::util::int_to_string(res_no_counts);

	       int sequence_count = 0;
	       for (std::size_t i=0; i<sequence_str.size(); i++) {
		  char c = sequence_str[i];
		  if (c >= 'a' && c <= 'z') sequence_count++;
		  if (c >= 'A' && c <= 'Z') sequence_count++;
	       }
	       // std::cout << "debug:: sequence_str " << sequence_str << " gives sequence_count " << sequence_count << std::endl;
	       if (sequence_count > 0)
		  sequence_count_str = coot::util::int_to_string(sequence_count);

	       std::string label = "Counts: Residues ";
	       label += res_no_diff_count_str;
	       label += " Sequence: ";
	       label += sequence_count_str;

	       GtkWidget *red_light_widget   = lookup_widget(res_no_1_widget, "mutate_sequence_red_light_image");
	       GtkWidget *green_light_widget = lookup_widget(res_no_1_widget, "mutate_sequence_green_light_image");
	       bool show_green_light = false;
	       if (res_no_counts >= 1) {
		  if (sequence_count >= 1) {
		     if (res_no_counts == sequence_count) {
			label += " counts match";
			show_green_light = true;
		     }
		  }
	       }

	       if (show_green_light) {
		  gtk_widget_hide(red_light_widget);
		  gtk_widget_show(green_light_widget);
	       } else {
		  gtk_widget_show(red_light_widget);
		  gtk_widget_hide(green_light_widget);
	       }

	       gtk_label_set_text(GTK_LABEL(label_widget), label.c_str());

	    }
	    catch (const std::runtime_error &rte) {

	    }
	 } else {
	    std::cout << "Null text" << std::endl;
	 }
      }
   }
}


#include "cc-interface.hh"

/* handle_read_ccp4_map is now a .hh/c++ interface function, so give the callback an internal c function */
int handle_read_ccp4_map_internal(const char *fn, int is_difference_map) {

   int status = 0;
   if (fn) { 
      std::string file_name(fn);
      status = handle_read_ccp4_map(file_name, is_difference_map);
   }
   return status;
}
