#include "script/ScriptEngine.h"

#include "io/ProjectFile.h"
#include "model/Parameters.h"
#include "model/Profiles.h"
#include "model/Solids.h"
#include "model/Tr.h"
#include "sketch/Solver.h"

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>

namespace py = pybind11;

namespace {

constexpr double kPi = 3.14159265358979323846;

using DocPtr = std::shared_ptr<ScriptDocument>;

// Python objects the engine keeps: dropped before Python ends.
struct PyState
{
    py::dict scopes;    // the console's variables, by scope name
    py::dict commands;  // id → callable or file path
    py::dict compiled;  // generators' functions, by source and name
    py::object module;  // nibform
};
PyState* g_py = nullptr;

std::size_t hashOf(const std::string& s)
{
    return std::hash<std::string>{}(s);
}

// --- Values and picks -------------------------------------------------------------------

struct PyValue
{
    double value = 0.0;
    ValueKind kind = ValueKind::Number;
};

ValueKind kindOf(const std::string& name)
{
    if (name == "length" || name == "mm")
        return ValueKind::Length;
    if (name == "angle" || name == "deg")
        return ValueKind::Angle;
    if (name == "number")
        return ValueKind::Number;
    throw py::value_error("A value's kind is \"length\", \"angle\" or \"number\", not \"" + name + "\"");
}

const char* kindName(ValueKind k)
{
    return k == ValueKind::Length ? "length" : k == ValueKind::Angle ? "angle" : "number";
}

gp_Pnt point3(py::handle h)
{
    const auto v = h.cast<std::vector<double>>();
    if (v.size() != 3)
        throw py::value_error("A point in space is (x, y, z)");
    return gp_Pnt(v[0], v[1], v[2]);
}

gp_Dir dir3(py::handle h)
{
    const gp_Pnt p = point3(h);
    const gp_Vec v(p.XYZ());
    if (v.Magnitude() < 1e-12)
        throw py::value_error("A direction can't be (0, 0, 0)");
    return gp_Dir(v);
}

gp_Pnt2d point2(py::handle h)
{
    const auto v = h.cast<std::vector<double>>();
    if (v.size() != 2)
        throw py::value_error("A sketch point is (x, y)");
    return gp_Pnt2d(v[0], v[1]);
}

struct PyEdge
{
    EdgePick pick;
};
struct PyFace
{
    FacePick pick;
};
struct PyRegion
{
    int sketch = -1;
    gp_Pnt2d inside;
};

// --- Shapes (what generators make) --------------------------------------------------------

struct PyShape
{
    TopoDS_Shape shape;
};

TopoDS_Shape tidy(const TopoDS_Shape& s)
{
    ShapeUpgrade_UnifySameDomain unify(s, true, true, false);
    unify.Build();
    return unify.Shape();
}

PyShape faceFromWire(BRepBuilderAPI_MakeWire& wire)
{
    if (!wire.IsDone())
        throw py::value_error("The outline doesn't close");
    BRepBuilderAPI_MakeFace face(gp_Pln(gp::Origin(), gp::DZ()), wire.Wire(), true);
    if (!face.IsDone())
        throw py::value_error("The outline doesn't make a face");
    return {face.Face()};
}

PyShape polygon(const std::vector<std::vector<double>>& pts)
{
    if (pts.size() < 3)
        throw py::value_error("A polygon needs at least 3 points");
    std::vector<gp_Pnt> p;
    for (const auto& q : pts) {
        if (q.size() != 2)
            throw py::value_error("A polygon's points are (x, y)");
        if (p.empty() || p.back().Distance(gp_Pnt(q[0], q[1], 0)) > 1e-9)
            p.emplace_back(q[0], q[1], 0.0);
    }
    if (p.front().Distance(p.back()) < 1e-9)
        p.pop_back();
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < p.size(); ++i)
        wire.Add(BRepBuilderAPI_MakeEdge(p[i], p[(i + 1) % p.size()]).Edge());
    return faceFromWire(wire);
}

PyShape splineFace(const std::vector<std::vector<double>>& pts)
{
    if (pts.size() < 3)
        throw py::value_error("A closed curve needs at least 3 points");
    Handle(TColgp_HArray1OfPnt) array = new TColgp_HArray1OfPnt(1, static_cast<int>(pts.size()));
    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (pts[i].size() != 2)
            throw py::value_error("A curve's points are (x, y)");
        array->SetValue(static_cast<int>(i) + 1, gp_Pnt(pts[i][0], pts[i][1], 0.0));
    }
    GeomAPI_Interpolate interpolate(array, true, 1e-9);
    interpolate.Perform();
    if (!interpolate.IsDone())
        throw py::value_error("Can't make a smooth curve through the points");
    BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge());
    return faceFromWire(wire);
}

PyShape circleFace(double r, const std::vector<double>& c)
{
    if (r <= 0.0)
        throw py::value_error("A circle's radius must be more than 0");
    const gp_Pnt center(c.size() > 0 ? c[0] : 0.0, c.size() > 1 ? c[1] : 0.0, 0.0);
    BRepBuilderAPI_MakeWire wire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center, gp::DZ()), r)).Edge());
    return faceFromWire(wire);
}

// An outline drawn piece by piece from a start point: lines, arcs and smooth curves.
struct PyPath
{
    gp_Pnt start, at;
    std::vector<TopoDS_Edge> edges;
};

gp_Pnt flat(const std::vector<double>& p)
{
    if (p.size() != 2)
        throw py::value_error("A point of a path is (x, y)");
    return gp_Pnt(p[0], p[1], 0.0);
}

void lineTo(PyPath& path, const gp_Pnt& to)
{
    if (path.at.Distance(to) < 1e-9)
        return;
    path.edges.push_back(BRepBuilderAPI_MakeEdge(path.at, to).Edge());
    path.at = to;
}

PyShape closePath(PyPath& path)
{
    lineTo(path, path.start);
    if (path.edges.empty())
        throw py::value_error("The path is empty");
    BRepBuilderAPI_MakeWire wire;
    for (const TopoDS_Edge& e : path.edges)
        wire.Add(e);
    return faceFromWire(wire);
}

PyShape booleanOf(const PyShape& a, const PyShape& b, int op)
{
    TopoDS_Shape out;
    try {
        if (op == 0) {
            BRepAlgoAPI_Fuse f(a.shape, b.shape);
            if (!f.IsDone() || f.HasErrors())
                throw py::value_error("Couldn't join the shapes");
            out = f.Shape();
        } else if (op == 1) {
            BRepAlgoAPI_Cut c(a.shape, b.shape);
            if (!c.IsDone() || c.HasErrors())
                throw py::value_error("Couldn't cut the shapes");
            out = c.Shape();
        } else {
            BRepAlgoAPI_Common c(a.shape, b.shape);
            if (!c.IsDone() || c.HasErrors())
                throw py::value_error("Couldn't intersect the shapes");
            out = c.Shape();
        }
    } catch (const Standard_Failure& e) {
        throw py::value_error(std::string("Geometry error: ") + (e.GetMessageString() ? e.GetMessageString() : ""));
    }
    return {tidy(out)};
}

PyShape transformed(const PyShape& s, const gp_Trsf& t)
{
    return {BRepBuilderAPI_Transform(s.shape, t, true).Shape()};
}

PyShape extruded(const PyShape& s, double height)
{
    // Faces along their plane's normal (Z for those made here).
    gp_Dir along = gp::DZ();
    for (TopExp_Explorer it(s.shape, TopAbs_FACE); it.More(); it.Next()) {
        if (const auto n = planarNormal(TopoDS::Face(it.Current()))) {
            along = *n;
            break;
        }
    }
    if (std::abs(height) < 1e-12)
        throw py::value_error("Extrude by more than 0");
    return {BRepPrimAPI_MakePrism(s.shape, gp_Vec(along) * height).Shape()};
}

gp_Ax1 axisOf(const std::string& axis, const std::vector<double>& c)
{
    const gp_Pnt center(c.size() > 0 ? c[0] : 0.0, c.size() > 1 ? c[1] : 0.0, c.size() > 2 ? c[2] : 0.0);
    if (axis == "X" || axis == "x")
        return gp_Ax1(center, gp::DX());
    if (axis == "Y" || axis == "y")
        return gp_Ax1(center, gp::DY());
    if (axis == "Z" || axis == "z")
        return gp_Ax1(center, gp::DZ());
    throw py::value_error("An axis is \"X\", \"Y\" or \"Z\"");
}

// --- The document -----------------------------------------------------------------------

struct PyDocument
{
    DocPtr doc;
};

struct PySketch
{
    DocPtr doc;
    int id = -1;
    Sketch& get() const
    {
        Sketch* s = doc->sketch(id);
        if (!s)
            throw py::value_error("The sketch is gone");
        return *s;
    }
};

struct PyFeature
{
    DocPtr doc;
    int index = -1;
    Feature& get() const
    {
        if (index < 0 || index >= static_cast<int>(doc->features().size()))
            throw py::index_error("The feature is gone");
        return doc->features()[index];
    }
};

struct PyBody
{
    TopoDS_Shape shape;
};

BodyOperation operationOf(const std::string& op)
{
    if (op == "auto")
        return BodyOperation::Auto;
    if (op == "new")
        return BodyOperation::NewBody;
    if (op == "join")
        return BodyOperation::Join;
    if (op == "cut")
        return BodyOperation::Cut;
    throw py::value_error("An operation is \"auto\", \"new\", \"join\" or \"cut\"");
}

// A feature's value: a number, or an expression of the parameters (a string).
void setValue(Feature& f, const std::string& key, py::handle v)
{
    if (!featureField(f, key))
        throw py::value_error("This feature has no value \"" + key + "\"");
    if (py::isinstance<py::str>(v)) {
        f.expressions[key] = Expression{v.cast<std::string>(), LengthUnit::Millimetre};
        return;
    }
    f.expressions.erase(key);
    setFeatureValue(f, key, v.cast<double>());
}

int addFeature(const DocPtr& doc, Feature f)
{
    const int index = doc->addFeature(std::move(f));
    return index;
}

gp_Ax3 basePlane(const std::string& name)
{
    const int id = name == "XY" ? kBaseXY : name == "XZ" ? kBaseXZ : name == "YZ" ? kBaseYZ : 0;
    if (id == 0)
        throw py::value_error("A base plane is \"XY\", \"XZ\" or \"YZ\"");
    return baseDatum(id)->frame;
}

std::vector<RegionPick> regionsOf(const DocPtr& doc, py::handle what)
{
    std::vector<RegionPick> out;
    auto one = [&](py::handle h) {
        if (py::isinstance<PyRegion>(h)) {
            const PyRegion r = h.cast<PyRegion>();
            out.push_back({r.sketch, r.inside});
        } else if (py::isinstance<PySketch>(h)) {
            const PySketch s = h.cast<PySketch>();
            const Sketch& sketch = s.get();
            for (const TopoDS_Face& face : findRegions(sketch)) {
                if (const auto inside = insidePoint(sketch, face))
                    out.push_back({s.id, *inside});
            }
        } else {
            throw py::type_error("Give regions (sketch.region(x, y)) or a sketch (all its regions)");
        }
    };
    if (py::isinstance<py::list>(what) || py::isinstance<py::tuple>(what)) {
        for (py::handle h : what)
            one(h);
    } else {
        one(what);
    }
    if (out.empty())
        throw py::value_error("No closed regions to use");
    return out;
}

AxisRef axisRef(py::handle a)
{
    AxisRef ref;
    if (py::isinstance<py::str>(a)) {
        const std::string s = a.cast<std::string>();
        ref.kind = AxisRef::Kind::Datum;
        ref.datum = s == "X" ? kBaseX : s == "Y" ? kBaseY : s == "Z" ? kBaseZ : 0;
        if (ref.datum == 0)
            throw py::value_error("An axis is \"X\", \"Y\", \"Z\", an edge or (sketch, line)");
        return ref;
    }
    if (py::isinstance<PyEdge>(a)) {
        ref.kind = AxisRef::Kind::Edge;
        ref.edge = a.cast<PyEdge>().pick;
        return ref;
    }
    const auto pair = a.cast<py::tuple>();
    if (pair.size() == 2 && py::isinstance<PySketch>(pair[0])) {
        ref.kind = AxisRef::Kind::SketchLine;
        ref.sketch = pair[0].cast<PySketch>().id;
        ref.entity = pair[1].cast<int>();
        return ref;
    }
    throw py::type_error("An axis is \"X\", \"Y\", \"Z\", an edge or (sketch, line)");
}

PointId pointArg(Sketch& s, py::handle h)
{
    if (py::isinstance<py::int_>(h)) {
        const int id = h.cast<int>();
        if (!s.point(id))
            throw py::value_error("No point " + std::to_string(id) + " in the sketch");
        return id;
    }
    return s.pointAt(point2(h));
}

SketchConstraint relation(ConstraintKind kind)
{
    SketchConstraint c;
    c.kind = kind;
    return c;
}

// A dimension's value: a number, or an expression of the parameters.
void dimensionValue(SketchConstraint& c, py::handle v, bool angle)
{
    if (py::isinstance<py::str>(v)) {
        c.expression = Expression{v.cast<std::string>(), LengthUnit::Millimetre};
        c.value = 0.0;
    } else {
        c.value = v.cast<double>() * (angle ? kPi / 180.0 : 1.0);
    }
}

int constrain(const PySketch& ps, SketchConstraint c)
{
    Sketch& s = ps.get();
    const int id = s.addConstraint(c);
    if (!c.expression.empty())
        ps.doc->applyParameters();
    solve(s);
    ps.doc->touch();
    return id;
}

std::string exceptionText(const py::error_already_set& e)
{
    return e.what();
}

} // namespace

// --- The Python module ------------------------------------------------------------------

PYBIND11_EMBEDDED_MODULE(_nibform, m)
{
    m.doc() = "Nibform's scripting core; use the nibform module";

    py::class_<PyValue>(m, "Value")
        .def(py::init([](double v, const std::string& kind) { return PyValue{v, kindOf(kind)}; }))
        .def_readonly("value", &PyValue::value)
        .def_property_readonly("kind", [](const PyValue& v) { return kindName(v.kind); })
        .def("__float__", [](const PyValue& v) { return v.value; })
        .def("__repr__", [](const PyValue& v) { return std::to_string(v.value) + " (" + kindName(v.kind) + ")"; });

    py::class_<PyEdge>(m, "Edge", "An edge of a body, found by its middle and direction")
        .def(py::init([](py::handle middle, py::handle direction) { return PyEdge{{point3(middle), dir3(direction)}}; }),
             py::arg("middle"), py::arg("direction"));
    py::class_<PyFace>(m, "Face", "A face of a body, found by its middle and outward normal")
        .def(py::init([](py::handle middle, py::handle normal) { return PyFace{{point3(middle), dir3(normal)}}; }),
             py::arg("middle"), py::arg("normal"));
    py::class_<PyRegion>(m, "Region", "A closed region of a sketch, by a point inside it")
        .def_readonly("sketch", &PyRegion::sketch)
        .def_property_readonly("inside", [](const PyRegion& r) { return std::make_tuple(r.inside.X(), r.inside.Y()); });

    py::class_<PyShape>(m, "Shape", "A face or solid made by a generator")
        .def("extrude", &extruded, py::arg("height"), "A face pulled into a solid along its normal")
        .def("revolve", [](const PyShape& s, double degrees, const std::string& axis, const std::vector<double>& center) {
            return PyShape{BRepPrimAPI_MakeRevol(s.shape, axisOf(axis, center), degrees * kPi / 180.0).Shape()};
        }, py::arg("degrees") = 360.0, py::arg("axis") = "Y", py::arg("center") = std::vector<double>{})
        .def("fuse", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 0); })
        .def("cut", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 1); })
        .def("common", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 2); })
        .def("__or__", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 0); })
        .def("__sub__", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 1); })
        .def("__and__", [](const PyShape& a, const PyShape& b) { return booleanOf(a, b, 2); })
        .def("moved", [](const PyShape& s, double x, double y, double z) {
            gp_Trsf t;
            t.SetTranslation(gp_Vec(x, y, z));
            return transformed(s, t);
        }, py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 0.0)
        .def("rotated", [](const PyShape& s, double degrees, const std::string& axis, const std::vector<double>& center) {
            gp_Trsf t;
            t.SetRotation(axisOf(axis, center), degrees * kPi / 180.0);
            return transformed(s, t);
        }, py::arg("degrees"), py::arg("axis") = "Z", py::arg("center") = std::vector<double>{})
        .def_property_readonly("volume", [](const PyShape& s) { return volumeOf(s.shape); })
        .def_property_readonly("area", [](const PyShape& s) {
            GProp_GProps p;
            BRepGProp::SurfaceProperties(s.shape, p);
            return p.Mass();
        });
    py::class_<PyPath>(m, "Path", "An outline drawn from a start point: lines, arcs and smooth curves; close() makes it a face")
        .def(py::init([](const std::vector<double>& start) {
            PyPath p;
            p.start = p.at = flat(start);
            return p;
        }), py::arg("start"))
        .def("line_to", [](PyPath& p, const std::vector<double>& to) -> PyPath& {
            lineTo(p, flat(to));
            return p;
        }, py::arg("point"), py::return_value_policy::reference_internal)
        .def("arc_to", [](PyPath& p, const std::vector<double>& through, const std::vector<double>& to) -> PyPath& {
            const gp_Pnt m = flat(through), e = flat(to);
            GC_MakeArcOfCircle arc(p.at, m, e);
            if (!arc.IsDone())
                throw py::value_error("Those points make no arc");
            p.edges.push_back(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
            p.at = e;
            return p;
        }, py::arg("through"), py::arg("point"), "An arc through a point to another", py::return_value_policy::reference_internal)
        .def("curve_through", [](PyPath& p, const std::vector<std::vector<double>>& pts) -> PyPath& {
            if (pts.empty())
                return p;
            Handle(TColgp_HArray1OfPnt) array = new TColgp_HArray1OfPnt(1, static_cast<int>(pts.size()) + 1);
            array->SetValue(1, p.at);
            for (std::size_t i = 0; i < pts.size(); ++i)
                array->SetValue(static_cast<int>(i) + 2, flat(pts[i]));
            GeomAPI_Interpolate interpolate(array, false, 1e-9);
            interpolate.Perform();
            if (!interpolate.IsDone())
                throw py::value_error("Can't make a smooth curve through the points");
            p.edges.push_back(BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge());
            p.at = flat(pts.back());
            return p;
        }, py::arg("points"), "A smooth curve from where the path is through the points", py::return_value_policy::reference_internal)
        .def_property_readonly("point", [](const PyPath& p) { return std::make_tuple(p.at.X(), p.at.Y()); })
        .def("close", &closePath, "Back to the start with a line (if not there), as a flat face");
    m.def("polygon", &polygon, py::arg("points"), "A flat face bounded by straight lines through the points (x, y)");
    m.def("spline", &splineFace, py::arg("points"), "A flat face bounded by a smooth closed curve through the points");
    m.def("circle", &circleFace, py::arg("radius"), py::arg("center") = std::vector<double>{0.0, 0.0});
    m.def("rectangle", [](double w, double h, const std::vector<double>& c) {
        const double x = c.size() > 0 ? c[0] : 0.0, y = c.size() > 1 ? c[1] : 0.0;
        return polygon({{x - w / 2, y - h / 2}, {x + w / 2, y - h / 2}, {x + w / 2, y + h / 2}, {x - w / 2, y + h / 2}});
    }, py::arg("width"), py::arg("height"), py::arg("center") = std::vector<double>{0.0, 0.0});
    m.def("box", [](double x, double y, double z, const std::vector<double>& c) {
        const gp_Pnt corner(c.size() > 0 ? c[0] : 0.0, c.size() > 1 ? c[1] : 0.0, c.size() > 2 ? c[2] : 0.0);
        return PyShape{BRepPrimAPI_MakeBox(corner, x, y, z).Shape()};
    }, py::arg("x"), py::arg("y"), py::arg("z"), py::arg("corner") = std::vector<double>{0.0, 0.0, 0.0});
    m.def("cylinder", [](double r, double h, const std::vector<double>& c) {
        const gp_Pnt base(c.size() > 0 ? c[0] : 0.0, c.size() > 1 ? c[1] : 0.0, c.size() > 2 ? c[2] : 0.0);
        return PyShape{BRepPrimAPI_MakeCylinder(gp_Ax2(base, gp::DZ()), r, h).Shape()};
    }, py::arg("radius"), py::arg("height"), py::arg("base") = std::vector<double>{0.0, 0.0, 0.0});

    py::class_<PyBody>(m, "Body", "A body of the model as it was last built")
        .def_property_readonly("volume", [](const PyBody& b) { return volumeOf(b.shape); })
        .def_property_readonly("area", [](const PyBody& b) {
            GProp_GProps p;
            BRepGProp::SurfaceProperties(b.shape, p);
            return p.Mass();
        })
        .def_property_readonly("center", [](const PyBody& b) {
            GProp_GProps p;
            BRepGProp::VolumeProperties(b.shape, p);
            const gp_Pnt c = p.CentreOfMass();
            return std::make_tuple(c.X(), c.Y(), c.Z());
        })
        .def_property_readonly("box", [](const PyBody& b) {
            Bnd_Box box;
            BRepBndLib::AddOptimal(b.shape, box, false, false);
            double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
            if (!box.IsVoid())
                box.Get(x0, y0, z0, x1, y1, z1);
            return std::make_tuple(x0, y0, z0, x1, y1, z1);
        })
        .def_property_readonly("shape", [](const PyBody& b) { return PyShape{b.shape}; });

    py::class_<PySketch>(m, "Sketch", "A sketch of the document")
        .def_readonly("id", &PySketch::id)
        .def("point", [](const PySketch& s, double x, double y) {
            const int id = s.get().addPoint({x, y});
            s.doc->touch();
            return id;
        }, py::arg("x"), py::arg("y"))
        .def("line", [](const PySketch& s, py::handle a, py::handle b) {
            Sketch& k = s.get();
            const int id = k.addLine(pointArg(k, a), pointArg(k, b));
            s.doc->touch();
            return id;
        }, py::arg("a"), py::arg("b"), "A line between two points (ids or (x, y)); its id")
        .def("polyline", [](const PySketch& s, py::list pts, bool closed) {
            Sketch& k = s.get();
            std::vector<PointId> ids;
            for (py::handle h : pts)
                ids.push_back(pointArg(k, h));
            std::vector<int> lines;
            for (std::size_t i = 0; i + 1 < ids.size(); ++i)
                lines.push_back(k.addLine(ids[i], ids[i + 1]));
            if (closed && ids.size() > 2)
                lines.push_back(k.addLine(ids.back(), ids.front()));
            s.doc->touch();
            return lines;
        }, py::arg("points"), py::arg("closed") = false, "Lines through the points in turn; their ids")
        .def("rectangle", [](const PySketch& s, double x0, double y0, double x1, double y1) {
            Sketch& k = s.get();
            const PointId a = k.pointAt({x0, y0}), b = k.pointAt({x1, y0}), c = k.pointAt({x1, y1}), d = k.pointAt({x0, y1});
            std::vector<int> lines{k.addLine(a, b), k.addLine(b, c), k.addLine(c, d), k.addLine(d, a)};
            s.doc->touch();
            return lines;
        }, py::arg("x0"), py::arg("y0"), py::arg("x1"), py::arg("y1"), "Four lines (bottom, right, top, left)")
        .def("circle", [](const PySketch& s, py::handle center, double radius) {
            Sketch& k = s.get();
            const int id = k.addCircle(pointArg(k, center), radius);
            s.doc->touch();
            return id;
        }, py::arg("center"), py::arg("radius"))
        .def("arc", [](const PySketch& s, py::handle center, py::handle start, py::handle end) {
            Sketch& k = s.get();
            const int id = k.addArc(pointArg(k, center), pointArg(k, start), pointArg(k, end));
            s.doc->touch();
            return id;
        }, py::arg("center"), py::arg("start"), py::arg("end"), "Counterclockwise from start to end")
        .def("spline", [](const PySketch& s, py::list pts, bool closed) {
            Sketch& k = s.get();
            std::vector<PointId> ids;
            for (py::handle h : pts)
                ids.push_back(pointArg(k, h));
            const int id = k.addSpline(ids, closed);
            s.doc->touch();
            return id;
        }, py::arg("points"), py::arg("closed") = false)
        .def("text", [](const PySketch& s, py::handle at, const std::string& text, double height) {
            Sketch& k = s.get();
            const int id = k.addText(pointArg(k, at), text, height);
            s.doc->touch();
            return id;
        }, py::arg("at"), py::arg("text"), py::arg("height") = 10.0)
        .def("construction", [](const PySketch& s, int entity, bool on) {
            s.get().setConstruction(entity, on);
            s.doc->touch();
        }, py::arg("entity"), py::arg("on") = true)
        .def("ends", [](const PySketch& s, int entity) {
            const SketchEntity* e = s.get().entity(entity);
            if (!e)
                throw py::value_error("No curve " + std::to_string(entity));
            return std::make_tuple(e->a, e->b);
        }, py::arg("entity"), "The points a curve starts and ends at")
        .def("center", [](const PySketch& s, int entity) {
            const SketchEntity* e = s.get().entity(entity);
            if (!e)
                throw py::value_error("No curve " + std::to_string(entity));
            return e->c;
        }, py::arg("entity"))
        .def("position", [](const PySketch& s, int point) {
            const gp_Pnt2d p = s.get().pos(point);
            return std::make_tuple(p.X(), p.Y());
        }, py::arg("point"))
        // Relations
        .def("horizontal", [](const PySketch& s, int line) {
            const SketchEntity* e = s.get().entity(line);
            if (!e)
                throw py::value_error("No line " + std::to_string(line));
            SketchConstraint c = relation(ConstraintKind::Horizontal);
            c.p1 = e->a;
            c.p2 = e->b;
            c.e1 = line;
            return constrain(s, c);
        }, py::arg("line"))
        .def("vertical", [](const PySketch& s, int line) {
            const SketchEntity* e = s.get().entity(line);
            if (!e)
                throw py::value_error("No line " + std::to_string(line));
            SketchConstraint c = relation(ConstraintKind::Vertical);
            c.p1 = e->a;
            c.p2 = e->b;
            c.e1 = line;
            return constrain(s, c);
        }, py::arg("line"))
        .def("parallel", [](const PySketch& s, int a, int b) {
            SketchConstraint c = relation(ConstraintKind::Parallel);
            c.e1 = a;
            c.e2 = b;
            return constrain(s, c);
        })
        .def("perpendicular", [](const PySketch& s, int a, int b) {
            SketchConstraint c = relation(ConstraintKind::Perpendicular);
            c.e1 = a;
            c.e2 = b;
            return constrain(s, c);
        })
        .def("equal", [](const PySketch& s, int a, int b) {
            SketchConstraint c = relation(ConstraintKind::Equal);
            c.e1 = a;
            c.e2 = b;
            return constrain(s, c);
        })
        .def("concentric", [](const PySketch& s, int a, int b) {
            SketchConstraint c = relation(ConstraintKind::Concentric);
            c.e1 = a;
            c.e2 = b;
            return constrain(s, c);
        })
        .def("tangent", [](const PySketch& s, int a, int b) {
            SketchConstraint c = relation(ConstraintKind::Tangent);
            c.e1 = a;
            c.e2 = b;
            c.value = 1.0;
            return constrain(s, c);
        })
        .def("on", [](const PySketch& s, int point, int entity) {
            SketchConstraint c = relation(ConstraintKind::PointOnEntity);
            c.p1 = point;
            c.e1 = entity;
            return constrain(s, c);
        }, py::arg("point"), py::arg("entity"))
        .def("midpoint", [](const PySketch& s, int point, int line) {
            SketchConstraint c = relation(ConstraintKind::Midpoint);
            c.p1 = point;
            c.e1 = line;
            return constrain(s, c);
        }, py::arg("point"), py::arg("line"))
        .def("fix", [](const PySketch& s, int point) {
            SketchConstraint c = relation(ConstraintKind::Fixed);
            c.p1 = point;
            c.target = s.get().pos(point);
            return constrain(s, c);
        }, py::arg("point"))
        .def("coincident", [](const PySketch& s, int keep, int drop) {
            if (!s.get().mergePoints(keep, drop))
                throw py::value_error("Those points can't be one");
            solve(s.get());
            s.doc->touch();
        }, py::arg("keep"), py::arg("drop"))
        .def("symmetric", [](const PySketch& s, int a, int b, int line) {
            SketchConstraint c = relation(ConstraintKind::Symmetric);
            c.p1 = a;
            c.p2 = b;
            c.e1 = line;
            return constrain(s, c);
        }, py::arg("a"), py::arg("b"), py::arg("line"))
        // Dimensions: a number, or an expression of the parameters ("width / 2")
        .def("distance", [](const PySketch& s, int a, int b, py::handle value) {
            SketchConstraint c = relation(ConstraintKind::Distance);
            c.p1 = a;
            c.p2 = b;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("a"), py::arg("b"), py::arg("value"))
        .def("length", [](const PySketch& s, int line, py::handle value) {
            const SketchEntity* e = s.get().entity(line);
            if (!e)
                throw py::value_error("No line " + std::to_string(line));
            SketchConstraint c = relation(ConstraintKind::Distance);
            c.p1 = e->a;
            c.p2 = e->b;
            c.e1 = line;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("line"), py::arg("value"))
        .def("horizontal_distance", [](const PySketch& s, int a, int b, py::handle value) {
            SketchConstraint c = relation(ConstraintKind::HorizontalDistance);
            c.p1 = a;
            c.p2 = b;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("a"), py::arg("b"), py::arg("value"))
        .def("vertical_distance", [](const PySketch& s, int a, int b, py::handle value) {
            SketchConstraint c = relation(ConstraintKind::VerticalDistance);
            c.p1 = a;
            c.p2 = b;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("a"), py::arg("b"), py::arg("value"))
        .def("radius", [](const PySketch& s, int circle, py::handle value) {
            SketchConstraint c = relation(ConstraintKind::Radius);
            c.e1 = circle;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("circle"), py::arg("value"))
        .def("diameter", [](const PySketch& s, int circle, py::handle value) {
            SketchConstraint c = relation(ConstraintKind::Diameter);
            c.e1 = circle;
            dimensionValue(c, value, false);
            return constrain(s, c);
        }, py::arg("circle"), py::arg("value"))
        .def("angle", [](const PySketch& s, int a, int b, py::handle degrees) {
            SketchConstraint c = relation(ConstraintKind::Angle);
            c.e1 = a;
            c.e2 = b;
            dimensionValue(c, degrees, true);
            return constrain(s, c);
        }, py::arg("a"), py::arg("b"), py::arg("degrees"))
        .def("solve", [](const PySketch& s) {
            const SolveResult r = solve(s.get());
            s.doc->touch();
            return r.ok;
        }, "Solves the relations again; True when they all hold")
        // Regions
        .def("region", [](const PySketch& s, double x, double y) {
            const Sketch& k = s.get();
            const gp_Pnt at = k.to3d(gp_Pnt2d(x, y));
            const std::vector<TopoDS_Face> regions = findRegions(k);
            if (!regionAt(regions, at))
                throw py::value_error("No closed region of the sketch at (" + std::to_string(x) + ", " + std::to_string(y) + ")");
            return PyRegion{s.id, gp_Pnt2d(x, y)};
        }, py::arg("x"), py::arg("y"), "The closed region with (x, y) inside it")
        .def("regions", [](const PySketch& s) {
            const Sketch& k = s.get();
            std::vector<PyRegion> out;
            for (const TopoDS_Face& face : findRegions(k)) {
                if (const auto inside = insidePoint(k, face))
                    out.push_back({s.id, *inside});
            }
            return out;
        }, "Every closed region")
        .def("__repr__", [](const PySketch& s) { return "<Sketch " + std::to_string(s.id) + ">"; });

    py::class_<PyFeature>(m, "Feature", "An operation in the document's history")
        .def_readonly("index", &PyFeature::index)
        .def_property_readonly("id", [](const PyFeature& f) { return f.get().id; })
        .def_property_readonly("kind", [](const PyFeature& f) {
            return QJsonDocument::fromJson(featureToJsonText(f.get())).object().value("kind").toString().toStdString();
        })
        .def("value", [](const PyFeature& f, const std::string& key) {
            const auto v = featureValue(f.get(), key);
            if (!v)
                throw py::value_error("This feature has no value \"" + key + "\"");
            return *v;
        }, py::arg("key") = "value")
        .def("set", [](const PyFeature& f, const std::string& key, py::handle value) {
            setValue(f.get(), key, value);
            f.doc->applyParameters();
        }, py::arg("key"), py::arg("value"), "Sets a value: a number or an expression of the parameters")
        .def_property_readonly("error", [](const PyFeature& f) {
            const std::vector<std::string> errors = f.doc->errors();
            return f.index < static_cast<int>(errors.size()) ? errors[f.index] : std::string();
        })
        .def("to_json", [](const PyFeature& f) { return featureToJsonText(f.get()).toStdString(); })
        .def("__repr__", [](const PyFeature& f) { return "<Feature " + std::to_string(f.index) + ">"; });

    py::class_<PyDocument>(m, "Document", "A project: its sketches, history and parameters")
        .def("sketch", [](const PyDocument& d, py::object plane, double offset) {
            gp_Ax3 ax;
            if (py::isinstance<py::str>(plane)) {
                ax = basePlane(plane.cast<std::string>());
            } else {
                const auto t = plane.cast<py::tuple>();
                if (t.size() < 2)
                    throw py::value_error("A plane is \"XY\", \"XZ\", \"YZ\" or (origin, normal[, x direction])");
                const gp_Dir n = dir3(t[1]);
                const gp_Pnt o = point3(t[0]);
                if (t.size() > 2)
                    ax = gp_Ax3(o, n, dir3(t[2]));
                else
                    ax = gp_Ax3(o, n);
            }
            ax.Translate(gp_Vec(ax.Direction()) * offset);
            return PySketch{d.doc, d.doc->addSketch(ax)};
        }, py::arg("plane") = "XY", py::arg("offset") = 0.0, "A new sketch on a base plane or (origin, normal[, x])")
        .def("sketch_on", [](const PyDocument& d, const PyFace& face) {
            const auto found = findFace(d.doc->bodies(), face.pick);
            if (!found)
                throw py::value_error("No flat face there");
            const BRepAdaptor_Surface surface(found->first);
            GProp_GProps props;
            BRepGProp::SurfaceProperties(found->first, props);
            const gp_Ax3 ax(props.CentreOfMass(), *planarNormal(found->first), surface.Plane().XAxis().Direction());
            PlaneRef on;
            on.kind = PlaneRef::Kind::Face;
            on.face = facePick(found->first);
            return PySketch{d.doc, d.doc->addSketch(ax, on)};
        }, py::arg("face"), "A new sketch on a flat face of a body; it follows the face")
        .def_property_readonly("sketches", [](const PyDocument& d) {
            std::vector<PySketch> out;
            for (const auto& e : d.doc->project().sketches)
                out.push_back({d.doc, e.id});
            return out;
        })
        .def_property_readonly("features", [](const PyDocument& d) {
            std::vector<PyFeature> out;
            for (int i = 0; i < static_cast<int>(d.doc->project().features.size()); ++i)
                out.push_back({d.doc, i});
            return out;
        })
        // Features
        .def("extrude", [](const PyDocument& d, py::handle what, py::handle distance, const std::string& op, py::handle start) {
            Feature f;
            f.kind = Feature::Kind::Extrude;
            if (py::isinstance<PyFace>(what))
                f.faces = {what.cast<PyFace>().pick};
            else
                f.regions = regionsOf(d.doc, what);
            f.operation = operationOf(op);
            setValue(f, "value", distance);
            setValue(f, "startOffset", start);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("what"), py::arg("distance"), py::arg("operation") = "auto", py::arg("start") = 0.0,
           "Pulls sketch regions (or a body's face) by the distance")
        .def("revolve", [](const PyDocument& d, py::handle what, py::handle axis, py::handle degrees, const std::string& op) {
            Feature f;
            f.kind = Feature::Kind::Revolve;
            f.regions = regionsOf(d.doc, what);
            f.axis = axisRef(axis);
            f.operation = operationOf(op);
            setValue(f, "value", degrees);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("what"), py::arg("axis"), py::arg("degrees") = 360.0, py::arg("operation") = "auto")
        .def("fillet", [](const PyDocument& d, const std::vector<PyEdge>& edges, py::handle radius) {
            Feature f;
            f.kind = Feature::Kind::Fillet;
            for (const PyEdge& e : edges)
                f.edges.push_back(e.pick);
            setValue(f, "value", radius);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("edges"), py::arg("radius"))
        .def("chamfer", [](const PyDocument& d, const std::vector<PyEdge>& edges, py::handle size) {
            Feature f;
            f.kind = Feature::Kind::Chamfer;
            for (const PyEdge& e : edges)
                f.edges.push_back(e.pick);
            setValue(f, "value", size);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("edges"), py::arg("size"))
        .def("shell", [](const PyDocument& d, const std::vector<PyFace>& open, py::handle thickness) {
            Feature f;
            f.kind = Feature::Kind::Shell;
            for (const PyFace& p : open)
                f.faces.push_back(p.pick);
            setValue(f, "value", thickness);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("open"), py::arg("thickness"))
        .def("hole", [](const PyDocument& d, const PyFace& face, py::list at, py::handle diameter, py::handle depth) {
            Feature f;
            f.kind = Feature::Kind::Hole;
            f.faces = {face.pick};
            for (py::handle p : at)
                f.points.push_back(point3(p));
            setValue(f, "hole.diameter", diameter);
            // 0 deep: through everything; a depth (or an expression) makes it blind.
            const bool through = !py::isinstance<py::str>(depth) && depth.cast<double>() <= 0.0;
            if (!through) {
                f.hole.depth = 1.0; // its depth then counts
                setValue(f, "hole.depth", depth);
            }
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("face"), py::arg("at"), py::arg("diameter"), py::arg("depth") = 0.0, "Holes into a flat face (0 deep: through)")
        .def("base_flange", [](const PyDocument& d, py::handle what, py::handle thickness, py::handle radius, py::handle k,
                               py::handle depth, bool flip) {
            Feature f;
            f.kind = Feature::Kind::SheetBase;
            f.flip = flip;
            if (py::isinstance<py::tuple>(what) && py::len(what) == 2 && py::isinstance<PySketch>(what.cast<py::tuple>()[0])) {
                const auto t = what.cast<py::tuple>();
                f.pathSketch = t[0].cast<PySketch>().id;
                f.pathEntity = t[1].cast<int>();
                setValue(f, "value", depth);
            } else {
                f.regions = regionsOf(d.doc, what);
            }
            setValue(f, "sheet.thickness", thickness);
            setValue(f, "sheet.radius", radius);
            setValue(f, "sheet.kFactor", k);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("what"), py::arg("thickness"), py::arg("radius") = 1.0, py::arg("k") = 0.44, py::arg("depth") = 40.0,
           py::arg("flip") = false, "A sheet metal plate from regions, or a bent sheet along (sketch, line)")
        .def("flange", [](const PyDocument& d, const PyEdge& edge, py::handle length, py::handle angle, py::handle inset,
                          bool inside, bool flip) {
            Feature f;
            f.kind = Feature::Kind::Flange;
            f.edges = {edge.pick};
            f.flange.inside = inside;
            f.flip = flip;
            setValue(f, "value", length);
            setValue(f, "flange.angle", angle);
            setValue(f, "flange.inset", inset);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("edge"), py::arg("length"), py::arg("angle") = 90.0, py::arg("inset") = 0.0, py::arg("inside") = false,
           py::arg("flip") = false)
        .def("hem", [](const PyDocument& d, const PyEdge& edge, py::handle length, py::handle gap, bool flip) {
            Feature f;
            f.kind = Feature::Kind::Hem;
            f.edges = {edge.pick};
            f.flip = flip;
            setValue(f, "value", length);
            setValue(f, "flange.gap", gap);
            return PyFeature{d.doc, addFeature(d.doc, f)};
        }, py::arg("edge"), py::arg("length"), py::arg("gap") = 0.5, py::arg("flip") = false)
        .def("_generate", [](const PyDocument& d, const std::string& name, const std::string& op, py::dict values) {
            const auto gens = ScriptEngine::instance().generators();
            const auto g = std::find_if(gens.begin(), gens.end(), [&](const ScriptEngine::Generator& x) { return x.name == name; });
            if (g == gens.end())
                throw py::value_error("No generator \"" + name + "\"");
            Feature f;
            f.kind = Feature::Kind::Script;
            f.operation = operationOf(op);
            f.script.source = g->source;
            f.script.name = g->name;
            f.script.values = g->values;
            f.label = g->help;
            for (auto [key, value] : values) {
                const std::string k = key.cast<std::string>();
                if (!f.script.value(k))
                    throw py::value_error(name + " has no value \"" + k + "\"");
                setValue(f, "values." + k, value);
            }
            return PyFeature{d.doc, addFeature(d.doc, f)};
        })
        .def("add_feature", [](const PyDocument& d, const std::string& json, py::dict sketches) {
            QString error;
            auto f = featureFromJsonText(QByteArray::fromStdString(json), &error);
            if (!f)
                throw py::value_error(error.toStdString());
            std::map<int, int> map;
            for (auto [k, v] : sketches)
                map[k.cast<int>()] = v.cast<int>();
            auto remap = [&](int& id) {
                if (const auto it = map.find(id); it != map.end())
                    id = it->second;
            };
            for (RegionPick& r : f->regions)
                remap(r.sketch);
            remap(f->sketch);
            remap(f->pathSketch);
            remap(f->axis.sketch);
            for (SketchPointRef& p : f->holeAt)
                remap(p.sketch);
            f->id = 0; // numbered anew
            if (f->kind == Feature::Kind::Script && f->script.source)
                ScriptEngine::instance().trust(*f->script.source); // written by the user's own macro
            return PyFeature{d.doc, addFeature(d.doc, *f)};
        }, py::arg("json"), py::arg("sketches") = py::dict(), "A feature as a project file has it (what macros replay)")
        .def("add_sketch", [](const PyDocument& d, const std::string& json, py::object entry) {
            QString error;
            const auto e = sketchFromJsonText(QByteArray::fromStdString(json), &error);
            if (!e)
                throw py::value_error(error.toStdString());
            std::optional<PlaneRef> on;
            if (!entry.is_none()) {
                const auto f = featureFromJsonText(QByteArray::fromStdString(entry.cast<std::string>()), &error);
                if (f && f->plane.kind != PlaneRef::Kind::None)
                    on = f->plane;
            }
            const int id = d.doc->addSketch(e->sketch.plane(), on);
            *d.doc->sketch(id) = e->sketch;
            d.doc->touch();
            return PySketch{d.doc, id};
        }, py::arg("json"), py::arg("entry") = py::none())
        // Parameters
        .def("set_parameter", [](const PyDocument& d, const std::string& name, py::handle value, const std::string& kind,
                                 const std::string& comment) {
            ParameterTable& t = d.doc->parameters();
            const ValueKind k = kindOf(kind);
            if (py::isinstance<py::str>(value)) {
                int i = t.indexOf(name);
                if (i < 0) {
                    t.parameters.push_back(Parameter{name, {}, k, comment});
                    i = static_cast<int>(t.parameters.size()) - 1;
                }
                t.parameters[i].expression = Expression{value.cast<std::string>(), LengthUnit::Millimetre};
                t.parameters[i].kind = k;
                if (!comment.empty())
                    t.parameters[i].comment = comment;
            } else {
                ExpressionError error;
                if (!setParameterValue(t, name, k, value.cast<double>(), LengthUnit::Millimetre, &error))
                    throw py::value_error("Can't set " + name + ": " + error.detail);
                if (!comment.empty())
                    t.parameters[t.indexOf(name)].comment = comment;
            }
            const auto issues = d.doc->applyParameters();
            for (const ParameterIssue& issue : issues) {
                if (issue.feature >= 0 || issue.sketch >= 0)
                    py::module_::import("warnings").attr("warn")("A value can't follow its expression: " + issue.error.detail);
            }
        }, py::arg("name"), py::arg("value"), py::arg("kind") = "length", py::arg("comment") = "",
           "Sets a parameter (a number, or an expression of others), adding it if new")
        .def("parameter", [](const PyDocument& d, const std::string& name) {
            const ParameterScope scope(d.doc->parameters());
            const int i = d.doc->parameters().indexOf(name);
            if (i < 0)
                throw py::key_error(name);
            const auto& v = scope.values()[i];
            if (!v.value)
                throw py::value_error(name + " has no value: " + v.error.detail);
            return *v.value;
        }, py::arg("name"))
        .def_property_readonly("parameters", [](const PyDocument& d) {
            const ParameterScope scope(d.doc->parameters());
            py::dict out;
            for (std::size_t i = 0; i < d.doc->parameters().parameters.size(); ++i) {
                const auto& v = scope.values()[i];
                out[py::str(d.doc->parameters().parameters[i].name)] = v.value ? py::cast(*v.value) : py::none();
            }
            return out;
        })
        .def_property_readonly("configurations", [](const PyDocument& d) {
            std::vector<std::string> out;
            for (const Configuration& c : d.doc->parameters().configurations)
                out.push_back(c.name);
            return out;
        })
        .def("save_configuration", [](const PyDocument& d, const std::string& name) {
            saveConfiguration(d.doc->parameters(), name);
            d.doc->touch();
        })
        .def("apply_configuration", [](const PyDocument& d, const std::string& name) {
            if (!applyConfiguration(d.doc->parameters(), name))
                throw py::key_error(name);
            d.doc->applyParameters();
        }, py::arg("name"), "Switches the parameters to a configuration's values (the model follows)")
        // Results
        .def("rebuild", [](const PyDocument& d) { return d.doc->errors(); },
             "Builds the model; each feature's error (empty when it applied)")
        .def_property_readonly("errors", [](const PyDocument& d) {
            std::vector<std::string> out;
            for (const std::string& e : d.doc->errors()) {
                if (!e.empty())
                    out.push_back(e);
            }
            return out;
        })
        .def_property_readonly("bodies", [](const PyDocument& d) {
            std::vector<PyBody> out;
            for (const TopoDS_Shape& s : d.doc->bodies())
                out.push_back({s});
            return out;
        })
        .def("export", [](const PyDocument& d, const std::string& path) {
            const std::string error = d.doc->exportTo(path);
            if (!error.empty())
                throw std::runtime_error(error);
        }, py::arg("path"), "STEP, IGES, STL or 3MF, by the file's suffix")
        .def("export_flat_pattern", [](const PyDocument& d, const std::string& path, int sheet) {
            const std::string error = d.doc->exportFlatPattern(path, sheet);
            if (!error.empty())
                throw std::runtime_error(error);
        }, py::arg("path"), py::arg("sheet") = 0, "A sheet metal body unfolded, as DXF")
        .def("save", [](const PyDocument& d, const std::string& path) {
            const std::string error = d.doc->save(path);
            if (!error.empty())
                throw std::runtime_error(error);
        }, py::arg("path"));

    m.def("document", [] {
        DocPtr doc = ScriptEngine::instance().current();
        if (!doc) {
            doc = std::make_shared<ScriptDocument>();
            ScriptEngine::instance().setCurrent(doc);
        }
        return PyDocument{doc};
    }, "The project being worked on: the window's, or a new one when run headless");
    m.def("new", [] { return PyDocument{std::make_shared<ScriptDocument>()}; }, "A new, empty project");
    m.def("open", [](const std::string& path) {
        QString error;
        auto project = loadProject(QString::fromStdString(path), &error);
        if (!project)
            throw std::runtime_error(error.toStdString());
        return PyDocument{std::make_shared<ScriptDocument>(std::move(*project))};
    }, py::arg("path"), "A project file");

    m.def("_add_generator", [](const std::string& name, const std::string& title, const std::string& source,
                               const std::vector<std::tuple<std::string, double, std::string>>& values, const std::string& help) {
        ScriptEngine::Generator g;
        g.name = name;
        g.source = std::make_shared<const std::string>(source);
        g.help = title;
        (void)help;
        for (const auto& [n, v, k] : values)
            g.values.push_back({n, v, kindOf(k)});
        ScriptEngine::instance().addGenerator(std::move(g));
    });
    m.def("_add_command", [](const std::string& name, const std::string& help, py::object fn) {
        const std::string id = ScriptEngine::instance().addCommand(name, help);
        g_py->commands[py::str(id)] = fn;
    });
}

// --- The engine -------------------------------------------------------------------------

namespace {

// The Python half of the module: values, decorators, and the console's helpers.
constexpr const char* kPrelude = R"PY(
import inspect as _inspect, textwrap as _textwrap
import _nibform
from _nibform import (Document, Sketch, Feature, Body, Shape, Edge, Face, Region, Value, Path,
                      polygon, spline, circle, rectangle, box, cylinder, document, new, open)

__all__ = ["Document", "Sketch", "Feature", "Body", "Shape", "Edge", "Face", "Region", "Value", "Path",
           "polygon", "spline", "circle", "rectangle", "box", "cylinder", "document", "new", "open",
           "mm", "deg", "number", "edge", "face", "generator", "command"]

def mm(value):
    """A length (for a generator's value): shown and typed in millimetres or inches."""
    return Value(float(value), "length")

def deg(value):
    """An angle in degrees (for a generator's value)."""
    return Value(float(value), "angle")

def number(value):
    """A plain number (for a generator's value), such as a count."""
    return Value(float(value), "number")

def edge(middle, direction):
    """An edge of a body: the one nearest `middle` running along `direction`."""
    return Edge(middle, direction)

def face(middle, normal):
    """A face of a body: the one through `middle` facing `normal`."""
    return Face(middle, normal)

def _source(fn):
    lines = _textwrap.dedent(_inspect.getsource(fn)).splitlines()
    while lines and not lines[0].lstrip().startswith(("def ", "async def ")):
        lines.pop(0)  # its decorators
    return "\n".join(lines) + "\n"

def generator(title=None, **defaults):
    """Makes a function a generator: a feature of the history that runs it with its values
    (editable later, and following parameters) and adds the solid it returns.

        @nibform.generator("Spur gear", module=nibform.mm(2), teeth=nibform.number(20))
        def spur_gear(module, teeth): ...

    The function must stand on its own: it can use `math` and `nibform` (as `nf` too),
    and everything else it needs goes inside it."""
    def register(fn):
        values = []
        for name, p in _inspect.signature(fn).parameters.items():
            d = defaults.get(name, p.default)
            if d is _inspect.Parameter.empty:
                raise TypeError(f"{fn.__name__}: give '{name}' a default value")
            kind = d.kind if isinstance(d, Value) else "number"
            values.append((name, float(d), kind))
        _nibform._add_generator(fn.__name__, title or fn.__name__.replace("_", " ").capitalize(),
                                _source(fn), values, _inspect.getdoc(fn) or "")
        fn.generator_name = fn.__name__
        return fn
    if callable(title):
        fn, title = title, None
        return register(fn)
    return register

def command(title=None, help=""):
    """Adds the function (called with no arguments) as a command of the app: under
    Scripts → Plugins, on the toolbar and in the radial menu."""
    def register(fn):
        _nibform._add_command(title or fn.__name__.replace("_", " ").capitalize(), help, fn)
        return fn
    if callable(title):
        fn, title = title, None
        return register(fn)
    return register

def _generate(self, generator, operation="new", **values):
    """Adds a generator's solid to the history (with `operation`), its values as given
    (numbers, or expressions of the parameters as strings) or its defaults."""
    name = getattr(generator, "generator_name", generator)
    return self._generate(name, operation, values)
Document.generate = _generate
)PY";

struct Capture
{
    std::function<void(const std::string&)> fn;
    void write(const std::string& s)
    {
        if (fn)
            fn(s);
    }
    void flush() {}
};

// Its output to the callbacks while it runs.
struct Redirect
{
    py::object oldOut, oldErr;
    Redirect(const ScriptEngine::Output& output)
    {
        py::module_ sys = py::module_::import("sys");
        oldOut = sys.attr("stdout");
        oldErr = sys.attr("stderr");
        py::module_::import("_nibform_io");
        if (output.out)
            sys.attr("stdout") = py::cast(Capture{output.out});
        if (output.err)
            sys.attr("stderr") = py::cast(Capture{output.err});
    }
    ~Redirect()
    {
        py::module_ sys = py::module_::import("sys");
        sys.attr("stdout") = oldOut;
        sys.attr("stderr") = oldErr;
    }
};

} // namespace

PYBIND11_EMBEDDED_MODULE(_nibform_io, m)
{
    py::class_<Capture>(m, "Capture").def("write", &Capture::write).def("flush", &Capture::flush);
}

ScriptEngine& ScriptEngine::instance()
{
    static ScriptEngine engine;
    return engine;
}

ScriptEngine::~ScriptEngine() = default; // stop() runs before the end (Python can't end this late)

bool ScriptEngine::start(std::string* error)
{
    if (m_running)
        return true;
    try {
        py::initialize_interpreter(false); // the app's signals stay its own
        g_py = new PyState;
        py::module_ module = py::module_::import("types").attr("ModuleType")("nibform", "Nibform's scripting API");
        py::module_::import("sys").attr("modules")["nibform"] = module;
        py::exec(kPrelude, module.attr("__dict__"));
        g_py->module = module;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        return false;
    }
    m_running = true;
    // The window's thread lets go of Python; whoever runs something takes it.
    m_threadState = PyEval_SaveThread();
    setScriptRunner([this](const ScriptCall& call, std::string& err) { return generate(call, err); });
    return true;
}

void ScriptEngine::stop()
{
    if (!m_running)
        return;
    setScriptRunner({});
    PyEval_RestoreThread(static_cast<PyThreadState*>(m_threadState));
    m_current.reset();
    delete g_py;
    g_py = nullptr;
    py::finalize_interpreter();
    m_running = false;
}

ScriptEngine::Result ScriptEngine::run(const std::string& code, const std::string& filename,
                                       std::shared_ptr<ScriptDocument> document, const Output& output,
                                       const std::string& scope, const std::vector<std::string>& argv)
{
    if (!m_running)
        return {false, "Python isn't running"};
    py::gil_scoped_acquire gil;
    const std::shared_ptr<ScriptDocument> before = m_current;
    if (document)
        m_current = document;
    Result result;
    try {
        Redirect redirect(output);
        py::dict globals;
        if (!scope.empty()) {
            if (!g_py->scopes.contains(scope)) {
                py::dict fresh;
                fresh["__builtins__"] = py::module_::import("builtins");
                fresh["__name__"] = "__main__";
                g_py->scopes[py::str(scope)] = fresh;
            }
            globals = g_py->scopes[py::str(scope)].cast<py::dict>();
        } else {
            globals["__builtins__"] = py::module_::import("builtins");
            globals["__name__"] = "__main__";
            globals["__file__"] = filename;
        }
        if (!argv.empty())
            py::module_::import("sys").attr("argv") = argv;
        // Functions typed in are found by inspect (generators need their source).
        py::module_::import("linecache").attr("cache")[py::str(filename)] =
            py::make_tuple(code.size(), py::none(), py::module_::import("io").attr("StringIO")(code).attr("readlines")(),
                           filename);
        globals["nf"] = g_py->module;
        globals["nibform"] = g_py->module;
        // An expression in the console shows its value.
        py::object compiled;
        bool expression = false;
        if (!scope.empty()) {
            try {
                compiled = py::module_::import("builtins").attr("compile")(code, filename, "eval");
                expression = true;
            } catch (const py::error_already_set&) {
                expression = false;
            }
        }
        if (!expression)
            compiled = py::module_::import("builtins").attr("compile")(code, filename, "exec");
        py::object value = py::reinterpret_steal<py::object>(PyEval_EvalCode(compiled.ptr(), globals.ptr(), globals.ptr()));
        if (!value)
            throw py::error_already_set();
        if (expression && !value.is_none() && output.out)
            output.out(py::repr(value).cast<std::string>() + "\n");
    } catch (py::error_already_set& e) {
        if (e.matches(PyExc_SystemExit)) {
            // sys.exit(): 0 or None ends well, a number is the exit code, a text the error.
            const py::object code = e.value().attr("code");
            if (code.is_none()) {
                result = {};
            } else if (py::isinstance<py::int_>(code)) {
                result.exitCode = code.cast<int>();
                result.ok = result.exitCode == 0;
            } else {
                result = {false, py::str(code).cast<std::string>(), 1};
            }
        } else {
            result = {false, exceptionText(e), 1};
        }
    } catch (const std::exception& e) {
        result = {false, e.what()};
    }
    if (document)
        m_current = before ? before : document;
    return result;
}

ScriptEngine::Result ScriptEngine::runFile(const std::string& path, std::shared_ptr<ScriptDocument> document,
                                           const Output& output, const std::vector<std::string>& argv)
{
    std::ifstream in(path);
    if (!in)
        return {false, "Can't read " + path};
    std::stringstream code;
    code << in.rdbuf();
    std::vector<std::string> args = argv;
    if (args.empty())
        args.push_back(path);
    return run(code.str(), path, std::move(document), output, {}, args);
}

bool ScriptEngine::complete(const std::string& code)
{
    if (!m_running)
        return true;
    py::gil_scoped_acquire gil;
    try {
        py::object c = py::module_::import("codeop").attr("compile_command")(code, "<console>", "single");
        return !c.is_none();
    } catch (const py::error_already_set&) {
        return true; // an error: run it, to show it
    }
}

void ScriptEngine::addGenerator(Generator g)
{
    if (g.source)
        trust(*g.source);
    std::erase_if(m_generators, [&](const Generator& x) { return x.name == g.name; });
    m_generators.push_back(std::move(g));
}

std::optional<TopoDS_Shape> ScriptEngine::generate(const ScriptCall& call, std::string& error)
{
    if (!m_running || !call.source) {
        error = QT_TRANSLATE_NOOP("Model", "Scripts aren't running");
        return std::nullopt;
    }
    if (!trusted(*call.source)) {
        error = QT_TRANSLATE_NOOP("Model", "The project's scripts aren't trusted yet: allow them to run (Scripts → Trust This Project's Scripts)");
        return std::nullopt;
    }
    py::gil_scoped_acquire gil;
    try {
        const std::string key = std::to_string(hashOf(*call.source)) + ":" + call.name;
        py::object fn;
        if (g_py->compiled.contains(key)) {
            fn = g_py->compiled[py::str(key)];
        } else {
            py::dict globals;
            globals["__builtins__"] = py::module_::import("builtins");
            globals["math"] = py::module_::import("math");
            globals["nf"] = g_py->module;
            globals["nibform"] = g_py->module;
            py::exec(*call.source, globals);
            if (!globals.contains(call.name)) {
                error = "The script has no function " + call.name;
                return std::nullopt;
            }
            fn = globals[py::str(call.name)];
            g_py->compiled[py::str(key)] = fn;
        }
        py::dict kwargs;
        for (const ScriptValue& v : call.values)
            kwargs[py::str(v.name)] = v.value;
        py::object made = fn(**kwargs);
        if (!py::isinstance<PyShape>(made)) {
            error = call.name + " didn't return a shape";
            return std::nullopt;
        }
        return made.cast<PyShape>().shape;
    } catch (const py::error_already_set& e) {
        // The last line says what went wrong.
        std::string text = e.what();
        const auto cut = text.find('\n');
        error = call.name + ": " + (cut == std::string::npos ? text : text.substr(0, cut));
        return std::nullopt;
    } catch (const std::exception& e) {
        error = call.name + ": " + e.what();
        return std::nullopt;
    }
}

void ScriptEngine::trust(const std::string& source)
{
    m_trusted.insert(hashOf(source));
}

bool ScriptEngine::trusted(const std::string& source) const
{
    return m_trustAll || m_trusted.count(hashOf(source)) > 0;
}

std::vector<std::string> ScriptEngine::untrusted(const std::vector<Feature>& features) const
{
    std::vector<std::string> out;
    for (const Feature& f : features) {
        if (f.kind == Feature::Kind::Script && f.script.source && !trusted(*f.script.source)
            && std::find(out.begin(), out.end(), *f.script.source) == out.end())
            out.push_back(*f.script.source);
    }
    return out;
}

std::string ScriptEngine::addCommand(const std::string& name, const std::string& help)
{
    const std::string id = "command" + std::to_string(m_nextCommand++);
    std::erase_if(m_commands, [&](const Command& c) { return c.name == name; });
    m_commands.push_back({id, name, help});
    return id;
}

ScriptEngine::Result ScriptEngine::runCommand(const std::string& id, std::shared_ptr<ScriptDocument> document,
                                              const Output& output)
{
    if (!m_running)
        return {false, "Python isn't running"};
    std::string file;
    {
        py::gil_scoped_acquire gil;
        if (!g_py->commands.contains(id))
            return {false, "No such command"};
        py::object what = g_py->commands[py::str(id)];
        if (py::isinstance<py::str>(what))
            file = what.cast<std::string>();
    }
    if (!file.empty())
        return runFile(file, std::move(document), output);
    py::gil_scoped_acquire gil;
    const std::shared_ptr<ScriptDocument> before = m_current;
    m_current = document;
    Result result;
    try {
        Redirect redirect(output);
        g_py->commands[py::str(id)]();
    } catch (const py::error_already_set& e) {
        result = {false, exceptionText(e)};
    } catch (const std::exception& e) {
        result = {false, e.what()};
    }
    m_current = before ? before : document;
    return result;
}

std::vector<std::string> ScriptEngine::loadPlugins(const std::string& folder, std::shared_ptr<ScriptDocument> document,
                                                   const Output& output)
{
    std::vector<std::string> errors;
    if (!m_running)
        return errors;
    std::vector<std::string> files;
    {
        py::gil_scoped_acquire gil;
        py::module_ os = py::module_::import("os");
        if (!os.attr("path").attr("isdir")(folder).cast<bool>())
            return errors;
        for (py::handle name : py::module_::import("builtins").attr("sorted")(os.attr("listdir")(folder))) {
            const std::string n = name.cast<std::string>();
            if (n.size() > 3 && n.substr(n.size() - 3) == ".py")
                files.push_back(os.attr("path").attr("join")(folder, n).cast<std::string>());
        }
    }
    for (const std::string& file : files) {
        const std::size_t commandsBefore = m_commands.size(), generatorsBefore = m_generators.size();
        const Result r = runFile(file, document, output);
        if (!r.ok) {
            errors.push_back(file + ": " + r.error);
            continue;
        }
        if (m_commands.size() == commandsBefore && m_generators.size() == generatorsBefore) {
            // A plain script: a command that runs it.
            std::string name = file.substr(file.find_last_of("/\\") + 1);
            name = name.substr(0, name.size() - 3);
            std::replace(name.begin(), name.end(), '_', ' ');
            if (!name.empty())
                name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            const std::string id = addCommand(name, file);
            py::gil_scoped_acquire gil;
            g_py->commands[py::str(id)] = py::str(file);
        }
    }
    return errors;
}

void ScriptEngine::clearPlugins()
{
    m_commands.clear();
    if (m_running) {
        py::gil_scoped_acquire gil;
        g_py->commands.clear();
    }
}

// --- Macros -----------------------------------------------------------------------------

namespace {

std::string pyString(const std::string& s)
{
    // A raw-safe literal: JSON has no triple quotes, but escape backslashes and quotes anyway.
    std::string out = "'";
    for (char c : s) {
        if (c == '\\' || c == '\'')
            out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "'";
}

} // namespace

std::string macroScript(const ProjectData& before, const ProjectData& after)
{
    std::ostringstream py;
    py << "# A recorded Nibform macro: adds again what was made while recording.\n"
       << "import nibform as nf\n\n"
       << "doc = nf.document()\n"
       << "sketches = {}  # the recorded sketches' ids → the new ones\n";
    // Parameters that are new or changed.
    for (const Parameter& p : after.parameters.parameters) {
        const int i = before.parameters.indexOf(p.name);
        if (i >= 0 && before.parameters.parameters[i] == p)
            continue;
        py << "doc.set_parameter(" << pyString(p.name) << ", " << pyString(p.expression.text) << ", kind="
           << pyString(kindName(p.kind)) << (p.comment.empty() ? "" : ", comment=" + pyString(p.comment)) << ")\n";
    }
    auto known = [&](int id) {
        return std::any_of(before.sketches.begin(), before.sketches.end(), [&](const auto& e) { return e.id == id; });
    };
    const std::size_t from = std::min(before.features.size(), after.features.size());
    for (std::size_t i = from; i < after.features.size(); ++i) {
        const Feature& f = after.features[i];
        if (f.kind == Feature::Kind::Sketch) {
            const auto e = std::find_if(after.sketches.begin(), after.sketches.end(), [&](const auto& s) { return s.id == f.sketch; });
            if (e == after.sketches.end() || known(f.sketch))
                continue;
            py << "sketches[" << f.sketch << "] = doc.add_sketch(" << pyString(sketchToJsonText(e->id, e->sketch).toStdString())
               << ", entry=" << pyString(featureToJsonText(f).toStdString()) << ").id\n";
            continue;
        }
        py << "doc.add_feature(" << pyString(featureToJsonText(f).toStdString()) << ", sketches=sketches)\n";
    }
    py << "doc.rebuild()\n";
    return py.str();
}
