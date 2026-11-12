# Roadmap

M0–M14 and i18n are done (see the README). Next: parameters, then the bigger features from
*Later*.

Each milestone is one branch, merged when its *done when* holds and the tests pass.

## M7 – Release basics ✓

Make it installable and safe to use for real work.

- GitHub Actions: build and run the tests on every push and pull request
- Packages: an AUR `PKGBUILD` and a Flatpak manifest; `cmake --install` with a
  `.desktop` file, the icon in the hicolor theme, and a MIME type for `.nibform` files
  (double-click opens them)
- Autosave to a recovery file every minute or so; after a crash, offer to restore
- Saving writes a temporary file and renames it, so a failed save never breaks a project
- *Help → About* (version, license, website), recent files in *File*
- A license, a version number in the app, and a download section on the website

**Done when:** a fresh Arch install can `yay -S nibform`, open a `.nibform` from the file
manager, and get the work back after `kill -9`.

## M8 – Pen first ✓

What makes a pen faster than a mouse.

- Shape recognition: draw a rough line, circle, arc or rectangle freehand and get the
  clean, constrained version (a *Freehand* tool; Line stays exact)
- A radial menu on long press or the pen's barrel button: the tools of the current mode
  in reach without the toolbar
- Touch gestures: two-finger tap undoes, three-finger tap redoes
- Scribble over geometry to delete it
- Pen hover shows what a tap would pick, before touching

**Done when:** a simple bracket can be sketched, dimensioned and extruded without
touching the toolbar.

## M9 – Sketch toolkit ✓

The sketch tools a real part needs.

- Polygon, slot, ellipse, spline (through points, editable)
- Sketch fillet and chamfer of corners (trim and arc in one step, with tangency)
- Mirror about a line; linear and circular patterns in the sketch
- Horizontal / vertical distance between points (next to the aligned one)
- Project body edges into an open sketch on demand
- A reference image on the sketch plane, scaled by two points, to trace over

**Done when:** a typical laser-cut or 3D-print outline can be drawn without workarounds.

## M10 – Solid features ✓

Beyond extrude.

- Revolve around a sketch line or an edge
- Sweep along a path, loft between profiles
- Mirror bodies; linear and circular patterns of bodies and features
- Hole tool: simple, counterbore, countersink, placed on a face
- Construction planes (offset, at an angle, through three points) and axes, to sketch
  on and to mirror about
- All of them in the history, editable like extrude

**Done when:** a flange (revolve), a handle (sweep) and a bolt circle (pattern of holes)
can be built and edited from the history.

## M11 – Transform and inspect ✓

Working with what is already there.

- Move, rotate and copy bodies with an on-screen gizmo, by typed values or snapped to
  points and faces
- Measure: distance, angle, radius, area, volume, mass properties
- Section view along a plane
- A body list: names, hide / show, colors
- Units: millimetres or inches

**Done when:** two parts can be lined up, checked for fit with a section and measured.

## M12 – Interop ✓

Getting models in and out.

- Import STEP, IGES and STL (as bodies to build on, or as references)
- DXF and SVG: import into a sketch, export a sketch
- 3MF export for 3D printing (with units and colors)
- Copy and paste between projects

**Done when:** a STEP from another CAD can be opened, modified and exported back.

## M13 – Drawings ✓

2D technical drawings from the model.

- Sheets with standard views (front, top, side, isometric) and section views
- Dimensions and notes on the sheet, following the model when it changes
- Export to PDF and SVG, print

**Done when:** a part from M10 gets a dimensioned drawing a workshop can use.

## M14 – Speed and robustness ✓

Staying smooth as models grow.

- Rebuild in a background thread, the UI stays responsive (with a progress hint)
- Rebuild only from the changed feature on, with cached intermediate bodies
- Profiling on large models and long histories; tessellation that adapts to zoom
- Fuzz tests for the project loader, more tests for features and picking

**Done when:** a model with 100+ features stays interactive while editing an early one.

## M15 – Parameters

Driving a model by named values.

- A parameter table: names, values with units, comments
- Expressions anywhere a value is typed (`width / 2 + 3 mm`), with parameters, arithmetic
  and functions; the field shows the result and keeps the expression
- Dimensions and features follow when a parameter changes; cycles and unknown names are
  shown as errors, not crashes
- Configurations: named sets of parameter values to switch between (sizes of one part)
- Parameters and configurations saved in the project, exported to and imported from CSV

**Done when:** a box with a lid is resized for a new board by changing three parameters,
and *Small* / *Large* configurations switch in one tap.

## M16 – Details

The finishing features a part for real use needs.

- Threads: cosmetic (shown, and as a callout in drawings) or modelled, metric and UNC /
  UNF, on holes and cylinders; the hole tool gets tapped holes
- Text in sketches (system fonts), to extrude, emboss or engrave on flat and curved faces
- Draft on faces, ribs from an open sketch line
- Variable-radius fillets and setback corners
- All of them in the history, editable, following parameters

**Done when:** a 3D-printed enclosure gets its screw bosses with threads, a ribbed wall,
draft and an engraved label, all editable afterwards.

## M17 – Assemblies

Several parts that fit and move together.

- Components: a body or a whole project inserted as an instance, many times, updated when
  the source changes
- Joints: rigid, revolute, slider, cylindrical, ball, planar; picked from faces, edges and
  points, with limits; drag a part to move it as the joints allow
- Interference and clearance check between components
- Exploded views, also as drawing views; a parts list (BOM) on the drawing sheet
- STEP export keeps the assembly structure

**Done when:** a hinge with two leaves and a pin can be put together, opened through its
range without collisions, and gets an exploded drawing with a parts list.

## M18 – Sheet metal

Parts bent from a flat sheet.

- Sheet metal rules: thickness, bend radius, K-factor (from parameters)
- Base flange from a sketch, edge flanges, hems, tabs; bend reliefs and corner reliefs
- Convert a shelled solid to sheet metal
- Flat pattern with bend lines, kept up to date; DXF export of it for the laser cutter
- Flat pattern drawing views with bend notes

**Done when:** a bent bracket with flanges and holes unfolds to a DXF that a laser shop
can cut and bend to the right size.

## M19 – Scripting

Automating what the UI does.

- A Python API to the project: sketches, constraints, features, parameters, export
- A script console in the app and a headless `nibform --run script.py` for batch jobs
- Macros: record a sequence of operations as a script
- Plugins: scripts in a folder add commands to the toolbar and the radial menu
- API docs and examples (a gear generator, batch export of configurations)

**Done when:** a script generates a spur gear from module and tooth count as an editable
feature, and a batch job exports every configuration to STEP.

## M20 – Versions and sharing

Working on a project over time and with others.

- Named versions inside the project, with a timeline to go back to and compare (a visual
  diff of bodies and of the history)
- Branch from a version, merge history changes where they don't collide
- Comments pinned to faces, edges and drawing views
- Projects in a synced folder (Nextcloud, Syncthing, Git): conflicts detected and offered
  as a merge, not lost
- A read-only viewer mode and a web export (glTF with a small viewer page) to send to
  people without nibform

**Done when:** two people editing a copy of one project in a synced folder can merge their
changes, and a client can look at the result in a browser.

## Later

Ideas that need the above first: CAM toolpaths for 3-axis milling and laser, simple FEA
(static stress on a part), rendering with materials, direct editing of imported bodies
(move and offset faces without a history).
