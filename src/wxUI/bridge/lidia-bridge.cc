// src/wxUI/bridge/lidia-bridge.cc

#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems

#include "lbg/lbg.hh" // early, as elsewhere, for RDKit's sake

#include "wxUI/bridge/lidia-bridge.hh"
#include "lbg-interface.hh"

#include <cstring>
#include <iostream>

extern "C" void start_ligand_builder_gui(); // c-interface-ligands-gui.cc

namespace {

bandicoot_lidia_hooks hooks;

lbg_info_t *builder(bandicoot_lidia_id id) {
   return static_cast<lbg_info_t *>(id);
}

void host_open(lbg_info_t *l) {
   if (hooks.open) hooks.open(l);
}

void host_status_text(lbg_info_t *l, const std::string &text) {
   if (hooks.status_text) hooks.status_text(l, text);
}

void host_alert(lbg_info_t *l, bool shown, const std::string &name) {
   if (hooks.alert) hooks.alert(l, shown, name);
}

void host_apply_sensitive(lbg_info_t *l, bool sensitive) {
   if (hooks.apply_sensitive) hooks.apply_sensitive(l, sensitive);
}

void host_atom_x_dialog(lbg_info_t *l, const std::string &element) {
   if (hooks.atom_x_dialog) hooks.atom_x_dialog(l, element);
}

void host_error(lbg_info_t *l, const std::string &what_failed, const std::string &subject) {
   if (hooks.error) hooks.error(l, what_failed, subject);
}

lbg_host_t host = { host_open, host_status_text, host_alert, host_apply_sensitive,
                    host_atom_x_dialog, host_error };

int addition_mode(bandicoot_lidia_tool tool) {
   switch (tool) {
   case bandicoot_lidia_tool::ring_3:          return lbg_info_t::TRIANGLE;
   case bandicoot_lidia_tool::ring_4:          return lbg_info_t::SQUARE;
   case bandicoot_lidia_tool::ring_5:          return lbg_info_t::PENTAGON;
   case bandicoot_lidia_tool::ring_6:          return lbg_info_t::HEXAGON;
   case bandicoot_lidia_tool::ring_6_aromatic: return lbg_info_t::HEXAGON_AROMATIC;
   case bandicoot_lidia_tool::ring_7:          return lbg_info_t::HEPTAGON;
   case bandicoot_lidia_tool::ring_8:          return lbg_info_t::OCTAGON;
   case bandicoot_lidia_tool::carbon:          return lbg_info_t::ATOM_C;
   case bandicoot_lidia_tool::nitrogen:        return lbg_info_t::ATOM_N;
   case bandicoot_lidia_tool::oxygen:          return lbg_info_t::ATOM_O;
   case bandicoot_lidia_tool::hydrogen:        return lbg_info_t::ATOM_H;
   case bandicoot_lidia_tool::sulfur:          return lbg_info_t::ATOM_S;
   case bandicoot_lidia_tool::phosphorus:      return lbg_info_t::ATOM_P;
   case bandicoot_lidia_tool::fluorine:        return lbg_info_t::ATOM_F;
   case bandicoot_lidia_tool::chlorine:        return lbg_info_t::ATOM_CL;
   case bandicoot_lidia_tool::iodine:          return lbg_info_t::ATOM_I;
   case bandicoot_lidia_tool::bromine:         return lbg_info_t::ATOM_BR;
   case bandicoot_lidia_tool::other_element:   return lbg_info_t::ATOM_X;
   case bandicoot_lidia_tool::charge:          return lbg_info_t::CHARGE;
   case bandicoot_lidia_tool::single_bond:     return lbg_info_t::ADD_SINGLE_BOND;
   case bandicoot_lidia_tool::double_bond:     return lbg_info_t::ADD_DOUBLE_BOND;
   case bandicoot_lidia_tool::triple_bond:     return lbg_info_t::ADD_TRIPLE_BOND;
   case bandicoot_lidia_tool::stereo_bond:     return lbg_info_t::ADD_STEREO_OUT_BOND;
   case bandicoot_lidia_tool::cut:             return lbg_info_t::DELETE_MODE;
   case bandicoot_lidia_tool::none:            break;
   }
   return lbg_info_t::NONE;
}

} // namespace

void bandicoot_lidia_install(const bandicoot_lidia_hooks &h) {
   hooks = h;
   lbg_info_t::host = &host;
}

void bandicoot_lidia_start() {
   start_ligand_builder_gui();
}

void bandicoot_lidia_set_tool(bandicoot_lidia_id id, bandicoot_lidia_tool tool) {
   lbg_info_t *l = builder(id);
   l->canvas_addition_mode = addition_mode(tool);
   l->set_in_delete_mode(tool == bandicoot_lidia_tool::cut);
   if (tool == bandicoot_lidia_tool::other_element)
      l->show_atom_x_dialog();
}

void bandicoot_lidia_canvas_size(bandicoot_lidia_id id, double *width, double *height) {
   gdouble left = 0, top = 0, right = 0, bottom = 0;
   goo_canvas_get_bounds(GOO_CANVAS(builder(id)->canvas), &left, &top, &right, &bottom);
   *width = right - left;
   *height = bottom - top;
}

void bandicoot_lidia_render(bandicoot_lidia_id id, int width, int height,
                            double scale, double x0, double y0,
                            std::vector<unsigned char> *rgb) {

   rgb->assign(size_t(width) * height * 3, 255);
   if (width <= 0 || height <= 0) return;

   cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
   cairo_t *cr = cairo_create(surface);
   cairo_set_source_rgb(cr, 1, 1, 1);
   cairo_paint(cr);
   cairo_scale(cr, scale, scale);
   cairo_translate(cr, -x0, -y0);
   GooCanvasBounds shown = { x0, y0, x0 + width / scale, y0 + height / scale };
   goo_canvas_render(GOO_CANVAS(builder(id)->canvas), cr, &shown, scale);
   cairo_destroy(cr);
   cairo_surface_flush(surface);

   // RGB24 keeps each pixel in a native-endian 32-bit word, red in the high byte
   const unsigned char *data = cairo_image_surface_get_data(surface);
   int stride = cairo_image_surface_get_stride(surface);
   for (int y = 0; y < height; y++) {
      const uint32_t *row = reinterpret_cast<const uint32_t *>(data + size_t(y) * stride);
      unsigned char *out = &(*rgb)[size_t(y) * width * 3];
      for (int x = 0; x < width; x++) {
         uint32_t p = row[x];
         out[3 * x]     = (p >> 16) & 0xff;
         out[3 * x + 1] = (p >> 8) & 0xff;
         out[3 * x + 2] = p & 0xff;
      }
   }
   cairo_surface_destroy(surface);
}

void bandicoot_lidia_pointer(bandicoot_lidia_id id, bandicoot_lidia_mouse what, int button,
                             double x, double y, bool shift, bool control, bool button_1_down) {

   lbg_info_t *l = builder(id);
   GooCanvas *canvas = GOO_CANVAS(l->canvas);
   GooCanvasItem *root = goo_canvas_get_root_item(canvas);
   GooCanvasItem *target = goo_canvas_get_item_at(canvas, x, y, TRUE);

   guint state = 0;
   if (shift)         state |= GDK_SHIFT_MASK;
   if (control)       state |= GDK_CONTROL_MASK;
   if (button_1_down) state |= GDK_BUTTON1_MASK;

   gboolean handled = FALSE;
   if (what == bandicoot_lidia_mouse::motion) {
      GdkEventMotion event;
      std::memset(&event, 0, sizeof(event));
      event.type = GDK_MOTION_NOTIFY;
      event.x = x;
      event.y = y;
      event.state = state;
      g_signal_emit_by_name(root, "motion_notify_event", target, &event, &handled);
   } else {
      GdkEventButton event;
      std::memset(&event, 0, sizeof(event));
      event.x = x;
      event.y = y;
      event.state = state;
      event.button = button;
      if (what == bandicoot_lidia_mouse::release) {
         event.type = GDK_BUTTON_RELEASE;
         g_signal_emit_by_name(root, "button_release_event", target, &event, &handled);
      } else {
         // as GTK: the second press of a double click comes as a plain press
         // and then as a double press
         event.type = GDK_BUTTON_PRESS;
         g_signal_emit_by_name(root, "button_press_event", target, &event, &handled);
         if (what == bandicoot_lidia_mouse::double_press) {
            event.type = GDK_2BUTTON_PRESS;
            target = goo_canvas_get_item_at(canvas, x, y, TRUE);
            g_signal_emit_by_name(root, "button_press_event", target, &event, &handled);
         }
      }
   }
}

void bandicoot_lidia_clear(bandicoot_lidia_id id) {
   builder(id)->clear(true);
}

void bandicoot_lidia_undo(bandicoot_lidia_id id) {
   builder(id)->undo();
}

void bandicoot_lidia_delete_hydrogens(bandicoot_lidia_id id) {
   builder(id)->delete_hydrogens();
}

void bandicoot_lidia_tidy_up(bandicoot_lidia_id id) {
#ifdef MAKE_ENHANCED_LIGAND_TOOLS
   builder(id)->clean_up_2d_representation();
#endif
}

std::string bandicoot_lidia_smiles(bandicoot_lidia_id id) {
   try {
      return builder(id)->get_smiles_string_from_mol();
   }
   catch (const std::exception &e) {
      std::cout << "WARNING:: no SMILES for the ligand builder's molecule: " << e.what()
                << std::endl;
   }
   return std::string();
}

void bandicoot_lidia_set_atom_x(bandicoot_lidia_id id, const std::string &element) {
   builder(id)->set_atom_x_string(element);
}

void bandicoot_lidia_show_alerts(bandicoot_lidia_id id, bool shown) {
   lbg_info_t *l = builder(id);
   l->show_alerts_user_control = shown;
   if (shown) {
      l->update_descriptor_attributes();
   } else {
      l->show_alert(false, "");
      l->clear_canvas_alerts();
   }
}

void bandicoot_lidia_show_environment(bandicoot_lidia_id id, bool shown) {
   lbg_info_t *l = builder(id);
   l->set_draw_flev_annotations(shown);
   if (shown) {
      l->draw_all_flev_annotations();
   } else {
      lig_build::pos_t zero_delta(0, 0);
      l->clear_and_redraw(zero_delta);
   }
}

void bandicoot_lidia_show_key(bandicoot_lidia_id id, bool shown) {
   if (shown)
      builder(id)->show_key();
   else
      builder(id)->hide_key();
}

void bandicoot_lidia_set_labels(bandicoot_lidia_id id, bandicoot_lidia_labels labels) {
   lbg_info_t *l = builder(id);
   l->set_display_atom_numbers(labels == bandicoot_lidia_labels::indices);
   l->set_display_atom_names(labels == bandicoot_lidia_labels::names);
   l->clear_and_redraw();
}

void bandicoot_lidia_new(bandicoot_lidia_id id) {
   builder(id)->new_lbg_window();
}

void bandicoot_lidia_import_smiles(bandicoot_lidia_id id, const std::string &smiles) {
   builder(id)->import_mol_from_smiles_string(smiles);
}

void bandicoot_lidia_import_comp_id(bandicoot_lidia_id id, const std::string &comp_id,
                                    bool keep_hydrogens) {
   builder(id)->import_mol_from_comp_id(comp_id, keep_hydrogens);
}

void bandicoot_lidia_open_file(bandicoot_lidia_id id, const std::string &path) {
   builder(id)->import_molecule_from_file(path);
}

void bandicoot_lidia_save(bandicoot_lidia_id id) {
   builder(id)->write_mdl_molfile_using_default_file_name();
}

void bandicoot_lidia_save_as(bandicoot_lidia_id id, const std::string &path) {
   lbg_info_t *l = builder(id);
   l->set_default_mdl_file_name(path);
   l->mol.write_mdl_molfile(path);
}

void bandicoot_lidia_export_as(bandicoot_lidia_id id, bandicoot_lidia_export format,
                               const std::string &path) {
   lbg_info_t *l = builder(id);
   switch (format) {
   case bandicoot_lidia_export::pdf: l->write_pdf(path); break;
   case bandicoot_lidia_export::png: l->write_png(path); break;
   case bandicoot_lidia_export::svg: l->write_svg(path); break;
   }
}

void bandicoot_lidia_fetch(bandicoot_lidia_id id, const std::string &name) {
   builder(id)->get_drug(name);
}

void bandicoot_lidia_apply(bandicoot_lidia_id id) {
   // as Lidia's own Apply button
   lbg_info_t *l = builder(id);
   l->mol.write_mdl_molfile("coot-lidia.mdl");
   l->import_prodrg_output("coot-lidia.mdl", l->get_comp_id());
}

void bandicoot_lidia_smiles_to_2d(const std::string &smiles) {
   smiles_to_ligand_builder(smiles.c_str());
}

void bandicoot_lidia_close(bandicoot_lidia_id id) {
   lbg_info_t *l = builder(id);
   g_object_unref(l->canvas);
   delete l;
}
