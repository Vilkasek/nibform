# Nibform

[![CI](https://github.com/Vilkasek/nibform/actions/workflows/ci.yml/badge.svg)](https://github.com/Vilkasek/nibform/actions/workflows/ci.yml)

**A native, pen-first parametric CAD app for Linux** – made for 2-in-1 laptops with a stylus
(the Framework 12 in the first place), and just as usable with a mouse and keyboard.
C++20 · Qt 6 · OpenCASCADE 7.9 · Eigen · GPL-3.0-or-later.

**Website:** https://vilkasek.github.io/nibform/

- **Sketches with constraints**: lines, arcs, circles, rectangles, polygons, slots, ellipses and
  splines – drawn with tools or freehand (rough strokes become clean shapes) – held by relations
  and dimensions that a solver keeps true as you drag and edit.
- **Solid modelling**: extrude (push / pull), revolve, sweep, loft, holes, fillets, chamfers,
  shells, booleans, mirror and patterns, construction planes and axes.
- **Parametric history**: every step can be edited later; the model rebuilds from it.
- **Move, measure, section**: a move gizmo and face-to-face alignment, measurements with mass
  properties, a section view.
- **Exchange**: STEP, IGES and STL in; STEP, IGES, STL and 3MF out; DXF and SVG sketches.
- **Technical drawings**: views, sections, dimensions that follow the model, PDF / SVG / print.
- **Pen first**: a radial menu under the pen, scribble to delete, palm rejection, touch gestures,
  a tablet mode. English and Polish.

## Contents

[Installing](#installing) · [Building](#building) · [Getting started](#getting-started) ·
[Sketching](#sketching) · [Constraints and dimensions](#constraints-and-dimensions) ·
[Bodies](#bodies) · [History and projects](#history-and-projects) ·
[Import and export](#import-and-export) · [Drawings](#drawings) · [Controls](#controls) ·
[Pen, touch and tablet](#pen-touch-and-tablet) · [Language](#language) ·
[Development](#development) · [Plan](#plan)

## Installing

- **Arch Linux** (AUR): `yay -S nibform` (the `PKGBUILD` is in `packaging/aur`).
- **Flatpak**: a manifest is in `packaging/flatpak`:
  `flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.vilkasek.Nibform.yml`.
- **From source**: build it (below), then `sudo cmake --install build`.

Each way gives a menu entry and an icon, and `.nibform` projects open from the file manager
(MIME type `application/x-nibform-project`; the app id is `io.github.vilkasek.Nibform`).

## Building

```sh
sudo pacman -S --needed qt6-base qt6-svg qt6-tools opencascade eigen cmake ninja
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build   # optional: the tests
./build/nibform
```

Other distributions work too, with Qt 6 (Widgets, OpenGL, Svg, PrintSupport, LinguistTools),
OpenCASCADE 7.9, Eigen 3 and CMake 3.24+. Arch's OpenCASCADE is built against GLX, so the app
forces `QT_QPA_PLATFORM=xcb` itself (on Wayland it runs through XWayland).

## Getting started

1. **New Sketch** (`Shift+S`) opens a sketch on the ground (or on a selected flat face). Draw a closed
   outline – with **Line** (`L`), **Rectangle** (`R`), **Circle** (`C`)… or roughly with
   **Freehand** (`Ctrl+Shift+F`).
2. Select a line and press **`D`** to give it a length; add relations from the constraint bar.
   Geometry turns green when nothing is left to move.
3. **✓ Finish Sketch** (`Ctrl+Enter`), tap the closed region and drag its arrow (or type a
   distance): it becomes a body.
4. Round edges with **Fillet** (`Shift+F`), drill with **Hole** (`H`)… Every step lands on the
   **History** bar at the bottom – double-tap one to change it, and everything after it follows.
5. *File → Save* keeps a `.nibform` project; *File → Export* hands the bodies to other tools,
   **Drawing** makes a dimensioned sheet of them.

`NIBFORM_DEMO=1 ./build/nibform` opens with a small sample model.

## Sketching

The app starts in **Model** mode (the "MODEL" badge). **New Sketch** (`Shift+S`) opens a sketch – on
the selected flat face of a body (its edges come in as locked construction geometry, to snap and
dimension to), on a selected construction plane, or on the ground; the arrow next to the button
picks a base plane (XY / XZ / YZ). The view turns to face it and the badge changes to "SKETCH".
**✓ Finish Sketch** (`Ctrl+Enter`) goes back to the model (an empty sketch disappears);
**Edit Sketch** reopens the sketch whose region is selected. Each mode has its own tools and
shortcuts.

| Tool | Shortcut | How |
|---|---|---|
| Select | `S` | tap an item, more taps add to the selection; drag points and curves; `Delete` removes |
| Line | `L` | tap-tap draws a polyline (it ends on an existing point, or with `Esc` / `Enter`); a drag draws a single segment |
| Freehand | `Ctrl+Shift+F` | draw roughly in one stroke and get the clean shape: a line, corners (a polyline or a closed polygon), a rectangle (turned or not), a circle or an arc. Ends join points nearby, nearly level / upright sides become exactly so, a turned rectangle keeps its right angles. A scribble deletes what it covers; a tap selects |
| Rectangle | `R` | two opposite corners |
| Polygon | `Shift+P` | center, then a corner; asks for the number of sides. Inscribed in a construction circle, sides equal |
| Slot | `Shift+S` | the centers of both round ends, then the width |
| Circle | `C` | center, then a point on the rim |
| Ellipse | `Shift+E` | center, the end of one axis, then how far the other one reaches |
| Arc | `A` | start, end, then a point on the arc |
| Spline | `Shift+N` | tap the points it passes through; tapping the first one closes it, `Enter` (or landing on an existing point) ends it. Drag its points to reshape it; deleting an inner point takes it out of the curve |
| Trim | `T` | the piece between the nearest crossings lights up red; a tap removes it, a drag removes everything along the way |
| Offset | `O` | tap a curve: its whole connected chain follows the pointer; another tap asks for an exact distance |
| Fillet / Chamfer | `Shift+F` / `Shift+C` | tap a corner where two lines meet and type the radius / distance |
| Mirror | `Shift+M` | select curves, then tap the line to mirror about; the copy stays symmetric |
| Linear / Circular pattern | `Shift+L` / `Ctrl+Shift+C` | select curves, pick the step (or the center) and the count |
| Project | `Ctrl+Shift+P` | tap a body edge or face: it is copied into the sketch, pinned in place |
| Construction | `Q` | toggles the selected curves to helper geometry (dashed: snapped to and dimensioned, never bounding a region) |
| Image | | *Image → Insert Image…* puts a picture on the sketch plane to trace over; tap two points a known distance apart and type it to scale it |

Related tools share a toolbar button (the arrow lists the others, the button shows the one used
last); all of them are also in the *Sketch* menu.

**Snapping** goes to points, the origin, segment middles and horizontal / vertical alignment, and
leaves the matching relation behind (an aligned line gets "horizontal", a point on the origin
"fix"…). `Alt` turns it off. `Esc` drops the operation in progress, a second `Esc` goes back to
Select.

## Constraints and dimensions

Select points and curves, then pick a constraint from the second bar (or the *Constraints*
menu); the ones that don't fit the selection are greyed out.

| Constraint | Shortcut | Selection |
|---|---|---|
| Dimension | `D` | a line or 2 points → distance; a point and a line → distance from it; 2 parallel lines → spacing; 2 other lines → angle; a circle → diameter; an arc → radius |
| Horizontal / Vertical dimension | `Shift+H` / `Shift+V` | a line or 2 points → the distance along X / Y |
| Horizontal / Vertical | `H` / `V` | lines, or 2 points |
| Parallel / Perpendicular | `P` / `N` | 2+ / 2 lines |
| Equal | `E` | lines (length) or circles and arcs (radius) |
| Tangent | `Shift+T` | a line and a circle / arc, or two circles / arcs |
| Concentric | `Shift+O` | circles and arcs |
| Coincident | `I` | 2 points (merged), or a point and a curve |
| Midpoint | `M` | a point and a line |
| Symmetric | `Y` | 2 points and a line they mirror about |
| Fix | `Z` | points or curves, pinned where they are |

**`D` with nothing selected** is a tool: tap what to dimension. While its value field is open,
tapping another point or line turns it into a dimension between them. Tap a selected dimension
(or press `Enter`) to change its value; both `,` and `.` work as the decimal separator.

Dragging moves the geometry while the solver keeps every constraint. Fully defined geometry turns
green, the status bar counts the degrees of freedom left, and conflicting or redundant constraints
are refused with a message. Relation glyphs and dimension labels can be selected and deleted.

## Bodies

Closed sketch regions are lightly filled. **Tap a region** (in Select or *Extrude*, `E`) and an
arrow appears with a distance field and a preview: drag the arrow or type a value (negative goes
the other way), tap more regions or flat body faces to add them, `Enter` applies, `Esc` drops it.

| Tool | Shortcut | How |
|---|---|---|
| Extrude | `E` | sketch regions and flat body faces (push / pull) |
| Revolve | `R` | tap regions, then the axis – a sketch line, a straight or round edge, or an axis; type the angle |
| Sweep | `W` | tap the profile region, then a curve of the path sketch (the curves connected to it follow) |
| Loft | `L` | tap profile regions on different planes in order, then `Enter` |
| Hole | `H` | tap a flat face where it goes (it snaps to sketch points – and then follows them – and to round edges' centers); through, blind, counterbored or countersunk |
| Fillet / Chamfer | `Shift+F` / `Shift+C` | tap edges (a face takes all of its edges), drag the handle or type the size |
| Shell | `Shift+H` | tap the faces to open and type the wall thickness |
| Move | `M` | tap bodies, then drag the gizmo's arrows and rings – or tap a corner, round edge or face of them and then one of another body: they meet, faces lie flush. `Enter` types offsets and a turn, and moves or copies |
| Mirror | `Shift+M` | tap bodies, `Enter`, then the mirror plane (a flat face or a plane) |
| Linear / Circular Pattern | `Shift+L` / `Ctrl+Shift+C` | tap a face of a cut, boss or hole to repeat what made it (or tap bodies), then the direction or axis; the count, spacing or angle go in the *Pattern* panel |
| Measure | `Ctrl+Shift+M` | tap a face, edge or corner, and a second one: lengths, areas, radii, distances, angles, plus the body's volume, center of mass, size and mass |
| Section | | tap a flat face or a plane: the view is cut open there (*View → Section View* turns it off) |
| Planes and axes | | an offset plane, a plane at an angle, a plane through 3 points, an axis – to sketch on, mirror about, revolve or pattern around |

What an extrusion (or revolve, sweep, loft) does to the bodies it meets is chosen under the value
field or in the *Operation* box: **Automatic** cuts when it starts inside a body (a sketch on a
face pushed into it), joins when it reaches or touches one, and makes a new body otherwise;
*New Body*, *Join* and *Cut* force one of them.

- **Patterns of features**: in *Linear / Circular Pattern*, tap any face of a pocket cut from a
  sketch, a boss, a hole (what an extrusion from a sketch, a revolve, sweep, loft or hole cut or
  added): that operation is picked and its faces light up; tap it again to drop it. Faces of
  nothing in particular pick their bodies instead. The *History* bar's arrow of such an entry
  offers the same (*Linear / Circular Pattern…*). The copies repeat the operation, so a bolt circle
  is one hole and a circular pattern of it, and changing the hole changes them all.
- **The Pattern panel** (on the right while a pattern tool is on, or a pattern from the history is
  edited) takes the numbers: the count (the original included), the spacing or the angle covered,
  *Full circle* (spread evenly around), *Reverse direction*; the model previews them as they are
  typed. *Accept* (`Enter`) applies, *Cancel* drops it (an edited pattern goes back).
- **Construction planes and axes** show as see-through squares and lines; the base planes and axes
  through the origin appear while a tool can use them. A plane picked in Select is where New
  Sketch starts.
- **Picking**: in Select, faces and edges of bodies can be picked anywhere (the edge wins near
  it). With edges picked, a small bar offers *Fillet* / *Chamfer*; with a flat face, *Sketch on
  Face* / *Extrude*. The *Bodies* menu unions, subtracts or intersects the bodies picked;
  `Delete` removes them (and sketches with picked regions).
- **Body list** (*View → Body List*): show / hide bodies, rename them, give them colors – kept with
  the project, and passed on to 3MF exports.
- Extruded regions are hidden so they don't cover the faces they lie on; *View → Show Extruded
  Regions* brings them back.

## History and projects

Bodies aren't stored: they are rebuilt from the **history** of operations, shown in order on the
bar at the bottom. A tap selects an entry, a double tap edits it, the arrow next to it offers
*Edit*, *Delete* and (where it applies) *Pattern*:

- a sketch reopens, and the model goes back to the moment it was made until it is finished;
- an operation with values asks them again with a live preview of the whole model (`Esc` puts it
  back).

Rebuilds replay only from the first entry that changed – every step's result is kept, so undo,
redo and putting a value back are instant – and run in the background: on a long history the
window stays usable, with a *Rebuilding…* bar at the top until the new model shows.

An operation whose input is gone (a region after a sketch change) turns red with an explanation
and is skipped. Inputs are remembered as geometry and found again on every rebuild: a region by a
point inside it, a face by its middle and normal, an edge by its middle and direction, a body by a
point inside it.

*File → Save* writes a `.nibform` project: readable JSON with the sketches, the history, drawings
and body names / colors. `nibform file.nibform` opens one; *Open Recent* lists the last ones. Undo
(`Ctrl+Z`) and redo (`Ctrl+Shift+Z`) cover everything. Unsaved work is autosaved every minute to a
recovery file (`~/.local/share/Nibform/Nibform/recovery`); after a crash the next start offers to
restore it. Saving writes a temporary file and renames it, so a failed save never breaks a project.

## Import and export

- **Open** a STEP, IGES or STL file (or `nibform part.step`): it becomes a new project whose history
  starts with the imported bodies – push / pull their faces, drill, round, and export back.
- **Import** (`Ctrl+I`) adds one to the open project, as **bodies** to build on or as a
  **reference** to look at (see-through; a big or open STL mesh can only be one). The geometry is
  kept in the project.
- **Export**: STEP, IGES, STL (binary, 0.05 mm mesh) or **3MF** for 3D printing – one object per
  shown body, with its name and color, in millimetres or inches.
- **Sketch → Import Drawing / Export Sketch**: DXF (lines, circles, arcs, polylines with bulges,
  splines, ellipses, points; in the file's units) and SVG (paths, shapes and their transforms,
  scaled to millimetres).
- **Copy / Paste** (`Ctrl+C` / `Ctrl+V`), also between two windows: sketch curves with the relations
  and dimensions among them, or whole bodies.

## Drawings

**Drawing** (on the model bar) turns the window into a technical drawing of the shown bodies:
the front view, the top view below it, the left view to its right (first-angle projection, as in
ISO / Europe) and an isometric view, at a standard scale that fits the sheet, with a frame and a
title block. *Back to Model* returns.

- **Views** show visible edges thick, hidden ones dashed, center lines on holes. *Add View* adds
  any standard view, an isometric one, or a **section** – hatched, labelled A–A, with its cutting
  line drawn on the views that see it edge on.
- **Dimension**: tap two corners or centers of a view, or the rim of a hole for its diameter.
  Dimensions are tied to the model: they follow the history's changes (a lost one shows a red "?").
- **Note**: tap where a text goes.
- In *Select*, drag views, dimensions and notes; a double tap opens their options (scale, hidden
  edges, cut position, dimension kind, text); `Delete` removes them. *Sheet* picks A4–A2,
  portrait or landscape, and the title.
- **PDF**, **SVG** and **Print** (`Ctrl+P`) give the sheet at its real size. Drawings are saved in
  the project.

## Controls

| Input | Action |
|---|---|
| Pen tip | the active tool |
| Pen button | drag: orbit; click: the radial menu |
| 1 finger / 2 fingers | orbit / pan and pinch zoom |
| Mouse left / right / middle button | the active tool / orbit (a click: the radial menu) / pan |
| Wheel, two-finger scroll | zoom about the pointer; with `Shift`, pan |
| `Shift` / `Ctrl` + right button | pan / zoom |
| Touchpad pinch, 3-finger swipe | zoom, pan |
| `F` | fit the view |
| `1`–`6` | the top, front, right, back, left and bottom views |
| `Ctrl+1`–`8` | the corners (`Ctrl+1`: isometric) |
| `Shift+1`–`0`, `Ctrl+Shift+9` / `0` | the edges |
| Corner cube | click a face, edge or corner |

View changes glide. Zoomed in on a body, its round faces get a finer mesh once the view rests (as
far as that stays quick: a very big body keeps its mesh). *View* also has:

- **Scrolling**: *Zooms* (the default) or *Orbits, touchpad style* – then two-finger scrolling
  turns the model, `Shift` pans and `Ctrl` zooms. (A wheel can't be told from a touchpad
  reliably, so it is a choice.)
- **Units**: millimetres or inches – values are shown and typed in it, the model stays in mm.
- **Theme**: System (follows the desktop), Light or Dark.

**Keyboard shortcuts** (*Edit → Keyboard Shortcuts…*): every tool, constraint, view and command,
grouped and searchable. Click a field and press the new keys, clear it for none; *Default* and
*Reset All* put them back. Keys that two actions would answer at the same time show in red (a
sketch tool and a model tool may share keys – only one of them works at a time). The defaults
follow the tools' English names: the first letter, else `Shift` + it, else `Ctrl+Shift` + it
(`F` stays *Fit*); where all three are taken, another letter of the name (*Sweep* `W`,
*Shell* `Shift+H`, *Spline* `Shift+N`).

## Pen, touch and tablet

- **Radial menu**: a long press of the pen, a click of its button or a right click opens the tools
  of the current mode in a ring around the pointer. Tap one, or after a long press slide onto one
  and lift.
- **Freehand and scribbles**: in a sketch, rough strokes become clean shapes (*Freehand*), and
  scribbling back and forth over curves or labels deletes them (also in Select).
- **The pen's eraser** trims the sketch pieces it passes over, whatever the tool; hovering, it
  shows what it would remove.
- **Hover**: before the pen touches, what a tap would pick lights up.
- **Touch gestures**: a quick two-finger tap undoes, three fingers redo.
- **Palm rejection** (*View → Palm Rejection*, on by default): while the pen is near, touches are
  ignored; wide contacts (a palm) always are.
- **Tablet mode** (*View → Tablet Mode*, `Ctrl+T`): bigger buttons and pick areas, a side bar with
  *Undo / Redo / Delete / Cancel / Done / Fit*, and a keypad in value fields.
- **Screen rotation**: in a narrow window the bars show icons only, and the model is framed again
  after turning.

## Language

English or Polish: *View → Język / Language* offers *System* (Polish on a Polish desktop, English
otherwise), *Polski* and *English*. The change applies right away, Qt's dialogs and the decimal
separator included.

## Development

The code is in `src/`: `model/` (solids, features, drawings, exchange – no Qt), `sketch/` (the
sketch, the solver, tools and the editor), `io/` (project files, DXF / SVG), `viewer/` (the
OpenCASCADE view) and `app/` (the window, the drawing sheet). The solver
(`src/sketch/Solver.cpp`, Eigen only) is Gauss-Newton with a minimum-norm step, so geometry moves as
little as possible; degrees of freedom come from the Jacobian's null space (SVD).

**Tests**: `ctest --test-dir build`, or one suite: `./build/test_sketch`, `test_solver`,
`test_trim`, `test_offset`, `test_model`, `test_features`, `test_project`, `test_input`,
`test_value_editor`, `test_toolkit`, `test_freehand`, `test_solid_features`, `test_inspect`,
`test_interop`, `test_drawing`, `test_rebuild`. `-DNIBFORM_BUILD_TESTS=OFF` skips them (and Qt Test).
`./build/bench_rebuild [pockets…]` times rebuilds of long histories (a plate with that many
pockets and holes; 100 makes 227 features): a full replay, by kind of feature, and cached
rebuilds after an edit early, midway and at the end.

**Environment variables** (development aids):

| Variable | Effect |
|---|---|
| `NIBFORM_DEMO=1` | builds a sample model; `=sketch` also opens a sketch |
| `NIBFORM_DEMO_IMAGE=picture.png` | with the sketch demo, a reference image on it |
| `NIBFORM_SCREENSHOT=file.png` | renders one frame, saves it and quits |
| `NIBFORM_WINDOW_SIZE=800x1280` | a window of that size (try portrait on a regular screen) |
| `NIBFORM_DEBUG_INPUT=1` | prints the input devices and scroll / gesture events |
| `NIBFORM_INVERT_SCROLL=1` | inverts the scroll pan / orbit direction |
| `NIBFORM_GL_CORE=1` | the OpenGL core profile (the compatibility one is the default: in core, Mesa draws 1 px lines) |
| `NIBFORM_AUTOSAVE_SECONDS=5` | autosaves more often than every minute |

**Translations**: the sources are in English; the Polish text lives in `translations/nibform_pl.ts`
(Qt Linguist) and is built into the app. After changing user-facing strings:

```sh
cmake --build build --target update_translations   # lupdate: new / changed strings into the .ts
python3 translations/fill_pl.py                    # fills in the Polish from its dictionary
```

`fill_pl.py` lists the strings it has no translation for.

**Website and icon**: the website is `docs/` (GitHub Pages, *Deploy from a branch*, `main` /
`/docs`). The logo is `docs/assets/logo.svg`; the window icon is drawn from it:
`rsvg-convert -w 256 -h 256 docs/assets/logo.svg -o src/app/nibform.png`.

**Releasing**:

1. Bump `VERSION` in `CMakeLists.txt` and add a `<release>` to
   `packaging/io.github.vilkasek.Nibform.metainfo.xml`.
2. Tag and push: `git tag v0.4.0 && git push origin v0.4.0`.
3. AUR: set `pkgver` in `packaging/aur/PKGBUILD`, run `updpkgsums` and
   `makepkg --printsrcinfo > .SRCINFO`, then copy both to the `nibform` AUR repository and push.

## Plan

- [x] **M0** – Qt window + OCCT view, mouse / touchpad / touch / pen navigation
- [x] **M1** – sketching on a plane with the pen (line, rectangle, circle, arc), snapping
- [x] **M2** – constraints and dimensions, solver
- [x] **M3** – profile detection, push / pull, booleans
- [x] **M4** – parametric history, project files, STEP / STL export
- [x] **M5** – sketch on face, fillet / chamfer / shell
- [x] **M6** – Framework 12 polish (screen rotation, palm rejection, tablet mode)
- [x] **i18n** – English UI with a Polish translation, language switching
- [x] **M7** – release basics: CI, AUR / Flatpak packages, file association, autosave and recovery
- [x] **M8** – pen first: freehand shape recognition, radial menu, touch gestures, scribble to delete, hover preview
- [x] **M9** – sketch toolkit: polygon, slot, ellipse, spline, sketch fillets, mirror and patterns, projection, reference images
- [x] **M10** – solid features: revolve, sweep, loft, mirror, patterns, holes, construction planes
- [x] **M11** – transform and inspect: move / rotate gizmo, measure, section view, body list, units
- [x] **M12** – interop: STEP / IGES / STL import, DXF / SVG sketches, 3MF export, copy and paste
- [x] **M13** – drawings: views, sections, dimensions, notes, PDF / SVG export, printing
- [x] **M14** – speed and robustness: background and incremental rebuild, large models

Details, scope and "done when" for each: [ROADMAP.md](ROADMAP.md).

## License

GPL-3.0-or-later; see [LICENSE](LICENSE).
