# Bandicoot — Outstanding Issues

Working tracker for known bugs / rough edges, roughly prioritized. This is
not a changelog and not a feature backlog — just things that are wrong and
worth fixing. Newest info at the top of each entry.

**Priority key**
- **P1** — correctness / data loss / crash / blocks a workflow
- **P2** — visible bug, but a workaround exists
- **P3** — cosmetic / minor / niche

---

## P2 — visible bug, workaround exists

### GitHub #28 — Side toolbar style is not preserved between sessions
Reported by alyubimov, 2026-08-28. Every other preference persists in
`~/.coot-preferences/coot_preferences.py`, but the model ("side") toolbar always comes back
as **Icons and Text**, whatever was last chosen from the sidebar's settings popup.

- **Root cause: the style state and the widget are wired in one direction only.**
  `set_model_toolbar_style()` (`src/c-interface-gui.cc:3162`) writes
  `graphics_info_t::model_toolbar_style_state` and then activates the matching check menu
  item, whose handler applies the style to the toolbar. But when a style is chosen directly
  from the sidebar popup, those handlers — `on_model_toolbar_icons_and_text1_activate`
  (`src/callbacks.c:10502`), `on_model_toolbar_icons1_activate` (`:10520`) and
  `on_model_toolbar_text1_activate` (`:10537`) — call `gtk_toolbar_set_style()` and **never
  write back** to `model_toolbar_style_state`. There is no widget → state path.
- **Why it is always "Icons and Text" specifically:** the state variable keeps its
  compiled-in default of `2` (`src/globjects.cc:235`), the preference writer stores that
  value (`src/graphics-info-preferences.cc:453`), and startup replays
  `set-model-toolbar-style 2`. The saved setting is therefore the default, never the choice.
- **Workaround:** set the style from **Preferences > Refinement Toolbar** rather than the
  sidebar popup. That path goes through `set_model_toolbar_style()`
  (`src/callbacks.c:7872/7885/7898`), so it updates the state and does persist.
- Inherited from Coot 0.9 rather than introduced here, but more visible in Bandicoot because
  the sidebar's own settings popup is the natural place to change the style.

---

## P3 — cosmetic / minor

### GitHub #15 — Model toolbar cannot be repositioned (partially addressed)
Reported by alyubimov, 2026-08-06. Coot 0.9 lets the docked model toolbar sit on any
edge of the rendering window; in Bandicoot it is fixed in place (on the **right**, not
the left as the issue text says — `bandicoot_reposition_sidebar()` pins it to
`NSMaxX(usable) - width`).

- **Root cause:** repositioning works by reparenting the legacy
  `model_fit_refine_toolbar_handlebox` between the main window's frames. Bandicoot lifts
  the `GtkToolbar` OUT of that handlebox into the native pinned sidebar
  (`gtk_widget_reparent` in `bandicoot_sidebar_install`, `src/bandicoot_appkit.mm`), so
  every position control moves an empty container between hidden frames.
- **DONE 2026-08-06:** the inert right-click popup is removed — right-click on the
  toolbar now does nothing (`on_model_toolbar_button_press_event`). `toolbar_popup_menu()`
  and `set_model_toolbar_docked_position_callback()` were deleted; the popup also cast the
  toolbar's parent to `GtkHandleBox` when it is a `GtkVBox`. Dock/undock and toolbar style
  remain on the sidebar's own settings popup.
- **STILL OPEN — the same dead UI in Preferences.** `Preferences > Refinement Toolbar`
  still offers a Model Toolbar Position radio group (`src/callbacks.c:7765-7800`) calling
  the same `set_model_toolbar_docked_position()`, and it is equally inert. **Decision
  (2026-08-06): leave it.** If a user reports it we explain or remove it then; the wx
  interface rework may land first, and it is unclear how many users move the toolbar at
  all.
- **Actual repositioning is deferred to the wx interface work.** Supporting left/right on
  the native sidebar is feasible (a persisted side + x computed in
  `bandicoot_reposition_sidebar()`), but the A/R bar and docked sequence view both inset
  themselves from the sidebar's edge, so it is three pinned windows moving together.
  Top/bottom would additionally need the toolbar re-oriented and would compete with the
  sequence view for the top edge.

### Ball & Stick aromatic-ring toruses not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
Aromatic-ring torus annotations in the Ball & Stick representation don't draw.
