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
sudo pacman -S --needed qt6-base qt6-svg qt6-tools opencascade eigen cmake ninja
cmake -S . -B build -G Ninja
cmake --build build
./build/nibform
```

Arch's OpenCASCADE is built against GLX, so the app forces `QT_QPA_PLATFORM=xcb` itself
(on Wayland it runs through XWayland). The OpenGL compatibility profile is used by default
(in core, Mesa limits line width to 1 px); for the core profile: `NIBFORM_GL_CORE=1`.
`NIBFORM_SCREENSHOT=file.png` renders one frame, saves it and quits.
`NIBFORM_DEBUG_INPUT=1` prints the input devices and scroll/gesture events,
`NIBFORM_INVERT_SCROLL=1` inverts the scroll pan / orbit direction.

**Scrolling** (*View → Scrolling*, remembered): the mouse wheel and two-finger touchpad scrolling
**zoom** about the pointer, `Shift` pans – or, touchpad style, they orbit (`Shift` pans, `Ctrl`
zooms). A pinch always zooms. (A wheel can't be told from a touchpad reliably: XWayland merges
them, and fine-grained wheels scroll in small steps too.)

**Keyboard shortcuts** (*Edit → Keyboard Shortcuts…*): every tool, constraint, view and command,
grouped and searchable; click a field and press the new keys, clear it for none, *Default* puts one
back, *Reset All* all of them. Keys used twice in one group show in red (a sketch tool and a model
tool may share keys: only one of them works at a time). Saved in the settings.

`NIBFORM_AUTOSAVE_SECONDS=5` autosaves more often than every minute.
`-DNIBFORM_BUILD_TESTS=OFF` skips the tests (and Qt Test).

Core tests: `ctest --test-dir build` (or `./build/test_sketch`, `./build/test_solver`, `./build/test_trim`, `./build/test_offset`, `./build/test_model`, `./build/test_features`, `./build/test_project`, `./build/test_input`, `./build/test_value_editor`, `./build/test_toolkit`, `./build/test_freehand`, `./build/test_solid_features`, `./build/test_inspect`, `./build/test_interop`, `./build/test_drawing`).

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
| Move | `T` | tap bodies (or pick them in Select first): they show where they go, with a gizmo – drag its arrows to slide them, its rings to turn them. Or tap a corner, round edge or flat face of them, then one of another body: corners and round edges' centers meet, a face lies flush against the other (turned to face it), a corner lands on a face. `Enter` asks offsets along X / Y / Z and a turn about Z (typed on top of what was dragged), with *Move* / *Copy*, and applies – a step of the history |
| Measure | `I` | tap a face (area, radius), an edge (length, radius) or a corner (position); a second pick gives the distance (with a line), the angle between flat faces / straight edges and the distance between round edges' centers. The card also shows the body's volume, surface, center of mass, size and its mass in PLA |
| Section | | tap a flat face or a plane (the base ones show): the view is cut open there, the cut filled in red; type an offset, pick the side to keep. *View → Section View* turns it off |
| Offset Plane, Plane at Angle, Plane Through 3 Points, Axis | | construction geometry: a plane parallel to a face or plane at a distance, turned about an edge / axis, through three body corners; an axis along an edge, through a round edge's center or two corners |

**Patterns of features**: the arrow of an extrusion (from a sketch), revolve, sweep, loft or hole on
the *History* bar offers *Linear Pattern…* / *Circular Pattern…*: tap the direction and the copies
repeat that operation (each hole is drilled again, each boss joined), following later changes of the
original – a bolt circle is one hole and a circular pattern of it.

**Bodies panel** (*View → Body List*): every body with a checkbox to show / hide it, its name
(double-click to rename) and a color swatch (*Choose Color…* / *Default Color*). Names, visibility
and colors are saved with the project and follow the bodies as the history rebuilds (each is found
again by a point inside it). **Units** (*View → Units*): millimetres or inches – values are shown
and typed in the chosen unit, the model stays in millimetres.

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
(sketch, extrude, fillet, chamfer, shell, revolve, sweep, loft, hole, mirror, pattern, move / copy,
construction planes and axes, body operations, delete). The **History** bar at the bottom
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
and closing asks to save. *Open Recent* lists the last projects.

**Getting models in and out**:

- *Open…* also takes STEP, IGES and STL files (and `nibform part.step` from the command line): they
  become a new, unsaved project whose history starts with the imported bodies – push / pull their
  faces, drill holes, round edges, then *Export* back.
- *Import…* (`Ctrl+I`) adds a STEP, IGES or STL file to the open project, as **bodies** to build on
  or as a **reference** – shown see-through, not modelled (a big or open STL mesh can only be one).
  The geometry is kept in the project, so it doesn't need the file later; the import is a step of
  the history ("Import · part.step").
- *Export* saves the bodies as STEP, IGES, STL (binary, 0.05 mm mesh) or **3MF** for 3D printing:
  one object per shown body with its name and color, in millimetres (or inches, when the app shows
  inches).
- *Sketch → Import Drawing (DXF / SVG)…* adds a drawing to the open sketch: DXF lines, circles,
  arcs, polylines (with bulges), splines, ellipses and points in the file's units; SVG paths, lines,
  rectangles, circles, ellipses and polylines with their transforms, in millimetres. Touching ends are
  joined, curves become splines. *Export Sketch (DXF / SVG)…* writes the open sketch (construction
  curves on their own layer / dashed).
- *Edit → Copy* / *Paste* (`Ctrl+C` / `Ctrl+V`), also between projects (two windows): curves selected
  in a sketch, with the relations and dimensions among them, paste into the open sketch (shifted a
  little when they would land right on top of themselves); selected bodies paste as an import.

Unsaved work is autosaved every minute to a recovery file (in `~/.local/share/Nibform/Nibform/recovery`,
never over the project itself). If Nibform is killed or crashes, the next start offers to
restore it; saving, or closing and discarding the changes, removes it. Saving writes a
temporary file and renames it into place, so a failed save never damages a project.

## Drawings

**Drawing** (on the model bar) turns the window into a technical drawing of the shown bodies (the
"DRAWING" badge; *Back to Model* returns). A new drawing gets the front view, the top view below
it, the left view to its right (first-angle projection, as in ISO / Europe) and an isometric view,
at a standard scale that fits an A4 sheet (A3 for big parts), with a frame and a title block
(title, scale, units, sheet size, date).

- Views show visible edges thick and hidden ones dashed (a view's double-tap menu turns them off),
  holes and round edges seen face on get center lines. *Add View* adds any of the six standard
  views, an isometric one, or a **section**: cut across the part's middle and seen like the front,
  top, left or right view, the cut faces hatched, labelled A–A, with the cutting line and arrows
  drawn on the views that see the plane edge on; *Cut Position…* moves the cut.
- **Dimension**: tap two corners or centers in a view – level or upright pairs give horizontal /
  vertical dimensions, others aligned ones (switchable from the double-tap menu); tap the rim of a
  hole for its diameter (an arc gives a radius). Dimensions are placed outside the view; drag them
  (or their leaders) where they read best. They are attached to the model: change the history and
  they follow the corners and holes they measure (one that loses its geometry shows a red "?").
- **Note**: tap where a text goes (multi-line; sizes 2.5–10 mm).
- *Select*: drag views, dimensions and notes; a double tap opens the options (scale, hidden edges,
  kind, text, delete); `Delete` removes the selected one. Right drag pans, the wheel or a pinch zooms,
  `F` fits the sheet. *Sheet*: A4 / A3 portrait or landscape, A2; the title.
- *PDF*, *SVG* and *Print* (`Ctrl+P`) give the sheet at its real size. Drawings are saved in the
  project and undone with the rest.

## View controls

| Input | Action |
|---|---|
| Pen (tip) | the active sketch tool |
| Pen + button | orbit |
| 1 finger | orbit |
| 2 fingers | pan + pinch zoom |
| Mouse LMB / RMB / MMB / wheel | sketch tool / orbit / pan / zoom (Shift+wheel pans) |
| Shift+RMB, Ctrl+RMB | pan, zoom |
| Touchpad: 2-finger scroll | zoom, or orbit with *View → Scrolling → Orbits* (a TrackPoint with the middle button too) |
| Touchpad: Shift + 2 fingers / 3 fingers | pan |
| Touchpad: pinch | zoom |
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
- [x] **M11** – transform and inspect: move / rotate gizmo, measure, section view, body list, units
- [x] **M12** – interop: STEP / IGES / STL import, DXF / SVG sketches, 3MF export, copy and paste
- [x] **M13** – drawings: views, sections, dimensions, notes, PDF / SVG export, printing
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
