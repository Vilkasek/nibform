# Nibform

A native, pen-first CAD app for Linux (target: the Framework 12 with a stylus).
C++20 · Qt 6 · OpenCASCADE 7.9 · Eigen.

**Website:** https://vilkasek.github.io/nibform/ (from `docs/`: GitHub Pages, *Deploy from a branch*, `main` / `/docs`).
The logo is `docs/assets/logo.svg`; the window icon `src/app/nibform.png` is drawn from it
(`rsvg-convert -w 256 -h 256 docs/assets/logo.svg -o src/app/nibform.png`).

## Installing

On Arch Linux, from the AUR: `yay -S nibform` (the `PKGBUILD` is in `packaging/aur`).
A Flatpak manifest is in `packaging/flatpak`:
`flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.vilkasek.Nibform.yml`.

Either way, or `sudo cmake --install build` after building, Nibform gets a menu entry and an
icon, and `.nibform` projects open from the file manager (MIME type
`application/x-nibform-project`; the app id is `io.github.vilkasek.Nibform`).

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

`NIBFORM_AUTOSAVE_SECONDS=5` autosaves more often than every minute.
`-DNIBFORM_BUILD_TESTS=OFF` skips the tests (and Qt Test).

Core tests: `ctest --test-dir build` (or `./build/test_sketch`, `./build/test_solver`, `./build/test_trim`, `./build/test_offset`, `./build/test_model`, `./build/test_features`, `./build/test_project`, `./build/test_input`, `./build/test_value_editor`, `./build/test_toolkit`, `./build/test_freehand`, `./build/test_solid_features`).

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
| Freehand | `P` | draw it roughly in one stroke and get the clean shape: a line, corners (a polyline or a closed polygon), a rectangle (turned or not), a circle or an arc. Ends and corners join points nearby, nearly level / upright sides become horizontal / vertical, a turned rectangle keeps its right angles. A scribble deletes what it covers (curves, dimensions, relations); a tap selects as in Select |
| Offset | `U` | tap a curve – takes the whole connected chain (or circle); the copy follows the cursor (side and distance), another tap asks for an exact value. Corners are intersected again, lines get "parallel", arcs share the center with the original |
| Construction | `Q` | toggles the selected curves to helper geometry (dashed): you can snap to and dimension them, but they don't make regions |
| Trim | `X` | like in Fusion: highlights in red the piece between the nearest intersections, a tap removes it; a drag removes everything along the way (one undo step). A curve without intersections disappears whole, a circle turns into an arc |
| Polygon | `Shift+P` | center, then a corner; asks for the number of sides (remembered). A regular polygon inscribed in a construction circle: corners on it, sides equal |
| Slot | `Shift+S` | the centers of both round ends, then the width: two lines tangent to two equal arcs, with a construction center line |
| Ellipse | `Shift+E` | center, the end of one axis, then how far the other axis reaches. Its center and axis ends are points to drag, snap to and dimension (the axes stay perpendicular) |
| Spline | `Shift+C` | tap the points it passes through; tapping the first one closes it, landing on an existing point or `Enter` ends it (`Esc` drops it). Drag its points to reshape it; deleting a selected inner point takes it out of the curve |
| Fillet | `G` | tap a corner where two lines meet, type the radius: the lines are cut back and joined by a tangent arc with a radius dimension (relations of the lines carry over) |
| Chamfer | `B` | the same with a straight bevel, both lines cut back by the distance |
| Mirror | `Shift+M` | select curves (in Select, or by tapping them in the tool and `Enter`), then tap the line to mirror about. The copy is held symmetric to the original (points on the line are shared), so it follows later changes |
| Linear pattern | `Shift+L` | select curves, pick a point and where its first copy goes, type the count (with the original). Copies keep the relations among the copied curves; copied circles and arcs stay equal to their originals |
| Circular pattern | `Shift+O` | select curves, pick the center, type the count: copies spread over a full turn |
| Project | `Shift+K` | tap a body edge or face: it is copied into the sketch, pinned in place (a face gives its outline). Edges not in the sketch's plane are projected onto it: slanted circles become ellipses, other curves splines |
| Image | | *Image → Insert Image…* puts a picture on the sketch plane, about the width of the view. In the Image tool, drag it to move it, or tap two points on it a known distance apart and type that distance to scale it. *Opacity* and *Remove Image* are in the same menu; the picture is saved in the project |

Related tools share a toolbar button (Rectangle / Polygon / Slot, Circle / Ellipse, Arc / Spline,
Fillet / Chamfer, Mirror / patterns): the arrow lists the others, and the button shows the one used last.
All of them are also in the *Sketch* menu. Ellipses and splines are trimmed whole (a line crossing
one is still cut at the crossing); Offset takes lines, circles and arcs.

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
| Horizontal / Vertical dimension | `Shift+H` / `Shift+V` | a line or 2 points → the distance along X / Y |
| Horizontal / Vertical | `H` / `V` | lines or 2 points |
| Parallel / Perpendicular | `P` / `N` | 2+ / 2 lines |
| Equal | `E` | 2+ lines (length) or 2+ circles/arcs (radius) |
| Tangent | `T` | a line and a circle/arc, or 2 circles/arcs |
| Concentric | `O` | 2+ circles/arcs |
| Coincident | `I` | 2 points (merges them) or a point and a curve |
| Midpoint | `M` | a point and a line |
| Symmetric | `Y` | 2 points and a line → the points mirror each other about it |
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
| Revolve | `R` | tap sketch regions, then the axis: a sketch line, a straight edge, a round edge (its center axis) or an axis; type the angle (360 = a full turn) and pick the operation |
| Sweep | `Shift+W` | tap the profile region, then a curve of the path sketch: the profile follows it and every curve connected to it (draw the profile on a plane across the path's start) |
| Loft | `Shift+F` | tap the profile regions in order (sketches on different planes), `Enter` lofts through their outlines |
| Hole | `H` | tap a flat face where the hole goes – it snaps to sketch points on that face (then it follows the point when the sketch changes) and to round edges' centers; more taps add holes. Type the diameter and pick *Through*, *Blind* (then the depth), *Counterbore* (head diameter and depth) or *Countersink* (90°, head diameter) |
| Mirror | `M` | tap bodies, `Enter`, then the mirror plane (a flat face or a plane; tapping a plane right away works too). A mirrored half that touches the original joins it |
| Linear / Circular Pattern | `Shift+L` / `Shift+O` | tap bodies, then the direction / axis (an edge, an axis or a sketch line; for a circular one a round edge gives its center axis); type the count (with the original) and the spacing or the angle covered (360 = evenly around) |
| Offset Plane, Plane at Angle, Plane Through 3 Points, Axis | | construction geometry: a plane parallel to a face or plane at a distance, turned about an edge / axis, through three body corners; an axis along an edge, through a round edge's center or two corners |

**Patterns of features**: the arrow of an extrusion (from a sketch), revolve, sweep, loft or hole on
the *History* bar offers *Linear Pattern…* / *Circular Pattern…*: tap the direction and the copies
repeat that operation (each hole is drilled again, each boss joined), following later changes of the
original – a bolt circle is one hole and a circular pattern of it.

**Construction planes and axes** show as see-through squares and lines. The base planes (XY, XZ, YZ)
and axes (X, Y, Z) through the origin appear while a tool can use them (revolve, mirror, patterns,
planes). A plane picked in Select is where **New Sketch** starts; planes and axes are also mirror
planes, revolve and pattern axes. Like everything else they live in the history: a plane made from a
face follows that face.

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
`NIBFORM_DEMO=1` builds a sample model (`NIBFORM_DEMO=sketch` also opens a sketch,
`NIBFORM_DEMO_IMAGE=picture.png` puts a reference image on it) –
handy with `NIBFORM_SCREENSHOT`.

## History, projects, export

The model is parametric: bodies aren't saved but rebuilt from the history of operations
(sketch, extrude, fillet, chamfer, shell, revolve, sweep, loft, hole, mirror, pattern, construction
planes and axes, body operations, delete). The **History** bar at the bottom
shows them in order. A tap selects an entry, a **double-click** (double tap) edits it, and the
arrow next to it offers *Edit* / *Delete*:

- a sketch reopens, the camera smoothly turns to face it, and the model goes back to the moment
  it was made – the bodies built from it (and everything later, dimmed on the bar) disappear
  until the sketch is finished, then rebuild by themselves;
- an operation with a value opens the value field with a live preview of the whole model (`Esc`
  restores it; a hole or a pattern asks its values in turn); the camera frames the model and, when it looks exactly along or across the
  operation's direction, switches to an isometric view. An operation whose input is gone (e.g. a
  region after a sketch change) is shown in red with an explanation in the hint, and is skipped.

Operation inputs are remembered geometrically so they can be found after a rebuild: a region by a
point inside it, a face by its middle and normal, an edge by its middle and direction, a body by a
point inside it.

**File**: *New* (`Ctrl+N`), *Open…* (`Ctrl+O`), *Save* (`Ctrl+S`), *Save As…*
(`Ctrl+Shift+S`) – a `.nibform` project is readable JSON with the sketches and the history;
`nibform file.nibform` opens one right away. The window title shows `*` for unsaved changes,
and closing asks to save. *Open Recent* lists the last projects. *Export STEP…* / *Export STL…*
save the bodies (binary STL, 0.05 mm mesh).

Unsaved work is autosaved every minute to a recovery file (in `~/.local/share/Nibform/Nibform/recovery`,
never over the project itself). If Nibform is killed or crashes, the next start offers to
restore it; saving, or closing and discarding the changes, removes it. Saving writes a
temporary file and renames it into place, so a failed save never damages a project.

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
  (the whole stroke is one undo step), whatever tool is selected. Hovering, it shows the
  piece it would remove.
- **Radial menu**: a long press of the pen (half a second without moving), a click of its
  barrel button or a right click opens the tools of the current mode in a ring around the
  pointer (in a sketch: Select, Freehand, Line, Rectangle, Circle, Arc, Trim, Dimension, Undo,
  Finish; in the model: Select, New / Edit Sketch, Extrude, Revolve, Hole, Fillet, Shell, Undo, Fit).
  Tap an item, or after a long press slide onto one and lift; a tap elsewhere or `Esc` closes it.
- **Scribble to delete**: in Select (from empty space) and Freehand, scribbling back and forth
  over curves, dimensions or relation glyphs deletes them (one undo step).
- **Touch gestures**: a quick two-finger tap undoes, a three-finger tap redoes.
- **Hover**: before the pen touches, what a tap would pick lights up – sketch points, curves and
  labels, body faces and edges, and sketch regions in the model (a region in front of a face
  wins, so the face isn't lit then).
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
- [x] **M7** – release basics: CI, AUR / Flatpak packages, file association, autosave and recovery
- [x] **M8** – pen first: freehand shape recognition, radial menu, touch gestures, scribble to delete, hover preview
- [x] **M9** – sketch toolkit: polygon, slot, ellipse, spline, sketch fillets, mirror and patterns, projection, reference images
- [x] **M10** – solid features: revolve, sweep, loft, mirror, patterns, holes, construction planes
- [ ] **M11** – transform and inspect: move / rotate gizmo, measure, section view, body list, units
- [ ] **M12** – interop: STEP / IGES / STL import, DXF / SVG sketches, 3MF export
- [ ] **M13** – drawings: views, dimensions, PDF / SVG export
- [ ] **M14** – speed and robustness: background and incremental rebuild, large models

Details, scope and "done when" for each: [ROADMAP.md](ROADMAP.md).

## Releasing

1. Bump `VERSION` in `CMakeLists.txt` and add a `<release>` to
   `packaging/io.github.vilkasek.Nibform.metainfo.xml`.
2. Tag and push: `git tag v0.4.0 && git push origin v0.4.0`.
3. AUR: set `pkgver` in `packaging/aur/PKGBUILD`, then `updpkgsums` and
   `makepkg --printsrcinfo > .SRCINFO`; copy both to the `nibform` AUR repository and push.

## License

GPL-3.0-or-later; see [LICENSE](LICENSE).
