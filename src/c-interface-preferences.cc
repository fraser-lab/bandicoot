/* src/c-interface-preferences.cc
 * 
 * Copyright 2005, 2006 The University of York
 * Author: Paul Emsley
 * Copyright 2007 The University of Oxford
 * Copyright 2014, 2015 by Medical Research Council
 * Author: Paul Emsley, Bernhard Lohkamp
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


#ifdef USE_PYTHON
#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include "compat/coot-sysdep.h"
#include "coot-utils/coot-package-paths.hh"


#ifndef HAVE_VECTOR
#define HAVE_VECTOR
#include <vector>
#endif // HAVE_VECTOR

#ifndef HAVE_STRING
#define HAVE_STRING
#include <string>
#endif // HAVE_STRING

#include <algorithm>
#include <fstream>  // Bandicoot pick-radius persistence
#include <cstdlib>  // atof
#include <cmath>    // fabs
#include <cstdio>   // snprintf

#include <string.h> // strlen, strcpy
#include <sys/types.h> // for stating
#include <sys/stat.h>
#if !defined _MSC_VER
#include <unistd.h>
#else
#define S_IRUSR S_IREAD
#define S_IWUSR S_IWRITE
#define S_IXUSR S_IEXEC
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#include <windows.h>
#include <direct.h>
#endif // _MSC_VER

#include <dirent.h>   // for extra scheme dir


#include "guile-fixups.h"

#include "graphics-info.h"
#include "interface.h"
#include "c-interface.h"
#include "c-interface-gtk-widgets.h"
#include "c-interface-preferences.h"
#include "c-interface-widgets.hh"
#include "cc-interface.hh"
#include "coot-preferences.h"

void preferences() {

  GtkWidget *w;
  show_preferences();

  update_preference_gui();

}

// ---------------------------------------------------------------------------
// Bandicoot: configurable atom-pick radii (Preferences > Pick Atom).
// Persisted in ~/.coot-preferences/bandicoot-pick-atom-radius as four
// whitespace-separated floats (one per line), in this order:
//    1. static-atom radius        (pick_atom_dist_cutoff)
//    2. symmetry-atom radius      (symm_pick_atom_dist_cutoff)
//    3. intermediate FAR radius   (intermediate_pick_far_cutoff)
//    4. intermediate NEAR radius  (intermediate_pick_near_cutoff)
// Re-applied at startup (bandicoot_load_pick_atom_radius, called from main).
// Trailing values may be absent (e.g. files written by an earlier build that
// only stored the first value) - missing ones keep their defaults.
// ---------------------------------------------------------------------------
static std::string bandicoot_pick_radius_dir() {
   std::string home = coot::get_home_dir();
   if (home.empty()) return "";
   return home + "/.coot-preferences";
}

void bandicoot_save_pick_atom_radius() {
   std::string dir = bandicoot_pick_radius_dir();
   if (dir.empty()) return;
   make_directory_maybe(dir.c_str());
   std::string fn = dir + "/bandicoot-pick-atom-radius";
   std::ofstream f(fn.c_str());
   if (f)
      f << graphics_info_t::pick_atom_dist_cutoff       << "\n"
        << graphics_info_t::symm_pick_atom_dist_cutoff  << "\n"
        << graphics_info_t::intermediate_pick_far_cutoff  << "\n"
        << graphics_info_t::intermediate_pick_near_cutoff << std::endl;
}

void bandicoot_load_pick_atom_radius() {
   std::string dir = bandicoot_pick_radius_dir();
   if (dir.empty()) return;
   std::string fn = dir + "/bandicoot-pick-atom-radius";
   std::ifstream f(fn.c_str());
   double v = 0.0;
   if (f && (f >> v) && v > 0.0 && v < 100.0) graphics_info_t::pick_atom_dist_cutoff = v;
   if (f && (f >> v) && v > 0.0 && v < 100.0) graphics_info_t::symm_pick_atom_dist_cutoff = v;
   if (f && (f >> v) && v > 0.0 && v < 100.0) graphics_info_t::intermediate_pick_far_cutoff = v;
   if (f && (f >> v) && v > 0.0 && v < 100.0) graphics_info_t::intermediate_pick_near_cutoff = v;
}

void set_pick_atom_distance_cutoff(float d) {
   if (d > 0.0)
      graphics_info_t::pick_atom_dist_cutoff = d;
   bandicoot_save_pick_atom_radius();
}

float get_pick_atom_distance_cutoff() {
   return graphics_info_t::pick_atom_dist_cutoff;
}

void set_symmetry_pick_atom_distance_cutoff(float d) {
   if (d > 0.0)
      graphics_info_t::symm_pick_atom_dist_cutoff = d;
   bandicoot_save_pick_atom_radius();
}

// far_lo == back/far tolerance, near_hi == front/near tolerance (depth-weighted)
void set_intermediate_pick_distance_cutoffs(float far_lo, float near_hi) {
   if (far_lo  > 0.0) graphics_info_t::intermediate_pick_far_cutoff  = far_lo;
   if (near_hi > 0.0) graphics_info_t::intermediate_pick_near_cutoff = near_hi;
   bandicoot_save_pick_atom_radius();
}

#ifdef __APPLE__
// Bandicoot tailoring of the Preferences dialog. Defined in the AppKit shim.
extern "C" int  bandicoot_sidebar_is_docked(void);
extern "C" void bandicoot_sidebar_set_docked_ext(int docked);

static void bandicoot_dock_toolbar_yes_toggled(GtkToggleButton *b, gpointer u) {
   if (gtk_toggle_button_get_active(b)) bandicoot_sidebar_set_docked_ext(1);
}
static void bandicoot_dock_toolbar_no_toggled(GtkToggleButton *b, gpointer u) {
   if (gtk_toggle_button_get_active(b)) bandicoot_sidebar_set_docked_ext(0);
}
static GtkWidget *bandicoot_ancestor_frame(GtkWidget *w) {
   while (w && !GTK_IS_FRAME(w)) w = gtk_widget_get_parent(w);
   return w;
}

// ---- Bandicoot "Pick Atom" preferences tab --------------------------------
// Adds a "Pick Atom" notebook page (its own page, like Console/Tips/etc.)
// carrying three framed radius controls: static atoms, symmetry atoms (both
// single-value 0.4/0.6/0.8/1.0 + Other), and intermediate/refinement atoms
// (a depth-weighted far-near RANGE: 0.4-0.8 / 0.6-1.0 / 0.8-1.2 + Other). Each
// applies live and persists. The page belongs to the "Others" category: it
// tracks the Others radio tool-button so it is shown only when "Others" is
// selected. Per-frame state lives in a heap struct freed with the frame.
struct bcoot_sv_ctrl { GtkWidget *entry; void (*setter)(float); };
struct bcoot_rg_ctrl { GtkWidget *lo; GtkWidget *hi; void (*setter)(float, float); };

static void bcoot_sv_radio_toggled(GtkToggleButton *b, gpointer data) {
   if (!gtk_toggle_button_get_active(b)) return;
   bcoot_sv_ctrl *c = (bcoot_sv_ctrl *) data;
   int tenths = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bcoot-tenths"));  // 0 == Other
   if (tenths == 0) {
      gtk_widget_set_sensitive(c->entry, TRUE);
      gtk_widget_grab_focus(c->entry);
      double v = atof(gtk_entry_get_text(GTK_ENTRY(c->entry)));
      if (v > 0.0) c->setter(v);
   } else {
      gtk_widget_set_sensitive(c->entry, FALSE);
      c->setter(tenths / 10.0);
   }
}

static void bcoot_sv_entry_activate(GtkEntry *e, gpointer data) {
   bcoot_sv_ctrl *c = (bcoot_sv_ctrl *) data;
   double v = atof(gtk_entry_get_text(GTK_ENTRY(c->entry)));
   if (v > 0.0) c->setter(v);
}

static void bcoot_rg_radio_toggled(GtkToggleButton *b, gpointer data) {
   if (!gtk_toggle_button_get_active(b)) return;
   bcoot_rg_ctrl *c = (bcoot_rg_ctrl *) data;
   // lo (far) stored in hundredths (4/6/8 -> 0.04/0.06/0.08), 0 == Other.
   // hi (near) stored in tenths (8/10/12 -> 0.8/1.0/1.2).
   int lo_h = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bcoot-lo-hundredths"));
   int hi_t = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bcoot-hi-tenths"));
   if (lo_h == 0) {
      gtk_widget_set_sensitive(c->lo, TRUE);
      gtk_widget_set_sensitive(c->hi, TRUE);
      gtk_widget_grab_focus(c->lo);
      double lo = atof(gtk_entry_get_text(GTK_ENTRY(c->lo)));
      double hi = atof(gtk_entry_get_text(GTK_ENTRY(c->hi)));
      if (lo > 0.0 && hi > 0.0) c->setter(lo, hi);
   } else {
      gtk_widget_set_sensitive(c->lo, FALSE);
      gtk_widget_set_sensitive(c->hi, FALSE);
      c->setter(lo_h / 100.0, hi_t / 10.0);
   }
}

static void bcoot_rg_entry_activate(GtkEntry *e, gpointer data) {
   bcoot_rg_ctrl *c = (bcoot_rg_ctrl *) data;
   double lo = atof(gtk_entry_get_text(GTK_ENTRY(c->lo)));
   double hi = atof(gtk_entry_get_text(GTK_ENTRY(c->hi)));
   if (lo > 0.0 && hi > 0.0) c->setter(lo, hi);
}

// Single-value radius frame: 0.4 / 0.6 / 0.8 / 1.0 + Other.
static GtkWidget *bcoot_build_sv_frame(const char *title, double cur, void (*setter)(float)) {
   bcoot_sv_ctrl *c = g_new0(bcoot_sv_ctrl, 1);
   GtkWidget *frame = gtk_frame_new(title);
   GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   gtk_container_add(GTK_CONTAINER(frame), vbox);

   GtkWidget *r04 = gtk_radio_button_new_with_label(NULL, "0.4 \xC3\x85");
   GtkWidget *r06 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r04), "0.6 \xC3\x85");
   GtkWidget *r08 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r04), "0.8 \xC3\x85");
   GtkWidget *r10 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r04), "1.0 \xC3\x85");
   GtkWidget *other_box = gtk_hbox_new(FALSE, 6);
   GtkWidget *rother = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r04), "Other:");
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
   c->entry = entry; c->setter = setter;
   gtk_box_pack_start(GTK_BOX(other_box), rother, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), entry,  FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), gtk_label_new("\xC3\x85"), FALSE, FALSE, 0);

   gtk_box_pack_start(GTK_BOX(vbox), r04, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), r06, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), r08, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), r10, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), other_box, FALSE, FALSE, 0);

   g_object_set_data(G_OBJECT(r04),    "bcoot-tenths", GINT_TO_POINTER(4));
   g_object_set_data(G_OBJECT(r06),    "bcoot-tenths", GINT_TO_POINTER(6));
   g_object_set_data(G_OBJECT(r08),    "bcoot-tenths", GINT_TO_POINTER(8));
   g_object_set_data(G_OBJECT(r10),    "bcoot-tenths", GINT_TO_POINTER(10));
   g_object_set_data(G_OBJECT(rother), "bcoot-tenths", GINT_TO_POINTER(0));

   char buf[32];
   snprintf(buf, sizeof(buf), "%g", cur);
   gtk_entry_set_text(GTK_ENTRY(entry), buf);
   bool preset = (fabs(cur-0.4) < 1e-6) || (fabs(cur-0.6) < 1e-6) ||
                 (fabs(cur-0.8) < 1e-6) || (fabs(cur-1.0) < 1e-6);
   if      (fabs(cur-0.4) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r04), TRUE);
   else if (fabs(cur-0.6) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r06), TRUE);
   else if (fabs(cur-0.8) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r08), TRUE);
   else if (fabs(cur-1.0) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r10), TRUE);
   else                           gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rother), TRUE);
   gtk_widget_set_sensitive(entry, !preset);

   g_signal_connect(r04,    "toggled",  G_CALLBACK(bcoot_sv_radio_toggled), c);
   g_signal_connect(r06,    "toggled",  G_CALLBACK(bcoot_sv_radio_toggled), c);
   g_signal_connect(r08,    "toggled",  G_CALLBACK(bcoot_sv_radio_toggled), c);
   g_signal_connect(r10,    "toggled",  G_CALLBACK(bcoot_sv_radio_toggled), c);
   g_signal_connect(rother, "toggled",  G_CALLBACK(bcoot_sv_radio_toggled), c);
   g_signal_connect(entry,  "activate", G_CALLBACK(bcoot_sv_entry_activate), c);

   g_object_set_data_full(G_OBJECT(frame), "bcoot-ctrl", c, g_free);
   return frame;
}

// Depth-weighted range frame: lo == far tolerance, hi == near tolerance.
static GtkWidget *bcoot_build_rg_frame(const char *title, double cur_lo, double cur_hi,
                                       void (*setter)(float, float)) {
   bcoot_rg_ctrl *c = g_new0(bcoot_rg_ctrl, 1);
   GtkWidget *frame = gtk_frame_new(title);
   GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   gtk_container_add(GTK_CONTAINER(frame), vbox);

   GtkWidget *r1 = gtk_radio_button_new_with_label(NULL, "0.04 \xC3\x85 - 0.8 \xC3\x85");
   GtkWidget *r2 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "0.06 \xC3\x85 - 1.0 \xC3\x85");
   GtkWidget *r3 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "0.08 \xC3\x85 - 1.2 \xC3\x85");
   GtkWidget *other_box = gtk_hbox_new(FALSE, 6);
   GtkWidget *rother = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "Other:");
   GtkWidget *lo = gtk_entry_new();  gtk_entry_set_width_chars(GTK_ENTRY(lo), 5);
   GtkWidget *hi = gtk_entry_new();  gtk_entry_set_width_chars(GTK_ENTRY(hi), 5);
   c->lo = lo; c->hi = hi; c->setter = setter;
   gtk_box_pack_start(GTK_BOX(other_box), rother, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), lo, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), gtk_label_new("\xC3\x85 -"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), hi, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(other_box), gtk_label_new("\xC3\x85"), FALSE, FALSE, 0);

   gtk_box_pack_start(GTK_BOX(vbox), r1, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), r2, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), r3, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), other_box, FALSE, FALSE, 0);

   g_object_set_data(G_OBJECT(r1), "bcoot-lo-hundredths", GINT_TO_POINTER(4));
   g_object_set_data(G_OBJECT(r1), "bcoot-hi-tenths",     GINT_TO_POINTER(8));
   g_object_set_data(G_OBJECT(r2), "bcoot-lo-hundredths", GINT_TO_POINTER(6));
   g_object_set_data(G_OBJECT(r2), "bcoot-hi-tenths",     GINT_TO_POINTER(10));
   g_object_set_data(G_OBJECT(r3), "bcoot-lo-hundredths", GINT_TO_POINTER(8));
   g_object_set_data(G_OBJECT(r3), "bcoot-hi-tenths",     GINT_TO_POINTER(12));
   g_object_set_data(G_OBJECT(rother), "bcoot-lo-hundredths", GINT_TO_POINTER(0));

   char b1[32], b2[32];
   snprintf(b1, sizeof(b1), "%g", cur_lo);
   snprintf(b2, sizeof(b2), "%g", cur_hi);
   gtk_entry_set_text(GTK_ENTRY(lo), b1);
   gtk_entry_set_text(GTK_ENTRY(hi), b2);
   bool preset = true;
   if      (fabs(cur_lo-0.04) < 1e-6 && fabs(cur_hi-0.8) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r1), TRUE);
   else if (fabs(cur_lo-0.06) < 1e-6 && fabs(cur_hi-1.0) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r2), TRUE);
   else if (fabs(cur_lo-0.08) < 1e-6 && fabs(cur_hi-1.2) < 1e-6) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r3), TRUE);
   else { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rother), TRUE); preset = false; }
   gtk_widget_set_sensitive(lo, !preset);
   gtk_widget_set_sensitive(hi, !preset);

   g_signal_connect(r1,     "toggled",  G_CALLBACK(bcoot_rg_radio_toggled), c);
   g_signal_connect(r2,     "toggled",  G_CALLBACK(bcoot_rg_radio_toggled), c);
   g_signal_connect(r3,     "toggled",  G_CALLBACK(bcoot_rg_radio_toggled), c);
   g_signal_connect(rother, "toggled",  G_CALLBACK(bcoot_rg_radio_toggled), c);
   g_signal_connect(lo,     "activate", G_CALLBACK(bcoot_rg_entry_activate), c);
   g_signal_connect(hi,     "activate", G_CALLBACK(bcoot_rg_entry_activate), c);

   g_object_set_data_full(G_OBJECT(frame), "bcoot-ctrl", c, g_free);
   return frame;
}

// Show/hide the Pick Atom page in step with the "Others" category button.
static void bandicoot_pick_atom_follow_other(GtkToggleToolButton *b, gpointer page) {
   if (gtk_toggle_tool_button_get_active(b))
      gtk_widget_show(GTK_WIDGET(page));
   else
      gtk_widget_hide(GTK_WIDGET(page));
}

static void bandicoot_add_pick_atom_tab(GtkWidget *prefs) {
   GtkWidget *nb = lookup_widget(prefs, "preferences_notebook");
   if (!nb || !GTK_IS_NOTEBOOK(nb)) return;

   GtkWidget *page = gtk_vbox_new(FALSE, 8);
   gtk_container_set_border_width(GTK_CONTAINER(page), 12);

   gtk_box_pack_start(GTK_BOX(page),
      bcoot_build_sv_frame("Atom Pick Radius",
                           graphics_info_t::pick_atom_dist_cutoff,
                           set_pick_atom_distance_cutoff),
      FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(page),
      bcoot_build_sv_frame("Symmetry Atom Pick Radius",
                           graphics_info_t::symm_pick_atom_dist_cutoff,
                           set_symmetry_pick_atom_distance_cutoff),
      FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(page),
      bcoot_build_rg_frame("Intermediate Atom Pick Radius",
                           graphics_info_t::intermediate_pick_far_cutoff,
                           graphics_info_t::intermediate_pick_near_cutoff,
                           set_intermediate_pick_distance_cutoffs),
      FALSE, FALSE, 0);

   GtkWidget *tab_label = gtk_label_new("Pick Atom");
   gtk_notebook_append_page(GTK_NOTEBOOK(nb), page, tab_label);
   gtk_widget_show(tab_label);
   gtk_widget_show_all(page);

   // Belong to the "Others" category: follow its radio tool-button and match
   // its current state (so the page is hidden unless "Others" is selected).
   GtkWidget *other_btn = lookup_widget(prefs, "preferences_other_radiotoolbutton");
   if (other_btn && GTK_IS_TOGGLE_TOOL_BUTTON(other_btn)) {
      g_signal_connect(other_btn, "toggled",
                       G_CALLBACK(bandicoot_pick_atom_follow_other), page);
      if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(other_btn)))
         gtk_widget_hide(page);
   }
}

// Tailor the Refinement-Toolbar preferences for Bandicoot's native UI:
//  - drop the "Main Toolbar" tab (we use the native macOS Customize Toolbar);
//  - replace the "Show Toolbar?" frame (show/hide + screen-edge position, none
//    of which apply) with a fresh "Dock Toolbar?" Yes/No wired to the sidebar.
static void bandicoot_fixup_preferences(GtkWidget *prefs) {
   if (!prefs) return;

   // 1. Remove the "Main Toolbar" notebook page.
   GtkWidget *nb = lookup_widget(prefs, "preferences_notebook");
   GtkWidget *mt = lookup_widget(prefs, "preferences_main_toolbar_style");
   if (nb && mt) {
      GtkWidget *page = mt;
      while (page && gtk_widget_get_parent(page) != nb) page = gtk_widget_get_parent(page);
      if (page) {
         int n = gtk_notebook_page_num(GTK_NOTEBOOK(nb), page);
         if (n >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(nb), n);
      }
   }

   // 2. Reuse the "Show Toolbar?" frame as "Dock Toolbar?": relabel it, hide its
   //    show/hide + position radios, and drop fresh Yes/No into the same box.
   //    (Reusing the frame keeps the existing layout slot; the old radios stay
   //    in-tree but hidden so update_preference_gui() can still set them safely.)
   GtkWidget *show_rb = lookup_widget(prefs, "preferences_model_toolbar_show_radiobutton");
   GtkWidget *hide_rb = lookup_widget(prefs, "preferences_model_toolbar_hide_radiobutton");
   GtkWidget *pos_box = lookup_widget(prefs, "hbox356");
   GtkWidget *box     = show_rb ? gtk_widget_get_parent(show_rb) : NULL;  // vbox285
   GtkWidget *frame   = bandicoot_ancestor_frame(show_rb);

   if (frame) gtk_frame_set_label(GTK_FRAME(frame), "Dock Toolbar?");
   if (show_rb) gtk_widget_hide(show_rb);
   if (hide_rb) gtk_widget_hide(hide_rb);
   if (pos_box) gtk_widget_hide(pos_box);

   if (box && GTK_IS_BOX(box)) {
      GtkWidget *yes = gtk_radio_button_new_with_label(NULL, "Yes");
      GtkWidget *no  = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(yes), "No");
      gtk_box_pack_start(GTK_BOX(box), yes, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), no,  FALSE, FALSE, 0);
      gtk_box_reorder_child(GTK_BOX(box), yes, 0);
      gtk_box_reorder_child(GTK_BOX(box), no,  1);
      // Initial state from the sidebar — set BEFORE connecting so it doesn't dock.
      if (bandicoot_sidebar_is_docked())
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(yes), TRUE);
      else
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(no), TRUE);
      g_signal_connect(yes, "toggled", G_CALLBACK(bandicoot_dock_toolbar_yes_toggled), NULL);
      g_signal_connect(no,  "toggled", G_CALLBACK(bandicoot_dock_toolbar_no_toggled), NULL);
      gtk_widget_show(yes);
      gtk_widget_show(no);
   }

   // The frame row used to be sized for ~6 radios and expands to fill the
   // column; with just Yes/No that leaves a tall empty frame. Stop its row
   // (hbox379) from expanding so the frame hugs its content.
   if (frame) {
      GtkWidget *row = gtk_widget_get_parent(frame);          // hbox379
      GtkWidget *col = row ? gtk_widget_get_parent(row) : NULL; // vbox274
      if (row && col && GTK_IS_BOX(col))
         gtk_box_set_child_packing(GTK_BOX(col), row, FALSE, FALSE, 0, GTK_PACK_START);
   }

   // 3. Add the "Pick Atom" tab (atom-pick radius control).
   bandicoot_add_pick_atom_tab(prefs);
}
#endif

void show_preferences(){

  GtkWidget *w = create_preferences();
  graphics_info_t::preferences_widget = w;

  if (1) { 
     GtkWidget *scrolled_win_model_toolbar = lookup_widget(w, "preferences_model_toolbar_icons_scrolledwindow");
     fill_preferences_model_toolbar_icons(w, scrolled_win_model_toolbar);
     GtkWidget *scrolled_win_main_toolbar = lookup_widget(w, "preferences_main_toolbar_icons_scrolledwindow");
     fill_preferences_main_toolbar_icons(w, scrolled_win_main_toolbar);

     GtkComboBox *combobox;
     // fill the bond combobox
     combobox = GTK_COMBO_BOX(lookup_widget(w, "preferences_bond_width_combobox"));
     for (int j=1; j<21; j++) {
	std::string s = graphics_info_t::int_to_string(j);
	gtk_combo_box_append_text(combobox, s.c_str());
     }
     // fill the font combobox
     combobox = GTK_COMBO_BOX(lookup_widget(w, "preferences_font_size_combobox"));
     std::vector<std::string> fonts;  
     fonts.push_back("Times Roman 10");
     fonts.push_back("Times Roman 24");
     fonts.push_back("Fixed 8/13");
     fonts.push_back("Fixed 9/15");
     for (unsigned int j=0; j<fonts.size(); j++) {
	gtk_combo_box_append_text(combobox, fonts[j].c_str());
     }
  }

#ifdef __APPLE__
  bandicoot_fixup_preferences(w);   // native-UI tailoring (see above)
#endif

  gtk_widget_show(w);

}

void clear_preferences() {

   graphics_info_t::preferences_widget = NULL;

}

void set_mark_cis_peptides_as_bad(int istate) {

   graphics_info_t::mark_cis_peptides_as_bad_flag = istate;
}

int show_mark_cis_peptides_as_bad_state() {

   return graphics_info_t::mark_cis_peptides_as_bad_flag;

}

void show_hide_preferences_tabs(GtkToggleToolButton *toggletoolbutton, int preference_type) {

  GtkWidget *frame;

  std::vector<std::string> preferences_tabs;
  
  if (preference_type == COOT_GENERAL_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_general_tabs;
  }
  if (preference_type == COOT_BOND_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_bond_tabs;
  }
  if (preference_type == COOT_GEOMETRY_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_geometry_tabs;
  }
  if (preference_type == COOT_COLOUR_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_colour_tabs;
  }
  if (preference_type == COOT_MAP_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_map_tabs;
  }
  if (preference_type == COOT_OTHER_PREFERENCES) {
    preferences_tabs = *graphics_info_t::preferences_other_tabs;
  }

  for (unsigned int i=0; i<preferences_tabs.size(); i++) {
    frame = lookup_widget(GTK_WIDGET(toggletoolbutton), preferences_tabs[i].c_str());
    if (gtk_toggle_tool_button_get_active(toggletoolbutton)){
	  gtk_widget_show(frame);
	} else {
	  gtk_widget_hide(frame);
	}
  }

}

#include "c-interface-preferences.h"

void make_preferences_internal() {
  
  graphics_info_t g;
  g.make_preferences_internal();
}

void make_preferences_internal_default() {

  make_preferences_internal();
  graphics_info_t g;
  g.preferences_internal_default = g.preferences_internal;

}

void reset_preferences() {

  graphics_info_t g;
  //std::vector<coot::preference_info_t> *ret = g.preferences_internal_default;
  g.preferences_internal = g.preferences_internal_default;
  update_preference_gui();

}


// update and populate Preferences GUI according to (preference file) setting
void update_preference_gui() {

  GtkWidget *dialog;
  GtkWidget *w;
  GtkWidget *colour_button;
  GtkAdjustment *adjustment;
  GtkWidget *entry;
  const gchar *gtext;
  std::string text;
  int preference_type;
  int ivalue;
  int ivalue2;
  float fval1;
  float fval2;
  float fval3;
  std::vector<int> ivector;
  unsigned short int v = 4;
  graphics_info_t g;
  
  dialog = g.preferences_widget;

  for (unsigned int i=0; i<g.preferences_internal.size(); i++) {
    preference_type = g.preferences_internal[i].preference_type;
    //std::cout <<"BL DEBUG:: type "<< preference_type<<" int " <<g.preferences_internal[i].ivalue << " int default " << g.preferences_internal[i].ivalue<<std::endl;

    switch (preference_type) {
      
    case PREFERENCES_FILE_CHOOSER:
      w = lookup_widget(dialog, "preferences_filechooser_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_filechooser_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_FILE_OVERWRITE:
      w = lookup_widget(dialog, "preferences_file_overwrite_yes_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_file_overwrite_no_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_FILE_FILTER:
      w = lookup_widget(dialog, "preferences_file_filter_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_file_filter_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_FILE_SORT_DATE:
      w = lookup_widget(dialog, "preferences_file_sort_by_date_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_file_sort_by_date_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_ACCEPT_DIALOG_DOCKED:
      w = lookup_widget(dialog, "preferences_dialog_accept_docked_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_dialog_accept_detouched_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_ACCEPT_DIALOG_DOCKED_SHOW:
      w = lookup_widget(dialog, "preferences_dialog_accept_docked_show_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_dialog_accept_docked_hide_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_IMMEDIATE_REPLACEMENT:
      w = lookup_widget(dialog, "preferences_dialog_accept_off_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_dialog_accept_on_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_VT_SURFACE:
      w = lookup_widget(dialog, "preferences_hid_spherical_radiobutton");
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue == 2) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_hid_flat_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_RECENTRE_PDB:
      w = lookup_widget(dialog, "preferences_recentre_pdb_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_recentre_pdb_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;      

    case PREFERENCES_BONDS_THICKNESS:
      w = lookup_widget(dialog, "preferences_bond_width_combobox");
      ivalue = g.preferences_internal[i].ivalue1;
      ivalue -= 1;      // offset
#if (GTK_MAJOR_VERSION > 1)
      gtk_combo_box_set_active(GTK_COMBO_BOX(w), ivalue);
#endif
      break;

    case PREFERENCES_BOND_COLOURS_MAP_ROTATION:
      w = lookup_widget(dialog, "preferences_bond_colours_hscale");
      fval1 = g.preferences_internal[i].fvalue1;
      adjustment = gtk_range_get_adjustment(GTK_RANGE(w));
      gtk_adjustment_set_value(adjustment, fval1);
      break;

    case PREFERENCES_BOND_COLOUR_ROTATION_C_ONLY:
      w = lookup_widget(dialog, "preferences_bond_colours_checkbutton");
      if (g.preferences_internal[i].ivalue1 == 1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
      }
      break;

    case PREFERENCES_MAP_RADIUS:
      w = lookup_widget(dialog, "preferences_map_radius_entry");
      text = graphics_info_t::float_to_string(g.preferences_internal[i].fvalue1);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_MAP_ISOLEVEL_INCREMENT:
      w = lookup_widget(dialog, "preferences_map_increment_size_entry");
      text = graphics_info_t::float_to_string_using_dec_pl(g.preferences_internal[i].fvalue1, v);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_DIFF_MAP_ISOLEVEL_INCREMENT:
      w = lookup_widget(dialog, "preferences_map_diff_increment_entry");
      text = graphics_info_t::float_to_string_using_dec_pl(g.preferences_internal[i].fvalue1, v);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_MAP_SAMPLING_RATE:
      w = lookup_widget(dialog, "preferences_map_sampling_entry");
      text = graphics_info_t::float_to_string_using_dec_pl(g.preferences_internal[i].fvalue1, v);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_DYNAMIC_MAP_SAMPLING:
      w = lookup_widget(dialog, "preferences_map_dynamic_sampling_checkbutton");
      if (g.preferences_internal[i].ivalue1 == 1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
      }
      break;

    case PREFERENCES_DYNAMIC_MAP_SIZE_DISPLAY:
      w = lookup_widget(dialog, "preferences_map_dynamic_size_checkbutton");
      if (g.preferences_internal[i].ivalue1 == 1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
      }
      break;

    case PREFERENCES_SWAP_DIFF_MAP_COLOURS:
      w = lookup_widget(dialog, "preferences_diff_map_colours_o_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_diff_map_colours_coot_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MAP_COLOURS_MAP_ROTATION:
      w = lookup_widget(dialog, "preferences_map_colours_hscale");
      fval1 = g.preferences_internal[i].fvalue1;
      adjustment = gtk_range_get_adjustment(GTK_RANGE(w));
      gtk_adjustment_set_value(adjustment, fval1);
      break;

    case PREFERENCES_SMOOTH_SCROLL:
      w = lookup_widget(dialog, "preferences_smooth_scroll_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_smooth_scroll_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_SMOOTH_SCROLL_STEPS:
      w = lookup_widget(dialog, "preferences_smooth_scroll_steps_entry");
      text = graphics_info_t::int_to_string(g.preferences_internal[i].ivalue1);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_SMOOTH_SCROLL_LIMIT:
      w = lookup_widget(dialog, "preferences_smooth_scroll_limit_entry");
      text = graphics_info_t::float_to_string(g.preferences_internal[i].fvalue1);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_MAP_DRAG:
      w = lookup_widget(dialog, "preferences_map_drag_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_map_drag_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MARK_CIS_BAD:
      w = lookup_widget(dialog, "preferences_geometry_cis_peptide_bad_yes_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_geometry_cis_peptide_bad_no_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_BG_COLOUR:
      fval1 = g.preferences_internal[i].fvalue1;  // red
      fval2 = g.preferences_internal[i].fvalue2;  // green
      fval3 = g.preferences_internal[i].fvalue3;  // blue
      colour_button = lookup_widget(dialog, "preferences_bg_colour_colorbutton");
      GdkColor bg_colour;
      if (fval1 < 0.01 && fval2 < 0.01 && fval3 < 0.01) {
	// black
	w = lookup_widget(dialog, "preferences_bg_colour_black_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	bg_colour.red = 0;
	bg_colour.green = 0;
	bg_colour.blue = 0;
      } else if (fval1 > 0.99 && fval2 > 0.99 && fval3 > 0.99) {
	// white
	w = lookup_widget(dialog, "preferences_bg_colour_white_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	bg_colour.red = 65535;
	bg_colour.green = 65535;
	bg_colour.blue = 65535;
      } else {
	// other colour
	w = lookup_widget(dialog, "preferences_bg_colour_own_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	bg_colour.red = (guint)(fval1 * 65535);
	bg_colour.green = (guint)(fval2 * 65535);
	bg_colour.blue = (guint)(fval3 * 65535);
      }
      gtk_color_button_set_color(GTK_COLOR_BUTTON(colour_button), &bg_colour);
      break;

    case PREFERENCES_ANTIALIAS:
      w = lookup_widget(dialog, "preferences_antialias_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_antialias_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_CONSOLE_COMMANDS:
      w = lookup_widget(dialog, "preferences_console_info_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_console_info_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_TIPS:
      w = lookup_widget(dialog, "preferences_tips_on_radiobutton");
      if (g.preferences_internal[i].ivalue1) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_tips_off_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_REFINEMENT_SPEED:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue == 4) {
	 w = lookup_widget(dialog, "preferences_refinement_speed_molasses_radiobutton");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 120) {
	 w = lookup_widget(dialog, "preferences_refinement_speed_crock_radiobutton");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 80) {
	 w = lookup_widget(dialog, "preferences_refinement_speed_default_radiobutton");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	 w = lookup_widget(dialog, "preferences_refinement_speed_own_radiobutton");
	 entry = lookup_widget(dialog, "preferences_refinement_speed_entry");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	 if (ivalue <= 0) {
	   std::cout<<"WARNING:: your refinement speed setting is 0 or below \nReset to default 80"<< std::endl;
	   ivalue = 80;
	 }
	 text = graphics_info_t::int_to_string(ivalue);
	 gtk_entry_set_text(GTK_ENTRY(entry), text.c_str());
      }
      break;

    case PREFERENCES_SPIN_SPEED:
      w = lookup_widget(dialog, "preferences_spin_speed_entry");
      text = graphics_info_t::float_to_string(g.preferences_internal[i].fvalue1);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_FONT_SIZE:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue < 2) {
	w = lookup_widget(dialog, "preferences_font_size_small_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 2) {
	w = lookup_widget(dialog, "preferences_font_size_medium_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 3) {
	w = lookup_widget(dialog, "preferences_font_size_large_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_font_size_others_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	ivalue -= 4;      // offset
	GtkComboBox *combobox = GTK_COMBO_BOX(lookup_widget(w, "preferences_font_size_combobox"));
	gtk_combo_box_set_active(combobox, ivalue);
      }
      break;

    case PREFERENCES_FONT_COLOUR:
      fval1 = g.preferences_internal[i].fvalue1;  // red
      fval2 = g.preferences_internal[i].fvalue2;  // green
      fval3 = g.preferences_internal[i].fvalue3;  // blue
      colour_button = lookup_widget(dialog, "preferences_font_colorbutton");
      GdkColor font_colour;
      if (fval1 >= 0.999 && 
	  fval2 >= 0.799 && fval2 <= 0.801 &&
	  fval3 >= 0.799 && fval3 <= 0.801) {
	// default
	w = lookup_widget(dialog, "preferences_font_colour_default_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	font_colour.red   = (guint)(1.0 * 65535);
	font_colour.green = (guint)(0.8 * 65535);
	font_colour.blue  = (guint)(0.8 * 65535);

      } else {
	// other colour

	w = lookup_widget(dialog, "preferences_font_colour_own_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
	font_colour.red   = (guint)(fval1 * 65535);
	font_colour.green = (guint)(fval2 * 65535);
	font_colour.blue  = (guint)(fval3 * 65535);
      }
      gtk_color_button_set_color(GTK_COLOR_BUTTON(colour_button), &font_colour);

      break;

    case PREFERENCES_PINK_POINTER:
      w = lookup_widget(dialog, "preferences_pink_pointer_entry");
      text = graphics_info_t::float_to_string(g.preferences_internal[i].fvalue1);
      gtk_entry_set_text(GTK_ENTRY(w), text.c_str());
      break;

    case PREFERENCES_MODEL_TOOLBAR_SHOW:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue == 0) {
	w = lookup_widget(dialog, "preferences_model_toolbar_hide_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_model_toolbar_show_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MODEL_TOOLBAR_POSITION:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue == 0) {
	w = lookup_widget(dialog, "preferences_model_toolbar_right_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 1){
	w = lookup_widget(dialog, "preferences_model_toolbar_left_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 2){
	w = lookup_widget(dialog, "preferences_model_toolbar_top_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 3){
	w = lookup_widget(dialog, "preferences_model_toolbar_bottom_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MODEL_TOOLBAR_STYLE:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue <= 1) {
	w = lookup_widget(dialog, "preferences_model_toolbar_style_icons_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 2) {
	w = lookup_widget(dialog, "preferences_model_toolbar_style_both_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_model_toolbar_style_text_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MODEL_TOOLBAR_ICONS:
      ivalue  = g.preferences_internal[i].ivalue1;
      ivalue2 = g.preferences_internal[i].ivalue2;
      if (ivalue2 == 1) {
	// show
	show_model_toolbar_icon(ivalue);
      } else {
	// hide
	hide_model_toolbar_icon(ivalue);
      }
      break;

    case PREFERENCES_MAIN_TOOLBAR_SHOW:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue == 0) {
	w = lookup_widget(dialog, "preferences_main_toolbar_hide_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_main_toolbar_show_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    //case PREFERENCES_MAIN_TOOLBAR_POSITION:
    //  ivalue = g.preferences_internal[i].ivalue1;
    //  if (ivalue == 0) {
//	w = lookup_widget(dialog, "preferences_model_toolbar_right_radiobutton");
	//gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      //} else if (ivalue == 1){
//	w = lookup_widget(dialog, "preferences_model_toolbar_left_radiobutton");
//	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
 //     } else if (ivalue == 2){
//	w = lookup_widget(dialog, "preferences_model_toolbar_top_radiobutton");
//	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
 //     } else if (ivalue == 3){
//	w = lookup_widget(dialog, "preferences_model_toolbar_bottom_radiobutton");
//	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
 //     }
  //    break;

    case PREFERENCES_MAIN_TOOLBAR_STYLE:
      ivalue = g.preferences_internal[i].ivalue1;
      if (ivalue <= 1) {
	w = lookup_widget(dialog, "preferences_main_toolbar_style_icons_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else if (ivalue == 2) {
	w = lookup_widget(dialog, "preferences_main_toolbar_style_both_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      } else {
	w = lookup_widget(dialog, "preferences_main_toolbar_style_text_radiobutton");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
      }
      break;

    case PREFERENCES_MAIN_TOOLBAR_ICONS:
      ivalue  = g.preferences_internal[i].ivalue1;
      ivalue2 = g.preferences_internal[i].ivalue2;
      if (ivalue2 == 1) {
	// show
	show_main_toolbar_icon(ivalue);
      } else {
	// hide
	hide_main_toolbar_icon(ivalue);
      }
      break;
    }
  }
}

void save_preferences() {

  graphics_info_t g;
  short int istat = 1;
  short int il;
  std::string preferences_name;
  std::string file_name;
  std::string directory = coot::package_data_dir();

  // only save things if we didn't start with --no-startup-scripts
  //
  if (! g.run_startup_scripts_flag)
     return;
  
  std::string tmp_directory = coot::get_home_dir();
  if (!tmp_directory.empty()) {
    directory = tmp_directory;
  }

  directory += "/.coot-preferences/";

  int status = make_directory_maybe(directory.c_str());
  if (status != 0) {
    std::cout<<"ERROR:: Cannot find directory "<< directory <<
      "        Will not be able to save preferences"<<std::endl;
  } else {

#ifdef USE_GUILE
    preferences_name = "coot-preferences.scm";
    file_name = directory + preferences_name;
    il = 1;
    istat = g.save_preference_file(file_name, il);
#endif // USE_GUILE
#ifdef USE_PYTHON
    preferences_name = "coot_preferences.py";
    file_name = directory + preferences_name;
    il = 2;
    istat = g.save_preference_file(file_name, il);
#endif // USE_PYTHON
  }
}
 

void preferences_internal_change_value_int(int preference_type, int ivalue) {
  graphics_info_t g;
  g.preferences_internal_change_value(preference_type, ivalue);
}

void preferences_internal_change_value_int2(int preference_type, int ivalue1, int ivalue2) {
  graphics_info_t g;
  g.preferences_internal_change_value(preference_type, ivalue1, ivalue2);
}

void preferences_internal_change_value_float(int preference_type, float fvalue) {
  graphics_info_t g;
  g.preferences_internal_change_value(preference_type, fvalue);
}

void preferences_internal_change_value_float3(int preference_type,
				       float fvalue1, float fvalue2, float fvalue3) {
  graphics_info_t g;
  g.preferences_internal_change_value(preference_type, fvalue1, fvalue2, fvalue3);
}

// FIXME:: make this generic with TOOLBAR enums
void
show_model_toolbar_icon(int pos) {
  graphics_info_t g;
  g.show_hide_toolbar_icon_pos(pos, 1, MODEL_TOOLBAR);
}

void
hide_model_toolbar_icon(int pos) {
  graphics_info_t g;
  g.show_hide_toolbar_icon_pos(pos, 0, MODEL_TOOLBAR);
}

void
show_main_toolbar_icon(int pos) {
  graphics_info_t g;
  g.show_hide_toolbar_icon_pos(pos, 1, MAIN_TOOLBAR);
}

void
hide_main_toolbar_icon(int pos) {
  graphics_info_t g;
  g.show_hide_toolbar_icon_pos(pos, 0, MAIN_TOOLBAR);
}

void
fill_preferences_model_toolbar_icons(GtkWidget *preferences,
				     GtkWidget *scrolled_window) {

  graphics_info_t g;
#if (GTK_MAJOR_VERSION >1)
  g.fill_preferences_model_toolbar_icons(preferences, scrolled_window);
#endif // GTK_MAJOR_VERSION
}

void
fill_preferences_main_toolbar_icons(GtkWidget *preferences,
				     GtkWidget *scrolled_window) {

  graphics_info_t g;
#if (GTK_MAJOR_VERSION >1)
  g.fill_preferences_main_toolbar_icons(preferences, scrolled_window);
#endif // GTK_MAJOR_VERSION
}
// end FIXME


GtkWidget *popup_window(const char *str) {

   GtkWidget *w = create_popup_info_window();
   GtkWidget *label = lookup_widget(w, "info_label");
   gtk_label_set_text(GTK_LABEL(label), str);
   return w;
}

void add_status_bar_text(const char *s) {

   graphics_info_t g;
   g.add_status_bar_text(std::string(s));
} 



/*  ----------------------------------------------------------------------- */
/*                  Other interface preferences                            */
/*  ----------------------------------------------------------------------- */

void set_model_fit_refine_dialog_stays_on_top(int istate) { 
   graphics_info_t::model_fit_refine_dialog_stays_on_top_flag = istate;
}

int model_fit_refine_dialog_stays_on_top_state() {

   return graphics_info_t::model_fit_refine_dialog_stays_on_top_flag;
} 


void save_accept_reject_dialog_window_position(GtkWidget *acc_rej_dialog) {

   // 20070801 crash reported by "Gajiwala, Ketan"

   // OK, we can reproduce a problem
   // Refine something
   // Close the window using WM delete window
   // Press return in Graphics window (globjects:key_press_event() GDK_Return case)
   // 
   // So, we need to set graphics_info_t::accept_reject_dialog to NULL
   // when we get a WM delete event on the Accept/Reject box

   if (acc_rej_dialog) { 
      gint upositionx, upositiony;
      if (acc_rej_dialog->window) {
	 gdk_window_get_root_origin (acc_rej_dialog->window, &upositionx, &upositiony);
	 graphics_info_t::accept_reject_dialog_x_position = upositionx;
	 graphics_info_t::accept_reject_dialog_y_position = upositiony;

      } else {
	 std::cout << "ERROR:: Trapped an error in save_accept_reject_dialog_window_position\n"
		   << "        Report to Central Control!\n"
		   << "        (What did you do to make this happen?)\n";
      }
   }
}

void set_accept_reject_dialog(GtkWidget *w) { /* used by callbacks to unset the widget.
						 (errr... it wasn't but it is now (as it
						 should be)). */

   graphics_info_t::accept_reject_dialog = w;
}

/* \brief set position of Model/Fit/Refine dialog */
void set_model_fit_refine_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::model_fit_refine_x_position = x_pos;
   graphics_info_t::model_fit_refine_y_position = y_pos;
}

/* \brief set position of Display Control dialog */
void set_display_control_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::display_manager_x_position = x_pos;
   graphics_info_t::display_manager_y_position = y_pos;
}

/* \brief set position of Go To Atom dialog */
void set_go_to_atom_window_position(int x_pos, int y_pos) {

   graphics_info_t::go_to_atom_window_x_position = x_pos;
   graphics_info_t::go_to_atom_window_y_position = y_pos;
}

/* \brief set position of Delete dialog */
void set_delete_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::delete_item_widget_x_position = x_pos;
   graphics_info_t::delete_item_widget_y_position = y_pos;
}

void set_rotate_translate_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::rotate_translate_x_position = x_pos;
   graphics_info_t::rotate_translate_y_position = y_pos;
}

/*! \brief set position of the Accept/Reject dialog */
void set_accept_reject_dialog_position(int x_pos, int y_pos) {
   graphics_info_t::accept_reject_dialog_x_position = x_pos;
   graphics_info_t::accept_reject_dialog_y_position = y_pos;
}

/*! \brief set position of the Ramachadran Plot dialog */
void set_ramachandran_plot_dialog_position(int x_pos, int y_pos) {
   graphics_info_t::ramachandran_plot_x_position = x_pos;
   graphics_info_t::ramachandran_plot_y_position = y_pos;
}

/*! \brief set edit chi angles dialog position */
void set_edit_chi_angles_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::edit_chi_angles_dialog_x_position = x_pos;
   graphics_info_t::edit_chi_angles_dialog_y_position = y_pos;
}


/*! \brief set rotamer selection dialog position */
void set_rotamer_selection_dialog_position(int x_pos, int y_pos) {

   graphics_info_t::rotamer_selection_dialog_x_position = x_pos;
   graphics_info_t::rotamer_selection_dialog_y_position = y_pos;
} 



/*  ------------------------------------------------------------------------ */
/*                     user define clicks                                    */
/*  ------------------------------------------------------------------------ */
#ifdef USE_GUILE
void user_defined_click_scm(int n_clicks, SCM func) {
  if (n_clicks > 0) {
    graphics_info_t g;
    g.user_defined_atom_pick_specs.clear();
    g.in_user_defined_define = n_clicks;
    SCM dest = SCM_BOOL_F;
    SCM mess = scm_makfrom0str("~s");
    SCM v = scm_simple_format(dest, mess, scm_list_1(func));
    std::string func_string = scm_to_locale_string(v);
    g.user_defined_click_scm_func = func;
    g.pick_cursor_maybe();
  } else {
    std::cout<<"INFO:: number of clicks less than 1, cannot define user click"<<std::endl;
  } 
} 
#endif // USE_GUILE

#ifdef USE_PYTHON
void user_defined_click_py(int n_clicks, PyObject *func) {
  if (n_clicks > 0) {
    graphics_info_t g;
    g.user_defined_atom_pick_specs.clear();
    g.in_user_defined_define = n_clicks;
    g.user_defined_click_py_func = func;
    Py_XINCREF(g.user_defined_click_py_func);
    g.pick_cursor_maybe();
  } else {
    std::cout<<"INFO:: number of clicks less than 1, cannot define user click"<<std::endl;
  } 
} 
#endif // USE_PYTHON
/*  ------------------------------------------------------------------------ */
/*                     state (a graphics_info thing)                         */
/*  ------------------------------------------------------------------------ */
void set_save_state_file_name(const char *filename) {
   graphics_info_t::save_state_file_name = filename; 
}

const char *save_state_file_name_raw() {
   return graphics_info_t::save_state_file_name.c_str();
}


#ifdef USE_GUILE
SCM save_state_file_name_scm() {

//    char *f = (char *) malloc(graphics_info_t::save_state_file_name.length() +1);
//    strcpy(f, graphics_info_t::save_state_file_name.c_str());
//    return f;

   std::string f = graphics_info_t::save_state_file_name;
   return scm_makfrom0str(f.c_str());
}
#endif // USE_GUILE

#ifdef USE_PYTHON
PyObject *save_state_file_name_py() {
   std::string f = graphics_info_t::save_state_file_name;
   return PyString_FromString(f.c_str());
}
#endif // USE_PYTHON




char *unmangle_hydrogen_name(const char *pdb_hydrogen_name) {

   std::string atom_name(pdb_hydrogen_name);
   std::string new_atom_name = atom_name;

   if (atom_name.length() == 4) { 
      if (atom_name[0] == '1' ||
	  atom_name[0] == '2' ||
	  atom_name[0] == '3' ||
	  atom_name[0] == '4' ||
	  atom_name[0] == '*') {
	 // switch it.
	 if (atom_name[3] == ' ') {
	    new_atom_name = " "; // lead with a space, testing.
	    new_atom_name += atom_name.substr(1,2) + atom_name[0];
	 } else { 
	    new_atom_name = atom_name.substr(1,3) + atom_name[0];
	 }
      }
   } else { 
      if (atom_name[3] != ' ') { 
	 if (atom_name[3] == ' ') {
	    new_atom_name = atom_name.substr(1,2) + atom_name[0];
	    new_atom_name += ' ';
	 }
	 if (atom_name[2] == ' ') {
	    new_atom_name = atom_name.substr(1,1) + atom_name[0];
	 new_atom_name += ' ';
	 new_atom_name += ' ';
	 }
      } else {
	 // atom_name length is 3 presumably
	 new_atom_name = ' ';
	 new_atom_name += atom_name.substr(1,2) + atom_name[0];
      }
   }

   int new_length = strlen(pdb_hydrogen_name) + 1;
   char *s = new char[new_length];
   for (int i=0; i<new_length; i++) s[i] = 0;
   strncpy(s, new_atom_name.c_str(), new_length);

//    std::cout << "mangle debug:: :" << pdb_hydrogen_name << ": to :" << s << ":" << std::endl;
   return s;
}



short int do_probe_dots_on_rotamers_and_chis_state() {
   return graphics_info_t::do_probe_dots_on_rotamers_and_chis_flag;
} 

void set_do_probe_dots_on_rotamers_and_chis(short int state) {
   graphics_info_t::do_probe_dots_on_rotamers_and_chis_flag = state;
}

void set_do_probe_dots_post_refine(short int state) {
   graphics_info_t::do_probe_dots_post_refine_flag = state;
} 

short int do_probe_dots_post_refine_state() {
   return graphics_info_t::do_probe_dots_post_refine_flag;
}

/* state is 1 for on and 0 for off */
void set_do_coot_probe_dots_during_refine(short int state) {
   graphics_info_t::do_coot_probe_dots_during_refine_flag = state;
}



// This is tedious and irritating to parse in C++.
// 
// a const when a member function
// 
// Note that the filenames have a trailing "/".
// 
std::vector<std::pair<std::string, std::string> > 
parse_ccp4i_defs(const std::string &filename) {

   std::vector<std::pair<std::string, std::string> > v;

   // put the current directory in, whether or not we can find the
   // ccp4 project dir
   // on Windows (without mingw or cygwin) there is no PWD,
   // so we set it to "", should work
   char *pwd = getenv("PWD");
   if (pwd) {
      v.push_back(std::pair<std::string, std::string> (std::string(" - Current Dir - "),
						       std::string(pwd) + "/"));
   } else {
#ifdef WINDOWS_MINGW
      v.push_back(std::pair<std::string, std::string> (std::string(" - Current Dir - "),
						       std::string("")));  
#endif // MINGW
   }
   
   struct stat buf;
   int stat_status = stat(filename.c_str(), &buf);
   if (stat_status != 0) {
     // silently return nothing if we can't find the file.
     return v;
   } 

   std::ifstream c_in(filename.c_str());

   // Let's also add ccp4_scratch to the list if the environment
   // variable is declared and if directory exists
   char *scratch = getenv("CCP4_SCR");
   if (scratch) {
      // struct stat buf; no shadow
      // in Windows stat needs to have a last / or \ removed, if existent
#ifdef WINDOWS_MINGW
      if (scratch[strlen(scratch) - 1] == '/') {
	scratch[strlen(scratch) - 1] = '\0';
      }
      if (scratch[strlen(scratch) - 1] == '\\') {
	scratch[strlen(scratch) - 1] = '\0';
      }
#endif // MINGW
      int istat_scratch = stat(scratch, &buf);
      if (istat_scratch == 0) {
	 if (S_ISDIR(buf.st_mode)) {
	    v.push_back(std::pair<std::string, std::string>(std::string("CCP4_SCR"),
							    std::string(scratch) + "/"));
	 }
      }
   }

   if (! c_in) {
      std::cout << "WARNING:: failed to open " << filename << std::endl;
   } else {
      // std::string s;
      char s[1000];
      std::vector <coot::alias_path_t> alias;
      std::vector <coot::alias_path_t> path;
      std::string::size_type ipath;
      std::string::size_type ialias;
      int index = -1;
      int icomma;
      short int path_coming = 0;
      short int alias_coming = 0;
      bool alias_flag = 0;
      while (! c_in.eof()) {
	 c_in >> s;
	 std::string ss(s);
	 // std::cout << "parsing:" << ss << std::endl;
	 if (path_coming == 2) {
	    path.push_back(coot::alias_path_t(index, ss, alias_flag));
	    path_coming = 0;
	 }
	 if (alias_coming == 2) {
	    alias.push_back(coot::alias_path_t(index, ss, alias_flag));
	    alias_coming = 0;
	 }
	 if ( path_coming == 1)  path_coming++;
	 if (alias_coming == 1) alias_coming++;
	 ipath  = ss.find("PROJECT_PATH,");
	 ialias = ss.find("PROJECT_ALIAS,");
	 if (ipath != std::string::npos) {
	    // std::cout << "DEBUG::  found a project path..." << std::endl;
	    path_coming = 1;
	    alias_flag = 0;
	    icomma = ss.find_last_of(",");
	    // std::cout << icomma << " " << ss.length() << std::endl;
	    if ( (icomma+1) < int(ss.length())) {
	       index = atoi(ss.substr(icomma+1, ss.length()).c_str());
	       // std::cout << "index: " << index << std::endl;
	    }
	 }
	 if (ialias != std::string::npos) {
	    alias_coming = 1;
	    alias_flag = 0;
	    icomma = ss.find_last_of(",");
	    if ( (icomma+1) < int(ss.length())) {
	       index = atoi(ss.substr(icomma+1, ss.length()).c_str());
	    }
	 }

	 // Things called ALIASES at at the CCP4 top level are
	 // actually speciified by DEF_DIR_PATH and DEF_DIR_ALIAS 
	 // in the same way that PROJECT_ALIAS and PROJECT_PATH work.
	 //

	 ipath  = ss.find("DEF_DIR_PATH,");
	 ialias = ss.find("DEF_DIR_ALIAS,");
	 if (ipath != std::string::npos) {
	    // std::cout << "DEBUG::  found an ALIAS path..." << ss << std::endl;
	    path_coming = 1;
	    alias_flag = 1;
	    icomma = ss.find_last_of(",");
	    // std::cout << icomma << " " << ss.length() << std::endl;
	    if ( (icomma+1) < int(ss.length())) {
	       index = atoi(ss.substr(icomma+1, ss.length()).c_str());
	       // std::cout << "index: " << index << std::endl;
	    }
	 }
	 if (ialias != std::string::npos) {
	    alias_coming = 1;
	    // std::cout << "DEBUG::  found an ALIAS name..." << ss << std::endl;
	    icomma = ss.find_last_of(",");
	    if ( (icomma+1) < int(ss.length())) {
	       index = atoi(ss.substr(icomma+1, ss.length()).c_str());
	    }
	 }
	 
      }

//       std::cout << "----------- path pairs: ------------" << std::endl;
//       for (int i=0; i<path.size(); i++) {
//   	 std::cout << path[i].index << "  " << path[i].s << " " << path[i].flag << std::endl;
//       }
//       std::cout << "----------- alias pairs: ------------" << std::endl;
//       for (int i=0; i<alias.size(); i++)
//   	 std::cout << alias[i].index << "  " << alias[i].s << " " << alias[i].flag << std::endl;
//       std::cout << "-------------------------------------" << std::endl;
      

      std::string alias_str;
      std::string path_str;
      for (unsigned int j=0; j<alias.size(); j++) {
	 for (unsigned int i=0; i<path.size(); i++) {
	    if (path[i].index == alias[j].index) {
	       if (path[i].flag == alias[j].flag) {
		  // check for "" "" pair here.
		  alias_str = alias[j].s;
		  path_str  = path[i].s;
		  // if the file is a directory, we need to put a "/" at the
		  // end so that went we set that filename in the fileselection
		  // widget, we go into the directory, rather than being in the
		  // directory above with the tail as the selected file.
		  //
		  struct stat buf_l;
		  int status = stat(path_str.c_str(), &buf_l);
	       
		  // valgrind says that buf.st_mode is uninitialised here
		  // strangely.  Perhaps we should first test for status?
		  // Yes - that was it.  I was using S_ISDIR() on a file
		  // that didn't exist.  Now we skip if the file does not
		  // exist or is not a directory.

		  // std::cout << "stating "<< path_str << std::endl;

		  if (status == 0) { 
		     if (S_ISDIR(buf_l.st_mode)) {
			path_str += "/";

			if (alias_str == "\"\"") {
			   alias_str = "";
			   path_str  = "";
			}
			v.push_back(std::pair<std::string, std::string> (alias_str, path_str));
		     }
		     // } else { 
		     // // This is too boring to see every time we open a file selection
		     // std::cout << "INFO:: directory for a CCP4i project: " 
		     // << path_str << " was not found\n";
		  }
	       }
	    }
	 }
      }
   }
   return v;
}

std::string
ccp4_project_directory(const std::string &ccp4_project_name) {

   std::string ccp4_defs_file_name = graphics_info_t::ccp4_defs_file_name();
   std::vector<std::pair<std::string, std::string> > v = 
      parse_ccp4i_defs(ccp4_defs_file_name);
   std::string r = "";
   for (unsigned int i=0; i<v.size(); i++) {
      if (v[i].first == ccp4_project_name) {
	 r = v[i].second;
	 break;
      }
   }
   return r;
}


/* movies */
void set_movie_file_name_prefix(const char *file_name) {
   graphics_info_t::movie_file_prefix = file_name;
}

void set_movie_frame_number(int frame_number) {
   graphics_info_t::movie_frame_number = frame_number;
}

#ifdef USE_GUILE
SCM movie_file_name_prefix() {
   SCM r = scm_makfrom0str(graphics_info_t::movie_file_prefix.c_str());
   return r;
}
#endif
#ifdef USE_PYTHON
PyObject * movie_file_name_prefix_py() {
   PyObject *r;
   r = PyString_FromString(graphics_info_t::movie_file_prefix.c_str());
   return r;
}
#endif // PYTHON

int movie_frame_number() {
   return graphics_info_t::movie_frame_number;
}

void set_make_movie_mode(int make_movie_flag) {
   graphics_info_t::make_movie_flag = make_movie_flag;
}


#ifdef USE_GUILE
void try_load_scheme_extras_dir() {

   char *s = getenv("COOT_SCHEME_EXTRAS_DIR");
   if (s) {

#if defined(WINDOWS_MINGW) || defined(_MSC_VER)
      std::vector<std::string> dirs = coot::util::split_string(s, ";");
#else
      std::vector<std::string> dirs = coot::util::split_string(s, ":");
#endif
      for (unsigned int i=0; i<dirs.size(); i++) { 
	 struct stat buf;
	 int status = stat(dirs[i].c_str(), &buf);
	 if (status != 0) {
	    std::cout << "WARNING:: no directory \"" << dirs[i] << "\""
		      << " in COOT_SCHEME_EXTRAS_DIR " << s
		      << std::endl;
	 } else {
	    if (S_ISDIR(buf.st_mode)) {

	       DIR *lib_dir = opendir(dirs[i].c_str());
	       if (lib_dir == NULL) {
		  std::cout << "An ERROR occured on opening the directory "
			    << dirs[i] << std::endl;
	       } else {

		  struct dirent *dir_ent;

		  // loop until the end of the filelist (readdir returns NULL)
		  // 
		  while (1) {
		     dir_ent = readdir(lib_dir);
		     if (dir_ent == NULL) {
			break;
		     } else {
			std::string sub_part(std::string(dir_ent->d_name));
			struct stat buf2;
			std::string fp = s;
			fp += "/";
			fp += sub_part;
			int status2 = stat(fp.c_str(), &buf2);
			if (status2 != 0) {
			   // std::cout << "WARNING:: no file " << sub_part << " in directory "
			   // << dirs[i] << std::endl;
			} else {
			   if (S_ISREG(buf2.st_mode)) {
			      if (coot::util::file_name_extension(sub_part) == ".scm") {
				 std::cout << "loading extra: " << fp << std::endl;
				 scm_c_primitive_load(fp.c_str()); 
			      }
			   }
			}
		     }
		  }
	       }
	    }
	 }
      }      
   }
}
#endif // USE_GUILE


#ifdef USE_PYTHON
void try_load_python_extras_dir() {

   char *s = getenv("COOT_PYTHON_EXTRAS_DIR");
   if (s) {
#if defined(WINDOWS_MINGW) || defined(_MSC_VER)
      std::vector<std::string> dirs = coot::util::split_string(s, ";");
#else
      std::vector<std::string> dirs = coot::util::split_string(s, ":");
#endif
      for (unsigned int i=0; i<dirs.size(); i++) { 
	 struct stat buf;
	 int status = stat(dirs[i].c_str(), &buf);
	 if (status != 0) {
	    std::cout << "WARNING:: no directory \"" << dirs[i] << "\""
		      << " in COOT_PYTHON_EXTRAS_DIR " << s
		      << std::endl;
	 } else {
	    if (S_ISDIR(buf.st_mode)) {

	       DIR *lib_dir = opendir(dirs[i].c_str());
	       if (lib_dir == NULL) {
		  std::cout << "An ERROR occured on opening the directory "
			    << dirs[i] << std::endl;
	       } else {

		  struct dirent *dir_ent;

		  // loop until the end of the filelist (readdir returns NULL)
		  // 
		  while (1) {
		     dir_ent = readdir(lib_dir);
		     if (dir_ent == NULL) {
			break;
		     } else {
			std::string sub_part(std::string(dir_ent->d_name));
			struct stat buf2;
			std::string fp = s;
			fp += "/";
			fp += sub_part;
			int status2 = stat(fp.c_str(), &buf2);
			if (status2 != 0) {
			   // std::cout << "WARNING:: no file " << sub_part << std::endl;
			} else {
			   if (S_ISREG(buf2.st_mode)) {
			      if (coot::util::file_name_extension(sub_part) == ".py") {
				 std::cout << "loading python extra: " << fp << std::endl;
				 run_python_script(fp.c_str()); 
			      }
			   }
			}
		     }
		  }
	       }
	    }
	 }
      }      
   }
}
#endif // USE_PYTHON


void set_button_label_for_external_refinement(const char *button_label) {
   graphics_info_t::external_refinement_program_button_label = button_label;
}


int preferences_internal_font_own_colour_flag() {

   int r = -1; 
   graphics_info_t g;
   for (unsigned int i=0; i<g.preferences_internal.size(); i++) {
      if (g.preferences_internal[i].preference_type == PREFERENCES_FONT_OWN_COLOUR_FLAG) {
	 r = g.preferences_internal[i].ivalue1;
	 break;
      }
   }

   return r;
} 
