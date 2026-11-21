# Scripting Nibform

Nibform runs Python scripts on a project: make sketches and features, set parameters,
switch configurations, rebuild and export – in the app, or with no window at all. A
script can also add **generated features**: solids made by your own Python function,
kept in the history with their values, which stay editable and follow parameters.

- [Where scripts run](#where-scripts-run)
- [A first script](#a-first-script)
- [The document](#the-document)
- [Sketches](#sketches)
- [Features](#features)
- [Parameters and configurations](#parameters-and-configurations)
- [Results and export](#results-and-export)
- [Generated features](#generated-features)
- [Commands and plugins](#commands-and-plugins)
- [Macros](#macros)
- [Scripts in projects from elsewhere](#scripts-in-projects-from-elsewhere)
- [Examples](#examples)

## Where scripts run

- **Script Console** (*Scripts → Script Console*, `Ctrl+Alt+P`): type Python a line at a time;
  a block (`def`, `for`, …) runs once an empty line ends it. `nf` is the `nibform` module.
- **Run Script…** (*Scripts*): runs a `.py` file on the project in the window.
- **Headless**: `nibform --run script.py [arguments]` runs a script with no window;
  `sys.argv` is the script and its arguments, `nf.document()` a new project, and the
  script's `sys.exit()` code is the app's.

In the app, a script works on the project as it is when the script starts, and
everything the script changes becomes **one step** to undo.

## A first script

```python
import nibform as nf

doc = nf.document()                      # the project in the window
s = doc.sketch("XY")                     # a sketch on the XY plane
s.rectangle(0, 0, 60, 40)
plate = doc.extrude(s, 8)                # every closed region of it, 8 mm up
doc.hole(nf.face((30, 20, 8), (0, 0, 1)), [(15, 20, 8), (45, 20, 8)], 6)
print(doc.bodies[0].volume)
```

## The document

| | |
|---|---|
| `nf.document()` | the project being worked on: the window's, or a new one when headless |
| `nf.new()` | a new, empty project |
| `nf.open(path)` | a `.nibform` project file |
| `doc.save(path)` | writes the project |
| `doc.sketches`, `doc.features` | its sketches and the history, in order |

Bodies are not stored, they are rebuilt from the history – exactly as in the app.
Faces and edges are picked by geometry, the way the app remembers them:

- `nf.face(middle, normal)` – the flat face through `middle` facing `normal`;
- `nf.edge(middle, direction)` – the edge nearest `middle` running along `direction`.

Points in space are `(x, y, z)`, in millimetres.

## Sketches

`doc.sketch(plane="XY", offset=0)` makes a sketch on a base plane (`"XY"`, `"XZ"`, `"YZ"`)
moved `offset` along its normal, or on `(origin, normal[, x direction])`.
`doc.sketch_on(nf.face(...))` makes one on a flat face of a body; it moves with the face.

Curves return their ids; points are ids or `(x, y)` (a point already there is reused):

| | |
|---|---|
| `s.point(x, y)` | a point |
| `s.line(a, b)` | a line |
| `s.polyline(points, closed=False)` | lines through the points |
| `s.rectangle(x0, y0, x1, y1)` | four lines: bottom, right, top, left |
| `s.circle(center, radius)` | a circle |
| `s.arc(center, start, end)` | an arc, counterclockwise |
| `s.spline(points, closed=False)` | a smooth curve through the points |
| `s.text(at, text, height=10)` | text in a system font |
| `s.construction(curve)` | makes a curve construction geometry |
| `s.ends(curve)`, `s.center(curve)`, `s.position(point)` | what a curve is made of, where a point is |

**Relations**: `horizontal(line)`, `vertical(line)`, `parallel(a, b)`, `perpendicular(a, b)`,
`equal(a, b)`, `concentric(a, b)`, `tangent(a, b)`, `on(point, curve)`, `midpoint(point, line)`,
`symmetric(a, b, line)`, `fix(point)`, `coincident(keep, drop)`.

**Dimensions** take a number or an **expression of the parameters** (a string):
`distance(a, b, value)`, `length(line, value)`, `horizontal_distance(a, b, value)`,
`vertical_distance(a, b, value)`, `radius(circle, value)`, `diameter(circle, value)`,
`angle(a, b, degrees)`. The sketch is solved after each; `s.solve()` solves it again.

**Regions**: `s.region(x, y)` is the closed region with `(x, y)` inside it, `s.regions()`
all of them. Features take a region, a list of them, or a whole sketch.

## Features

Each returns a `Feature`. Values are numbers or expressions of the parameters (strings);
`operation` is `"auto"`, `"new"`, `"join"` or `"cut"`.

| | |
|---|---|
| `doc.extrude(what, distance, operation="auto", start=0)` | regions (or a body's `nf.face`) pulled by the distance |
| `doc.revolve(what, axis, degrees=360, operation="auto")` | about `"X"`, `"Y"`, `"Z"`, an `nf.edge` or `(sketch, line)` |
| `doc.fillet(edges, radius)`, `doc.chamfer(edges, size)` | round or bevel edges |
| `doc.shell(open_faces, thickness)` | hollows a body, opening the faces |
| `doc.hole(face, at, diameter, depth=0)` | holes into a flat face (0: through) |
| `doc.base_flange(what, thickness, radius=1, k=0.44, depth=40, flip=False)` | sheet metal from regions, or bent along `(sketch, line)` |
| `doc.flange(edge, length, angle=90, inset=0, inside=False, flip=False)` | a sheet metal flange |
| `doc.hem(edge, length, gap=0.5, flip=False)` | a sheet metal hem |
| `doc.generate(generator, operation="new", **values)` | a generated feature (below) |
| `doc.add_feature(json, sketches={})` | any feature, as a project file has it |

A feature's values can be changed later: `f.set("value", 12)` or `f.set("value", "depth * 2")`;
`f.value(key)` reads one, `f.error` says why it didn't apply. The keys are those of the
project file: `value`, `startOffset`, `hole.diameter`, `sheet.thickness`, `flange.angle`,
`values.<name>` (a generator's), …

## Parameters and configurations

```python
doc.set_parameter("width", 80)                       # a length, in mm
doc.set_parameter("count", 6, kind="number")
doc.set_parameter("depth", "width / 4")              # an expression
doc.parameter("depth")                               # 20.0
doc.parameters                                       # {"width": 80.0, ...}
doc.save_configuration("Large")
doc.apply_configuration("Small")                     # the model follows
doc.configurations                                   # ["Large", "Small"]
```

## Results and export

| | |
|---|---|
| `doc.rebuild()` | builds the model; each feature's error (empty when it applied) |
| `doc.errors` | the errors there are |
| `doc.bodies` | the bodies: `volume`, `area`, `center`, `box` (x0, y0, z0, x1, y1, z1) |
| `doc.export(path)` | STEP, IGES, STL or 3MF, by the file's suffix |
| `doc.export_flat_pattern(path, sheet=0)` | a sheet metal body unfolded, as DXF |

## Generated features

A **generator** is a Python function that makes a solid from its values. Made a feature,
it is kept in the history – its code saved in the project, so the project rebuilds
anywhere – and its values are edited like any other feature's (double tap it), as
numbers or expressions of the parameters.

```python
@nf.generator("Washer", outer=nf.mm(16), inner=nf.mm(8.4), thickness=nf.mm(1.6))
def washer(outer, inner, thickness):
    return nf.circle(outer / 2).extrude(thickness).cut(nf.circle(inner / 2).extrude(thickness))

doc.set_parameter("bolt", 8)
doc.generate(washer, inner="bolt + 0.4")
```

- Each value's default gives its kind: `nf.mm(…)` a length, `nf.deg(…)` an angle,
  `nf.number(…)` (or a plain number) a number. They come to the function as floats.
- The function must **stand on its own**: it may use `math` and `nibform` (also as `nf`);
  helpers go inside it. It returns a solid; it must not change documents.
- Generators of plugins are under *Scripts → Insert Generated Feature*.

Shapes for generators (on the XY plane unless moved):

| | |
|---|---|
| `nf.polygon(points)`, `nf.spline(points)` | a face bounded by lines / a smooth closed curve through `(x, y)` points |
| `nf.circle(radius, center=(0, 0))`, `nf.rectangle(width, height, center=(0, 0))` | faces |
| `nf.Path(start).line_to(p).arc_to(through, p).curve_through(points)…close()` | a face drawn piece by piece |
| `nf.box(x, y, z, corner=(0, 0, 0))`, `nf.cylinder(radius, height, base=(0, 0, 0))` | solids |
| `shape.extrude(height)`, `shape.revolve(degrees=360, axis="Y")` | a face made solid |
| `a.fuse(b)`, `a.cut(b)`, `a.common(b)` (also `a \| b`, `a - b`, `a & b`) | booleans |
| `shape.moved(x, y, z)`, `shape.rotated(degrees, axis="Z", center=(0, 0, 0))` | placed elsewhere |
| `shape.volume`, `shape.area` | measures |

## Commands and plugins

Every `.py` file in the **plugin folder** (*Scripts → Plugins → Open Plugin Folder*;
`~/.local/share/Nibform/Nibform/plugins` on Linux) runs when the app starts (and on *Reload
Plugins*). A plugin adds commands and generators:

```python
import nibform as nf

@nf.command("Add a 10 mm cube", help="A cube at the origin")
def add_cube():
    doc = nf.document()
    s = doc.sketch("XY")
    s.rectangle(0, 0, 10, 10)
    doc.extrude(s, 10)
```

Commands are under *Scripts → Plugins*, on a *Plugins* button of the model bar and in the
radial menu. A file that adds neither is a command itself: it runs when picked.

## Macros

*Scripts → Record Macro* (or the console's button) starts recording; do the work, then stop
it: what was added to the project – sketches, features, parameters – is saved as a script
that adds it again. Saved in the plugin folder, it is a command at once.

## Scripts in projects from elsewhere

A generated feature's code is kept in the project file, and runs with your rights when the
model is built. So when a project with scripts comes from elsewhere (opened, or restored
after a crash), Nibform asks before running them; until you allow it (*Scripts → Trust This
Project's Scripts*), those features are left out of the model. Scripts you wrote or ran in
this session, and everything run with `nibform --run`, are trusted.

## Examples

In `examples/` (installed with the docs):

- **`gear.py`** – a spur gear from its module and tooth count as a generated feature; two
  parameters drive it. `nibform --run gear.py gear.step` exports it.
- **`export_configurations.py`** – every configuration of a project to STEP:
  `nibform --run export_configurations.py part.nibform out/`.
