# Bandicoot Keyboard Shortcuts

Single keys act on the atom or residue under the pointer in the graphics
window. `Ctrl` combinations are held together. Shifted letters (e.g. `Shift+R`)
are shown as the capital letter.

The model-building shortcuts are installed automatically on startup. To
customise them, edit your personal copy at `~/.coot-preferences/template_key_bindings.py`
(created via **Settings → Python: Install Template Keybindings**); your edits
override the built-in defaults.

## Navigation & view

| Key | Function |
|-----|----------|
| `U` | Undo last navigation move |
| `N` / `M` | Zoom in / zoom out |
| `D` / `F` | Decrease / increase clipping (depth of field) |
| `I` | Toggle spin |
| `Ctrl+R` | Toggle rock |
| `L` | Label closest atom |
| `O` / `Shift+O` | Next / previous NCS chain |
| `P` | Update Go To Atom to the closest atom |
| `V` | Undo symmetry view |
| `Keypad ↑ ↓ / PgUp / PgDn` | Translate along the screen Z axis |
| `.` / `,` | Next / previous (rotamer, difference-map peak, or water) |

## Refinement & fitting

| Key | Function |
|-----|----------|
| `R` | Refine active residue |
| `X` | Refine active residue (auto-accept) |
| `T` | Triple refine |
| `H` | Refine active residue and its neighbours |
| `Shift+R` | Refine residues within a 4.5 Å sphere |
| `Shift+B` | Regularize residues within a sphere |
| `A` | Refine auto-zone (while picking a refinement range) |
| `J` | Autofit rotamer |
| `Shift+J` | Jiggle-fit residue |
| `Q` | Pepflip |
| `Shift+F` / `Shift+G` | JED-flip / reverse JED-flip |
| `E` | Eigen-flip ligand |
| `Return` | Accept refinement / regularization |
| `Esc` | Reject refinement (stop and clear moving atoms) |

## Model editing

| Key | Function |
|-----|----------|
| `W` | Add water at the pointer |
| `Shift+W` | Add water, then centre and refine |
| `Y` | Add terminal residue |
| `K` | Fill partial residue |
| `Shift+K` | Delete residue sidechain |
| `Shift+P` | Delete residue hydrogens |
| `Ctrl+D` / `Ctrl+X` | Delete active residue |
| `Shift+X` | Edit chi angles |
| `Shift+Q` | Rotamers dialog for the active residue |
| `~` | Show rotamer name in the status bar |
| `Shift+A` | Accept baton position |
| `Shift+N` | Cootilus here (find nucleic acids nearby) |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo model edit |
| `$` | Ball-and-stick representation for the residue |

## Maps

| Key | Function |
|-----|----------|
| `Shift+Y` | Cycle displayed map (just one / next) |
| `Shift+M` | Step the scrollable map number |
| `G` | Go to blob under the pointer |

## Dialogs & windows

| Key | Function |
|-----|----------|
| `Ctrl+G` | Go To Residue (keyboard entry) |
| `F5` | Model / Fit / Refine dialog |
| `F6` | Go To Atom window |
| `F7` | Display Manager |
| `F8` | Save screenshot |
| `Ctrl+S` | Quick save |
| `Ctrl+1` | All-atom contact dots for the active molecule |
