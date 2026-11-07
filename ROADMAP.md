# Roadmap

M0–M6 and i18n are done (see the README). Next, roughly in order: first make Nibform
something others can install and trust with their work, then lean into what a pen does
best, then widen what can be modelled.

Each milestone is one branch, merged when its *done when* holds and the tests pass.

## M7 – Release basics

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

## M8 – Pen first

What makes a pen faster than a mouse.

- Shape recognition: draw a rough line, circle, arc or rectangle freehand and get the
  clean, constrained version (a *Freehand* tool, and optionally in Line)
- A radial menu on long press or the pen's barrel button: the tools of the current mode
  in reach without the toolbar
- Touch gestures: two-finger tap undoes, three-finger tap redoes
- Scribble over geometry to delete it
- Pen hover shows what a tap would pick, before touching

**Done when:** a simple bracket can be sketched, dimensioned and extruded without
touching the toolbar.

## M9 – Sketch toolkit

The sketch tools a real part needs.

- Polygon, slot, ellipse, spline (through points, editable)
- Sketch fillet and chamfer of corners (trim and arc in one step, with tangency)
- Mirror about a line; linear and circular patterns in the sketch
- Horizontal / vertical distance between points (next to the aligned one)
- Project body edges into an open sketch on demand
- A reference image on the sketch plane, scaled by two points, to trace over

**Done when:** a typical laser-cut or 3D-print outline can be drawn without workarounds.

## M10 – Solid features

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

## M11 – Transform and inspect

Working with what is already there.

- Move, rotate and copy bodies with an on-screen gizmo, by typed values or snapped to
  points and faces
- Measure: distance, angle, radius, area, volume, mass properties
- Section view along a plane
- A body list: names, hide / show, colors
- Units: millimetres or inches

**Done when:** two parts can be lined up, checked for fit with a section and measured.

## M12 – Interop

Getting models in and out.

- Import STEP, IGES and STL (as bodies to build on, or as references)
- DXF and SVG: import into a sketch, export a sketch
- 3MF export for 3D printing (with units and colors)
- Copy and paste between projects

**Done when:** a STEP from another CAD can be opened, modified and exported back.

## M13 – Drawings

2D technical drawings from the model.

- Sheets with standard views (front, top, side, isometric) and section views
- Dimensions and notes on the sheet, following the model when it changes
- Export to PDF and SVG, print

**Done when:** a part from M10 gets a dimensioned drawing a workshop can use.

## M14 – Speed and robustness

Staying smooth as models grow.

- Rebuild in a background thread, the UI stays responsive (with a progress hint)
- Rebuild only from the changed feature on, with cached intermediate bodies
- Profiling on large models and long histories; tessellation that adapts to zoom
- Fuzz tests for the project loader, more tests for features and picking

**Done when:** a model with 100+ features stays interactive while editing an early one.

## Later

Ideas that need the above first: assemblies and joints, sheet metal, threads and
cosmetic features, a plugin or scripting interface, collaboration on shared projects.
