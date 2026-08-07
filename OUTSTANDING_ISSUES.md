# Bandicoot — Outstanding Issues

Working tracker for known bugs / rough edges, roughly prioritized. This is
not a changelog and not a feature backlog — just things that are wrong and
worth fixing. Newest info at the top of each entry.

**Priority key**
- **P1** — correctness / data loss / crash / blocks a workflow
- **P2** — visible bug, but a workaround exists
- **P3** — cosmetic / minor / niche

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
  (Art, 2026-08-06): leave it.** If a user reports it we explain or remove it then; the wx
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
