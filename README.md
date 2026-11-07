# Nibform

A native, pen-first CAD app for Linux (target: the Framework 12 with a stylus).
C++20 · Qt 6 · OpenCASCADE 7.9 · Eigen.

**Website:** https://vilkasek.github.io/nibform/ (from `docs/`: GitHub Pages, *Deploy from a branch*, `main` / `/docs`).
The logo is `docs/assets/logo.svg`; the window icon `src/app/nibform.png` is drawn from it
(`rsvg-convert -w 256 -h 256 docs/assets/logo.svg -o src/app/nibform.png`).

## Building

```sh
sudo pacman -S --needed qt6-base qt6-tools opencascade eigen cmake ninja
cmake -S . -B build -G Ninja
cmake --build build
./build/nibform
```

Arch's OpenCASCADE is built against GLX, so the app forces `QT_QPA_PLATFORM=xcb` itself
(on Wayland it runs through XWayland). The OpenGL compatibility profile is used by default
(in core, Mesa limits line width to 1 px); for the core profile: `NIBFORM_GL_CORE=1`.
`NIBFORM_SCREENSHOT=file.png` renders one frame, saves it and quits.
`NIBFORM_DEBUG_INPUT=1` prints the input devices and scroll/gesture events,
`NIBFORM_INVERT_SCROLL=1` inverts the touchpad's orbit/pan direction.

Core tests: `ctest --test-dir build` (or `./build/test_sketch`, `./build/test_solver`, `./build/test_trim`, `./build/test_offset`, `./build/test_model`, `./build/test_features`, `./build/test_project`, `./build/test_input`, `./build/test_value_editor`).

## Language

The interface is in English or Polish: *View → Język / Language* offers *System* (Polish when the
desktop is Polish, English otherwise), *Polski* and *English*. The choice is remembered and applies
right away, without a restart; standard Qt dialogs and the decimal separator follow it.

The sources are in English; the Polish text lives in `translations/nibform_pl.ts` (Qt Linguist),
built into the app. After changing user-facing strings:

```sh
cmake --build build --target update_translations   # lupdate: new / changed strings into the .ts
python3 translations/fill_pl.py                    # fills in the Polish from its dictionary
```

`fill_pl.py` lists the strings it has no translation for (add them to its dictionary, or
translate them in Linguist).

## Two modes: Model and Sketch

The app starts in **Model** mode (the "MODEL" badge): bodies, closed sketch regions, extrude,
fillet and chamfer. **New Sketch** (`K`) opens a new sketch – on the selected planar face of a
body (its edges come into the sketch as locked construction geometry), or on the ground without
one; the arrow next to the button picks a plane (XY / XZ / YZ). The view then turns to face the
sketch and the badge changes to "SKETCH", with drawing tools and constraints. **✓ Finish Sketch**
(`Ctrl+Enter`) goes back to the bodies (an empty sketch disappears). **Edit Sketch** reopens the
sketch whose region is selected. Shortcuts only work in their own mode.

## Sketching

| Tool | Shortcut | How |
|---|---|---|
| Select | `S` | tap an item, Shift adds; `Delete` removes |
| Line | `L` | tap-tap draws a polyline (ends on an existing point or with `Esc`/`Enter`); a drag = a single segment |
| Rectangle | `R` | two opposite corners |
| Circle | `C` | center, then a point on the circumference |
| Arc | `A` | start, end, a point on the arc |
| Offset | `U` | tap a curve – takes the whole connected chain (or circle); the copy follows the cursor (side and distance), another tap asks for an exact value. Corners are intersected again, lines get "parallel", arcs share the center with the original |
| Construction | `Q` | toggles the selected curves to helper geometry (dashed): you can snap to and dimension them, but they don't make regions |
| Trim | `X` | like in Fusion: highlights in red the piece between the nearest intersections, a tap removes it; a drag removes everything along the way (one undo step). A curve without intersections disappears whole, a circle turns into an arc |

Snapping: points → origin → segment midpoints → horizontal/vertical alignment
(first with the tool's start point). `Alt` disables snapping.
Snapping leaves constraints behind: an aligned line gets "horizontal"/"vertical", a rectangle both,
a point at a segment's middle "midpoint", a point at the origin "fix".
`Esc` cancels the operation, a second `Esc` goes back to Select. `Ctrl+Z` / `Ctrl+Shift+Z` undo/redo.
The grid lies on the plane of the open sketch (in Model mode, on the ground).

## Constraints and dimensions

In Select, tap points, lines and arcs – every tap adds an item to the selection, tapping empty
space clears it. Then pick a constraint from the second toolbar (or the *Constraints* menu);
buttons that don't fit the selection are greyed out.

| Constraint | Shortcut | Selection |
|---|---|---|
| Dimension | `D` | line / 2 points → distance, point + line → distance from the line, 2 parallel lines → spacing (adds "parallel" if needed), 2 other lines → angle, circle → diameter, arc → radius |
| Horizontal / Vertical | `H` / `V` | lines or 2 points |
| Parallel / Perpendicular | `P` / `N` | 2+ / 2 lines |
| Equal | `E` | 2+ lines (length) or 2+ circles/arcs (radius) |
| Tangent | `T` | a line and a circle/arc, or 2 circles/arcs |
| Concentric | `O` | 2+ circles/arcs |
| Coincident | `I` | 2 points (merges them) or a point and a curve |
| Midpoint | `M` | a point and a line |
| Fix | `Z` | points or curves (pins them where they are) |

Or the other way round: **`D` with nothing selected** (or the *Dimension* button) is a tool – tap
what to dimension. A line, circle or arc asks for its value right away; a point waits for a second
point or a line. While the field is open, tapping another point or line turns the dimension into
one between them (a line's length into the gap to a second line, or the angle between them).
Tapping a dimension changes its value, `Esc` goes back to Select.

Tapping a selected dimension, double-clicking it or `Enter` opens a value field by its label:
`Enter` or ✓ accepts, `Esc` or ✕ cancels (both comma and point work as the decimal separator).
Dragging a point or a curve moves the geometry while the solver keeps the constraints. Fully
defined geometry turns green, and the status bar shows the remaining degrees of freedom.
Conflicting or redundant constraints are rejected with a message. Constraint glyphs and dimension
labels can be selected and deleted (`Delete`).

The solver (`src/sketch/Solver.cpp`, Eigen only): Gauss-Newton with a minimum-norm step,
so the geometry moves as little as possible on every change; degrees of freedom and "fully defined"
items come from the Jacobian's null space (SVD).

## Bodies

Closed sketch regions (also with holes, split by lines, bounded by arcs) are lightly filled.
**Tapping a region** (in Select), or a region or a planar face in the **Extrude** tool (`W`),
shows an arrow and a distance field with a preview of the body: drag the arrow or type a value
(a negative one flips the direction; the preview keeps up with typing); further taps add
regions/faces. `Enter` or ✓ extrudes, `Esc` or ✕ cancels.

| Tool | Shortcut | How |
|---|---|---|
| Extrude | `W` | sketch regions and planar body faces (push/pull) |
| Fillet | `G` | tap body edges (tapping a face takes all its edges), drag the handle into the material or type the radius – previewed right away; `Enter` accepts |
| Chamfer | `B` | as above, with the chamfer distance |
| Shell | `J` | tap the planar faces to open, drag the handle inwards or type the wall thickness – the body is hollowed out (the walls grow inwards) |

In Select, body faces and edges can be picked anywhere (the edge wins when the pen is close
to it). With edges selected, a bar with *Fillet* / *Chamfer* appears next to them; with one planar
face, *Sketch on Face* / *Extrude*; the tool takes over the selection.

What extruding does with existing bodies is chosen under the value field (the preview turns red
for a cut), or in the *Operation* box on the toolbar – they are the same setting:

| Mode | Effect |
|---|---|
| Automatic | a body face: pulling out adds, pushing in removes; a sketch region: starting inside a body – cuts (e.g. a sketch on a face pushed into it, even right through and out the other side, or a circle across the face's edge for a half-round notch), starting outside but reaching or touching a body – joins, away from bodies – a new body. The field shows "Automatic: cut" when it will cut |
| New Body / Join / Cut | forces that operation |

Editing an extrusion from the history offers the same choice.

Regions that have been extruded are hidden in the model, so they don't cover the faces they lie on
(those can then be picked); *View → Show Extruded Regions* brings them back, e.g. to extrude one again.
An extrusion that failed leaves its region in sight.

The *Bodies* menu unions, subtracts (from the first selected) or intersects the bodies whose faces
are selected. `Delete` removes bodies with selected faces and sketches with selected regions.
Undo covers sketches and bodies together.
`NIBFORM_DEMO=1` builds a sample model (`NIBFORM_DEMO=sketch` also opens a sketch) –
handy with `NIBFORM_SCREENSHOT`.

## History, projects, export

The model is parametric: bodies aren't saved but rebuilt from the history of operations
(sketch, extrude, fillet, chamfer, body operations, delete). The **History** bar at the bottom
shows them in order. A tap selects an entry, a **double-click** (double tap) edits it, and the
arrow next to it offers *Edit* / *Delete*:

- a sketch reopens, the camera smoothly turns to face it, and the model goes back to the moment
  it was made – the bodies built from it (and everything later, dimmed on the bar) disappear
  until the sketch is finished, then rebuild by themselves;
- an operation with a value opens the value field with a live preview of the whole model (`Esc`
  restores it); the camera frames the model and, when it looks exactly along or across the
  operation's direction, switches to an isometric view. An operation whose input is gone (e.g. a
  region after a sketch change) is shown in red with an explanation in the hint, and is skipped.

Operation inputs are remembered geometrically so they can be found after a rebuild: a region by a
point inside it, a face by its middle and normal, an edge by its middle and direction, a body by a
point inside it.

**File**: *New* (`Ctrl+N`), *Open…* (`Ctrl+O`), *Save* (`Ctrl+S`), *Save As…*
(`Ctrl+Shift+S`) – a `.nibform` project is readable JSON with the sketches and the history;
`nibform file.nibform` opens one right away. The window title shows `*` for unsaved changes,
and closing asks to save. *Export STEP…* / *Export STL…* save the bodies (binary STL, 0.05 mm mesh).

## View controls

| Input | Action |
|---|---|
| Pen (tip) | the active sketch tool |
| Pen + button | orbit |
| 1 finger | orbit |
| 2 fingers | pan + pinch zoom |
| Mouse LMB / RMB / MMB / wheel | sketch tool / orbit / pan / zoom |
| Shift+RMB, Ctrl+RMB | pan, zoom |
| Touchpad: 2-finger scroll | orbit (a TrackPoint with the middle button too) |
| Touchpad: Shift + 2 fingers / 3 fingers | pan |
| Touchpad: pinch, Ctrl + 2 fingers | zoom |
| `F` | fit the view |
| `1`–`6` | faces: top, front, right, back, left, bottom |
| `Ctrl+1`–`8` | corners: top (front-right, back-right, back-left, front-left), then bottom in the same order; `Ctrl+1` = isometric |
| `Shift+1`–`0` | edges: top (front, right, back, left), vertical (front-right, back-right, back-left, front-left), bottom-front, bottom-back |
| `Ctrl+Shift+9` / `0` | edges bottom-right / bottom-left |
| Corner cube | click a face, edge or corner |

All views are also in the *View* menu; changing the view (by shortcut, button or the cube) is a
smooth camera animation. The *Theme* is there too: System (follows the desktop's light/dark mode,
read through xdg-desktop-portal), Light or Dark; the choice is remembered.

## Pen, touch, tablet (Framework 12)

- **Palm rejection** (*View → Palm Rejection*, on by default): while the pen is near the screen
  (hovering, drawing or just drawn), touch is ignored and a touch gesture in progress is
  cancelled; wide contacts (the side of the palm) are always ignored.
- **The pen's eraser** in a sketch works like *Trim*: it removes the pieces it passes over
  (the whole stroke is one undo step), whatever tool is selected.
- **Tablet mode** (*View → Tablet Mode*, `Ctrl+T`, remembered): bigger buttons and hit areas,
  a side bar with *Undo / Redo / Delete / Cancel / Done / Fit* (instead of keys)
  and a numeric keypad in the value field.
- **Screen rotation**: in a narrow (portrait) window the bars show icons only; after the
  orientation changes, the model is framed again. `NIBFORM_WINDOW_SIZE=800x1280` tries it on a
  regular screen.

## Plan

- [x] **M0** – Qt window + OCCT view, mouse/touchpad/touch/pen navigation
- [x] **M1** – sketching on a plane with the pen (line, rectangle, circle, arc), snapping
- [x] **M2** – constraints and dimensions, solver
- [x] **M3** – profile detection, push/pull, booleans
- [x] **M4** – parametric history, project files, STEP/STL export
- [x] **M5** – sketch on face, fillet/chamfer/shell
- [x] **M6** – Framework 12 polish (screen rotation, palm rejection, tablet mode)
- [x] **i18n** – English UI with a Polish translation, language switching
