#include "model/SheetMetal.h"
#include "model/Tr.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Mat.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <set>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTol = 1e-6;

TopoDS_Compound compound()
{
    TopoDS_Compound c;
    BRep_Builder().MakeCompound(c);
    return c;
}

gp_Pnt centroid(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

double areaOf(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

double perimeterOf(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::LinearProperties(face, props);
    return props.Mass();
}

TopoDS_Shape tidy(const TopoDS_Shape& shape)
{
    ShapeUpgrade_UnifySameDomain unify(shape, true, true, false);
    unify.Build();
    return unify.Shape();
}

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Shape> out;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next())
        out.push_back(it.Current());
    return out;
}

std::optional<TopoDS_Shape> fused(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepAlgoAPI_Fuse op(a, b);
    if (!op.IsDone() || op.HasErrors())
        return std::nullopt;
    return tidy(op.Shape());
}

std::optional<TopoDS_Shape> cutOut(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepAlgoAPI_Cut op(a, b);
    if (!op.IsDone() || op.HasErrors())
        return std::nullopt;
    return tidy(op.Shape());
}

bool usable(const TopoDS_Shape& shape)
{
    if (shape.IsNull() || solidsOf(shape).empty() || !BRepCheck_Analyzer(shape).IsValid())
        return false;
    return volumeOf(shape) > 1e-9;
}

ModelResult failed(const std::vector<TopoDS_Shape>& bodies, const char* error)
{
    ModelResult r;
    r.bodies = bodies;
    r.error = error;
    return r;
}

// The bodies with `body` replaced by the solids of `shape` (the first in its place).
ModelResult replaced(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Shape& shape)
{
    ModelResult r;
    r.bodies = bodies;
    const std::vector<TopoDS_Shape> solids = solidsOf(shape);
    r.bodies[body] = solids.front();
    r.bodies.insert(r.bodies.end(), solids.begin() + 1, solids.end());
    return r;
}

// A closed polygon (3D points in order) as a planar face.
std::optional<TopoDS_Face> polygonFace(const std::vector<gp_Pnt>& points)
{
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const gp_Pnt& a = points[i];
        const gp_Pnt& b = points[(i + 1) % points.size()];
        if (a.Distance(b) < kTol)
            continue;
        wire.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
    }
    if (!wire.IsDone())
        return std::nullopt;
    BRepBuilderAPI_MakeFace face(wire.Wire(), true);
    if (!face.IsDone())
        return std::nullopt;
    return face.Face();
}

// A prism of the polygon along `along` (its length).
std::optional<TopoDS_Shape> polygonPrism(const std::vector<gp_Pnt>& points, const gp_Vec& along)
{
    const auto face = polygonFace(points);
    if (!face)
        return std::nullopt;
    BRepPrimAPI_MakePrism prism(*face, along);
    if (!prism.IsDone())
        return std::nullopt;
    return prism.Shape();
}

// The normal of a face (outward, as the face is oriented) along one of its edges, halfway.
std::optional<gp_Dir> normalAlong(const TopoDS_Face& face, const TopoDS_Edge& edge)
{
    double first = 0.0, last = 0.0;
    const Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull())
        return std::nullopt;
    const gp_Pnt2d uv = pcurve->Value((first + last) / 2.0);
    const BRepAdaptor_Surface surface(face);
    gp_Pnt p;
    gp_Vec du, dv;
    surface.D1(uv.X(), uv.Y(), p, du, dv);
    const gp_Vec n = du.Crossed(dv);
    if (n.Magnitude() < 1e-12)
        return std::nullopt;
    gp_Dir d(n);
    if (face.Orientation() == TopAbs_REVERSED)
        d.Reverse();
    return d;
}

// Whether the two faces meet smoothly (tangent) along the edge.
bool smoothAcross(const TopoDS_Edge& edge, const TopoDS_Face& a, const TopoDS_Face& b)
{
    const auto na = normalAlong(a, edge);
    const auto nb = normalAlong(b, edge);
    return na && nb && na->Dot(*nb) > 1.0 - 1e-6;
}

// The normal of a face at a point on it (outward).
std::optional<gp_Dir> normalAt(const TopoDS_Face& face, double u, double v)
{
    const BRepAdaptor_Surface surface(face);
    gp_Pnt p;
    gp_Vec du, dv;
    surface.D1(u, v, p, du, dv);
    const gp_Vec n = du.Crossed(dv);
    if (n.Magnitude() < 1e-12)
        return std::nullopt;
    gp_Dir d(n);
    if (face.Orientation() == TopAbs_REVERSED)
        d.Reverse();
    return d;
}

std::optional<gp_Lin> lineOf(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    if (curve.GetType() != GeomAbs_Line)
        return std::nullopt;
    return curve.Line();
}

bool onLine(const TopoDS_Edge& edge, const gp_Lin& line)
{
    const auto own = lineOf(edge);
    if (!own || std::abs(own->Direction().Dot(line.Direction())) < 1.0 - 1e-9)
        return false;
    TopoDS_Vertex a, b;
    TopExp::Vertices(edge, a, b);
    return line.Distance(BRep_Tool::Pnt(a)) < 1e-5 && line.Distance(BRep_Tool::Pnt(b)) < 1e-5;
}

// The line where two planes meet (they must not be parallel).
std::optional<gp_Lin> planesMeet(const gp_Pnt& p1, const gp_Dir& n1, const gp_Pnt& p2, const gp_Dir& n2)
{
    const gp_Vec along = gp_Vec(n1).Crossed(gp_Vec(n2));
    if (along.Magnitude() < 1e-9)
        return std::nullopt;
    const gp_Dir d(along);
    // The point of the line nearest the origin: on both planes, and with no part along it.
    const gp_Mat m(n1.XYZ(), n2.XYZ(), d.XYZ());
    gp_Mat rows = m.Transposed(); // rows: n1, n2, d
    if (std::abs(rows.Determinant()) < 1e-12)
        return std::nullopt;
    const gp_XYZ rhs(gp_Vec(n1).Dot(gp_Vec(p1.XYZ())), gp_Vec(n2).Dot(gp_Vec(p2.XYZ())), 0.0);
    gp_XYZ x = rhs;
    x.Multiply(rows.Inverted());
    return gp_Lin(gp_Pnt(x), d);
}

// Where a point lies along a direction, on the far side of a plane: solves the three
// plane equations n_i · x = c_i.
std::optional<gp_Pnt> solve3(const gp_Dir& n1, double c1, const gp_Dir& n2, double c2, const gp_Dir& n3, double c3)
{
    gp_Mat rows(n1.XYZ(), n2.XYZ(), n3.XYZ());
    rows.Transpose();
    if (std::abs(rows.Determinant()) < 1e-12)
        return std::nullopt;
    gp_XYZ x(c1, c2, c3);
    x.Multiply(rows.Inverted());
    return gp_Pnt(x);
}

// The center of the circle through three points.
std::optional<gp_Pnt> circleCenter(const gp_Pnt& a, const gp_Pnt& m, const gp_Pnt& b)
{
    const gp_Vec ab(a, b), am(a, m);
    const gp_Vec n = ab.Crossed(am);
    if (n.Magnitude() < 1e-12)
        return std::nullopt;
    const gp_Dir nd(n);
    // Equidistant from a and m, from a and b, and in their plane.
    const gp_XYZ mid1 = (a.XYZ() + m.XYZ()) / 2.0, mid2 = (a.XYZ() + b.XYZ()) / 2.0;
    return solve3(gp_Dir(am), am.XYZ().Dot(mid1) / am.Magnitude(), gp_Dir(ab), ab.XYZ().Dot(mid2) / ab.Magnitude(), nd,
                  nd.XYZ().Dot(a.XYZ()));
}

} // namespace

bool isThicknessFace(const TopoDS_Face& face, double thickness)
{
    // A strip as wide as the sheet: twice its area over its perimeter is about its width
    // (an end of a wall, the wall of a hole through the sheet).
    const double perimeter = perimeterOf(face);
    if (perimeter <= 0.0)
        return false;
    return 2.0 * areaOf(face) / perimeter < 1.5 * thickness;
}

std::optional<double> wallThickness(const TopoDS_Shape& body, const TopoDS_Face& face)
{
    const auto normal = planarNormal(face);
    if (!normal)
        return std::nullopt;
    gp_Pnt start = centroid(face);
    if (BRepClass_FaceClassifier(face, start, kTol).State() != TopAbs_IN) {
        // A ring or an L: a point a little inside from a vertex instead.
        bool found = false;
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More() && !found; it.Next()) {
            const BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
            const gp_Pnt mid = c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
            const gp_Vec toward(mid, start);
            if (toward.Magnitude() < kTol)
                continue;
            const gp_Pnt p = mid.Translated(toward.Normalized() * 1e-3);
            if (BRepClass_FaceClassifier(face, p, kTol).State() == TopAbs_IN) {
                start = p;
                found = true;
            }
        }
        if (!found)
            return std::nullopt;
    }
    IntCurvesFace_ShapeIntersector cast;
    cast.Load(body, kTol);
    cast.Perform(gp_Lin(start, normal->Reversed()), 1e-5, std::numeric_limits<double>::max());
    if (!cast.IsDone())
        return std::nullopt;
    double nearest = std::numeric_limits<double>::max();
    for (int i = 1; i <= cast.NbPnt(); ++i)
        nearest = std::min(nearest, cast.WParameter(i));
    if (nearest == std::numeric_limits<double>::max())
        return std::nullopt;
    return nearest;
}

// --- Base flange from an open path ----------------------------------------------------

std::optional<TopoDS_Shape> contourSheet(const TopoDS_Wire& path, const gp_Dir& normal, double depth,
                                         const SheetSpec& spec, bool flip, SheetFace& fixed, std::string& error)
{
    const double t = spec.thickness, r = spec.radius;
    if (t <= 0.0 || r < 0.0 || std::abs(depth) < kTol) {
        error = QT_TRANSLATE_NOOP("Model", "The sheet needs a thickness and a depth");
        return std::nullopt;
    }
    struct Piece
    {
        bool arc = false;
        gp_Pnt a, m, b; // start, middle, end (in the path's direction)
    };
    std::vector<Piece> pieces;
    for (BRepTools_WireExplorer ex(path); ex.More(); ex.Next()) {
        const TopoDS_Edge edge = ex.Current();
        const BRepAdaptor_Curve c(edge);
        Piece p;
        p.a = BRep_Tool::Pnt(TopExp::FirstVertex(edge, true));
        p.b = BRep_Tool::Pnt(TopExp::LastVertex(edge, true));
        p.m = c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
        if (c.GetType() == GeomAbs_Circle)
            p.arc = true;
        else if (c.GetType() != GeomAbs_Line) {
            error = QT_TRANSLATE_NOOP("Model", "A sheet's path takes lines and arcs only");
            return std::nullopt;
        }
        pieces.push_back(p);
    }
    if (pieces.empty()) {
        error = QT_TRANSLATE_NOOP("Model", "The sheet's path is empty");
        return std::nullopt;
    }
    if (pieces.front().a.Distance(pieces.back().b) < kTol) {
        error = QT_TRANSLATE_NOOP("Model", "A sheet's path must be open: pick a closed region for a flat sheet");
        return std::nullopt;
    }
    const double side = flip ? -1.0 : 1.0; // the material: to the left of the path, or to its right
    // The direction of travel at a point of a piece.
    auto tangent = [&](const Piece& p, const gp_Pnt& at) -> gp_Vec {
        if (!p.arc)
            return gp_Vec(p.a, p.b).Normalized();
        const auto c = circleCenter(p.a, p.m, p.b);
        if (!c)
            return gp_Vec(p.a, p.b).Normalized();
        // Turning about the center the way it goes from its start toward its middle.
        const gp_Vec n(normal);
        const double sense = n.Crossed(gp_Vec(*c, p.a)).Dot(gp_Vec(p.a, p.m)) < 0.0 ? -1.0 : 1.0;
        return n.Crossed(gp_Vec(*c, at)).Normalized() * sense;
    };
    auto materialSide = [&](const Piece& p, const gp_Pnt& at) {
        return gp_Vec(normal).Crossed(tangent(p, at)).Normalized() * side;
    };

    // Bends where two lines meet; other corners must already be smooth.
    std::vector<Piece> bent;
    std::vector<double> used(pieces.size(), 0.0); // of each line, taken by the bends at its ends
    std::vector<double> lengths;
    for (const Piece& p : pieces)
        lengths.push_back(p.a.Distance(p.b));
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        bent.push_back(pieces[i]);
        if (i + 1 == pieces.size())
            break;
        const Piece& p = pieces[i];
        const Piece& q = pieces[i + 1];
        const gp_Vec d1 = tangent(p, p.b), d2 = tangent(q, q.a);
        const double turn = d1.Angle(d2);
        if (turn < 1e-6)
            continue;
        if (p.arc || q.arc) {
            error = QT_TRANSLATE_NOOP("Model", "Where an arc meets the next piece of a sheet's path, it must be smooth");
            return std::nullopt;
        }
        if (turn > kPi - 1e-3) {
            error = QT_TRANSLATE_NOOP("Model", "A sheet's path can't fold straight back on itself");
            return std::nullopt;
        }
        const bool left = d1.Crossed(d2).Dot(gp_Vec(normal)) > 0.0;
        // The material inside the turn: the path is the bend's outside.
        const double radius = (left == (side > 0.0)) ? r + t : r;
        if (radius < kTol) {
            error = QT_TRANSLATE_NOOP("Model", "The bend radius must be more than zero");
            return std::nullopt;
        }
        const double cut = radius * std::tan(turn / 2.0);
        used[i] += cut;
        used[i + 1] += cut;
        if (used[i] > lengths[i] + kTol || used[i + 1] > lengths[i + 1] + kTol) {
            error = QT_TRANSLATE_NOOP("Model", "The bends don't fit between the corners of the sheet's path");
            return std::nullopt;
        }
        const gp_Pnt corner = p.b;
        const gp_Pnt t1 = corner.Translated(d1 * -cut);
        const gp_Pnt t2 = corner.Translated(d2 * cut);
        const gp_Vec inward = gp_Vec(normal).Crossed(d1).Normalized() * (left ? 1.0 : -1.0);
        const gp_Pnt center = t1.Translated(inward * radius);
        const gp_Pnt middle = center.Translated(gp_Vec(center, corner).Normalized() * radius);
        bent.back().b = t1;
        if (!bent.back().arc)
            bent.back().m = gp_Pnt((bent.back().a.XYZ() + t1.XYZ()) / 2.0);
        bent.push_back({true, t1, middle, t2});
        pieces[i + 1].a = t2;
        if (!pieces[i + 1].arc)
            pieces[i + 1].m = gp_Pnt((t2.XYZ() + pieces[i + 1].b.XYZ()) / 2.0);
    }

    // The path's pieces, the end, the pieces offset by the thickness back, the start.
    auto edgeOf = [](const gp_Pnt& a, const gp_Pnt& m, const gp_Pnt& b, bool arc) -> std::optional<TopoDS_Edge> {
        if (a.Distance(b) < kTol)
            return std::nullopt;
        if (!arc)
            return BRepBuilderAPI_MakeEdge(a, b).Edge();
        GC_MakeArcOfCircle make(a, m, b);
        if (!make.IsDone())
            return BRepBuilderAPI_MakeEdge(a, b).Edge();
        return BRepBuilderAPI_MakeEdge(make.Value()).Edge();
    };
    BRepBuilderAPI_MakeWire wire;
    for (const Piece& p : bent) {
        if (const auto e = edgeOf(p.a, p.m, p.b, p.arc))
            wire.Add(*e);
    }
    auto off = [&](const Piece& p, const gp_Pnt& at) { return at.Translated(materialSide(p, at) * t); };
    const Piece& last = bent.back();
    const Piece& first = bent.front();
    wire.Add(BRepBuilderAPI_MakeEdge(last.b, off(last, last.b)).Edge());
    for (auto it = bent.rbegin(); it != bent.rend(); ++it) {
        if (const auto e = edgeOf(off(*it, it->b), off(*it, it->m), off(*it, it->a), it->arc))
            wire.Add(*e);
    }
    wire.Add(BRepBuilderAPI_MakeEdge(off(first, first.a), first.a).Edge());
    if (!wire.IsDone()) {
        error = QT_TRANSLATE_NOOP("Model", "The sheet's outline doesn't close (is the thickness more than the bend radius?)");
        return std::nullopt;
    }
    BRepBuilderAPI_MakeFace face(wire.Wire(), true);
    if (!face.IsDone()) {
        error = QT_TRANSLATE_NOOP("Model", "The sheet's outline doesn't close (is the thickness more than the bend radius?)");
        return std::nullopt;
    }
    BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(normal) * depth);
    if (!prism.IsDone() || !usable(prism.Shape())) {
        error = QT_TRANSLATE_NOOP("Model", "The sheet crosses itself");
        return std::nullopt;
    }
    const gp_Pnt mid = first.arc ? first.m : gp_Pnt((first.a.XYZ() + first.b.XYZ()) / 2.0);
    fixed.middle = mid.Translated(gp_Vec(normal) * (depth / 2.0));
    fixed.normal = gp_Dir(materialSide(first, mid).Reversed());
    return prism.Shape();
}

// --- Edge flanges and hems ------------------------------------------------------------

ModelResult edgeFlange(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Edge& edge,
                       const SheetSpec& spec, double length, const FlangeSpec& flange, bool flip, bool hem)
{
    if (body < 0 || body >= static_cast<int>(bodies.size()))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sheet this was bent from is gone"));
    const double t = spec.thickness;
    const double r = hem ? flange.gap / 2.0 : spec.radius;
    if (t <= 0.0 || r <= 0.0)
        return failed(bodies, hem ? QT_TRANSLATE_NOOP("Model", "A hem needs a gap") : QT_TRANSLATE_NOOP("Model", "The bend radius must be more than zero"));
    const auto line = lineOf(edge);
    if (!line)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick a straight edge along an end of the sheet"));
    const gp_Dir along = line->Direction();

    // The edge's two faces: a side of the sheet and an end of it (as wide as the sheet).
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(bodies[body], TopAbs_EDGE, TopAbs_FACE, faces);
    if (!faces.Contains(edge))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The edge isn't on the sheet"));
    std::vector<TopoDS_Face> around;
    for (const TopoDS_Shape& f : faces.FindFromKey(edge)) {
        if (std::none_of(around.begin(), around.end(), [&](const TopoDS_Face& g) { return g.IsSame(f); }))
            around.push_back(TopoDS::Face(f));
    }
    auto widthAcross = [&](const TopoDS_Face& face, const gp_Dir& n) {
        const gp_Dir across = gp_Dir(gp_Vec(along).Crossed(gp_Vec(n)));
        double lo = std::numeric_limits<double>::max(), hi = -lo;
        for (TopExp_Explorer it(face, TopAbs_VERTEX); it.More(); it.Next()) {
            const double s = gp_Vec(line->Location(), BRep_Tool::Pnt(TopoDS::Vertex(it.Current()))).Dot(gp_Vec(across));
            lo = std::min(lo, s);
            hi = std::max(hi, s);
        }
        return hi - lo;
    };
    std::optional<TopoDS_Face> sideFace, endFace;
    if (around.size() == 2) {
        const auto n0 = planarNormal(around[0]), n1 = planarNormal(around[1]);
        if (n0 && n1 && std::abs(n0->Dot(*n1)) < 1e-6) {
            const double tol = 1e-4 + 1e-3 * t;
            const bool end0 = std::abs(widthAcross(around[0], *n0) - t) < tol;
            const bool end1 = std::abs(widthAcross(around[1], *n1) - t) < tol;
            if (end1 && !end0) {
                sideFace = around[0];
                endFace = around[1];
            } else if (end0 && !end1) {
                sideFace = around[1];
                endFace = around[0];
            } else if (end0 && end1) {
                // A sheet as wide as it is thick: the larger face is its side.
                const bool first = areaOf(around[0]) >= areaOf(around[1]);
                sideFace = around[first ? 0 : 1];
                endFace = around[first ? 1 : 0];
            }
        }
    }
    if (!sideFace)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick an edge along an end of the sheet (where its thickness shows)"));
    const gp_Dir n = *planarNormal(*sideFace);  // out of the side: the bend goes this way
    const gp_Dir out = *planarNormal(*endFace); // out of the end: away from the sheet
    const gp_Dir e = gp_Dir(gp_Vec(out).Crossed(gp_Vec(n)));
    const gp_Pnt origin = line->Location();

    // Along the edge: where it starts and ends.
    double z0 = std::numeric_limits<double>::max(), z1 = -z0;
    TopoDS_Vertex va, vb;
    TopExp::Vertices(edge, va, vb);
    for (const TopoDS_Vertex& v : {va, vb}) {
        const double z = gp_Vec(origin, BRep_Tool::Pnt(v)).Dot(gp_Vec(e));
        z0 = std::min(z0, z);
        z1 = std::max(z1, z);
    }
    const double inset = std::max(0.0, flange.inset);
    const double from = z0 + inset, to = z1 - inset;
    if (to - from < kTol)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The inset leaves nothing of the flange"));

    const double angle = hem ? kPi : std::clamp(flange.angle, 0.0, 180.0) * kPi / 180.0;
    if (angle < 1e-3 || (!hem && angle > kPi - 1e-3))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "A flange's angle is more than 0° and less than 180°"));
    // How far the bend reaches past where the outsides of the sheet and flange would meet.
    const double setback = hem ? 0.0 : (r + t) * std::tan(angle / 2.0);
    const double straight = hem ? length : length - setback;
    if (straight < -kTol)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The flange is shorter than its bend"));
    const bool inside = flange.inside && !hem;
    const double x0 = inside ? -setback : 0.0;

    // The flange's section, across the edge: x out of the end, y out of the side (the
    // sheet is between y = -t and 0), bent toward +y (or -y: mirrored about the sheet's middle).
    auto at = [&](double x, double y, double z = 0.0) {
        if (flip)
            y = -t - y;
        return origin.Translated(gp_Vec(out) * x + gp_Vec(n) * y + gp_Vec(e) * z);
    };
    const double cx = x0, cy = r;
    const double su = std::sin(angle), cu = -std::cos(angle);   // from the center to the bend's end
    const double sh = std::sin(angle / 2.0), ch = -std::cos(angle / 2.0);
    const double tx = std::cos(angle), ty = std::sin(angle);    // the flange's way
    const double ro = r + t;
    const gp_Pnt in0 = at(x0, 0.0), out0 = at(x0, -t);
    const gp_Pnt inMid = at(cx + r * sh, cy + r * ch), outMid = at(cx + ro * sh, cy + ro * ch);
    const gp_Pnt inEnd = at(cx + r * su, cy + r * cu), outEnd = at(cx + ro * su, cy + ro * cu);
    const double ls = std::max(0.0, straight);
    const gp_Pnt inTip = at(cx + r * su + ls * tx, cy + r * cu + ls * ty);
    const gp_Pnt outTip = at(cx + ro * su + ls * tx, cy + ro * cu + ls * ty);

    // Reaching a little back into the sheet: overlapping it, the flange joins it cleanly
    // (only touching it along the bend's start, it confuses the boolean).
    const double back = std::min(t, 1.0) / 2.0;
    const gp_Pnt inBack = at(x0 - back, 0.0), outBack = at(x0 - back, -t);
    BRepBuilderAPI_MakeWire wire;
    try {
        wire.Add(BRepBuilderAPI_MakeEdge(in0, inBack).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(inBack, outBack).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(outBack, out0).Edge());
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(out0, outMid, outEnd).Value()).Edge());
        if (ls > kTol) {
            wire.Add(BRepBuilderAPI_MakeEdge(outEnd, outTip).Edge());
            wire.Add(BRepBuilderAPI_MakeEdge(outTip, inTip).Edge());
            wire.Add(BRepBuilderAPI_MakeEdge(inTip, inEnd).Edge());
        } else {
            wire.Add(BRepBuilderAPI_MakeEdge(outEnd, inEnd).Edge());
        }
        wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(inEnd, inMid, in0).Value()).Edge());
    } catch (const Standard_Failure&) {
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not shape the flange"));
    }
    if (!wire.IsDone())
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not shape the flange"));
    BRepBuilderAPI_MakeFace section(wire.Wire(), true);
    if (!section.IsDone())
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not shape the flange"));
    gp_Trsf toStart;
    toStart.SetTranslation(gp_Vec(e) * from);
    const TopoDS_Shape placed = BRepBuilderAPI_Transform(section.Face(), toStart, true).Shape();
    const TopoDS_Shape solid = BRepPrimAPI_MakePrism(placed, gp_Vec(e) * (to - from)).Shape();

    TopoDS_Shape shape = bodies[body];
    if (inside && setback > kTol) {
        // The end of the sheet gives way to the bend, and (inset) reliefs let it bend there.
        const double eps = 1e-3;
        TopoDS_Compound tools = compound();
        auto slab = [&](double xa, double xb, double za, double zb) {
            const std::vector<gp_Pnt> rect{at(xa, eps, za), at(xb, eps, za), at(xb, -t - eps, za), at(xa, -t - eps, za)};
            if (const auto prism = polygonPrism(rect, gp_Vec(e) * (zb - za)))
                BRep_Builder().Add(tools, *prism);
        };
        slab(x0, eps, from, to);
        if (inset > kTol) {
            const double w = std::min(t, inset); // as wide as the sheet is thick
            slab(x0 - t, eps, from - w, from);
            slab(x0 - t, eps, to, to + w);
        }
        const auto trimmed = cutOut(shape, tools);
        if (!trimmed || solidsOf(*trimmed).empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not make room for the bend"));
        shape = *trimmed;
    }
    const auto joined = fused(shape, solid);
    if (!joined || !usable(*joined))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not join the flange to the sheet"));
    ModelResult result = replaced(bodies, body, *joined);
    result.tool = FeatureTool{solid, BodyOperation::Join};
    return result;
}

// --- Corner reliefs -------------------------------------------------------------------

ModelResult cornerRelief(const std::vector<TopoDS_Shape>& bodies, int body, const gp_Pnt& corner,
                         const SheetSpec& spec, double size, bool square)
{
    if (body < 0 || body >= static_cast<int>(bodies.size()))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sheet this was cut from is gone"));
    if (size <= 0.0)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "A relief needs a size"));
    const TopoDS_Shape vertex = BRepBuilderAPI_MakeVertex(corner).Shape();
    // The largest flat side of the sheet at the corner.
    std::optional<TopoDS_Face> best;
    double bestArea = 0.0;
    for (TopExp_Explorer it(bodies[body], TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        if (!planarNormal(face) || isThicknessFace(face, spec.thickness))
            continue;
        BRepExtrema_DistShapeShape d(face, vertex);
        if (!d.IsDone() || d.Value() > 1e-5)
            continue;
        const double a = areaOf(face);
        if (a > bestArea) {
            bestArea = a;
            best = face;
        }
    }
    if (!best)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Tap a corner of a flat side of the sheet"));
    const gp_Dir n = *planarNormal(*best);
    const double h = spec.radius + 2.0 * spec.thickness + 1e-3;
    TopoDS_Shape tool;
    if (!square) {
        tool = BRepPrimAPI_MakeCylinder(gp_Ax2(corner.Translated(gp_Vec(n) * -h), n), size / 2.0, 2.0 * h).Shape();
    } else {
        // Along the face's edges at the corner.
        gp_Dir u = gp_Dir(gp_Vec(n).Crossed(gp_Vec(gp::DX())).Magnitude() > 1e-6 ? gp_Vec(n).Crossed(gp_Vec(gp::DX()))
                                                                                 : gp_Vec(n).Crossed(gp_Vec(gp::DY())));
        for (TopExp_Explorer it(*best, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(it.Current());
            const auto l = lineOf(edge);
            if (l && l->Distance(corner) < 1e-5) {
                u = l->Direction();
                break;
            }
        }
        const gp_Dir v(gp_Vec(n).Crossed(gp_Vec(u)));
        const double s = size / 2.0;
        const gp_Pnt base = corner.Translated(gp_Vec(n) * -h);
        const std::vector<gp_Pnt> rect{base.Translated(gp_Vec(u) * -s + gp_Vec(v) * -s), base.Translated(gp_Vec(u) * s + gp_Vec(v) * -s),
                                       base.Translated(gp_Vec(u) * s + gp_Vec(v) * s), base.Translated(gp_Vec(u) * -s + gp_Vec(v) * s)};
        const auto prism = polygonPrism(rect, gp_Vec(n) * 2.0 * h);
        if (!prism)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not cut the relief"));
        tool = *prism;
    }
    const auto cut = cutOut(bodies[body], tool);
    if (!cut || !usable(*cut))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not cut the relief"));
    ModelResult result = replaced(bodies, body, *cut);
    result.tool = FeatureTool{tool, BodyOperation::Cut};
    return result;
}

// --- Converting a shelled solid -------------------------------------------------------

ModelResult convertToSheet(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Face& fixed,
                           const SheetSpec& spec)
{
    if (body < 0 || body >= static_cast<int>(bodies.size()))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The body to convert is gone"));
    const double t = spec.thickness, r = spec.radius;
    if (t <= 0.0 || r < 0.0)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sheet needs a thickness"));
    const TopoDS_Shape& shape = bodies[body];
    if (!planarNormal(fixed) || isThicknessFace(fixed, t))
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick a flat side of a wall"));

    TopTools_IndexedMapOfShape faceIndex;
    TopExp::MapShapes(shape, TopAbs_FACE, faceIndex);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    // The walls' sides on the fixed face's side: from it across every edge that doesn't
    // lead to an end of a wall.
    const int start = faceIndex.FindIndex(fixed);
    if (start == 0)
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face isn't on the body"));
    std::vector<int> parent(faceIndex.Extent() + 1, -1);
    std::vector<bool> wall(faceIndex.Extent() + 1, false);
    std::vector<bool> thin(faceIndex.Extent() + 1, false);
    for (int i = 1; i <= faceIndex.Extent(); ++i)
        thin[i] = isThicknessFace(TopoDS::Face(faceIndex(i)), t);
    std::deque<int> queue{start};
    wall[start] = true;
    while (!queue.empty()) {
        const int f = queue.front();
        queue.pop_front();
        for (TopExp_Explorer it(faceIndex(f), TopAbs_EDGE); it.More(); it.Next()) {
            for (const TopoDS_Shape& g : edgeFaces.FindFromKey(it.Current())) {
                const int gi = faceIndex.FindIndex(g);
                if (gi == f || wall[gi] || thin[gi])
                    continue;
                wall[gi] = true;
                parent[gi] = f;
                queue.push_back(gi);
            }
        }
    }
    // Each wall a shell of that thickness: just behind its side is material, just past
    // the thickness is not.
    for (int i = 1; i <= faceIndex.Extent(); ++i) {
        if (!wall[i])
            continue;
        const TopoDS_Face face = TopoDS::Face(faceIndex(i));
        if (!planarNormal(face))
            continue;
        const auto depth = wallThickness(shape, face);
        if (depth && std::abs(*depth - t) > 1e-3 + 1e-3 * t)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The walls aren't as thick as the sheet: shell the body with that thickness first"));
    }

    // Where two walls meet: bent to each other where one was reached from the other,
    // ripped apart elsewhere. Edges already smooth (round walls) stay.
    struct Meeting
    {
        int a = 0, b = 0;
        gp_Lin line;
    };
    std::vector<Meeting> bends, rips;
    std::set<std::pair<int, int>> seen;
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edgeFaces.FindKey(i));
        std::vector<int> ws;
        for (const TopoDS_Shape& f : edgeFaces(i)) {
            const int fi = faceIndex.FindIndex(f);
            if (wall[fi] && std::find(ws.begin(), ws.end(), fi) == ws.end())
                ws.push_back(fi);
        }
        if (ws.size() != 2)
            continue;
        const TopoDS_Face fa = TopoDS::Face(faceIndex(ws[0])), fb = TopoDS::Face(faceIndex(ws[1]));
        if (smoothAcross(edge, fa, fb))
            continue;
        const auto line = lineOf(edge);
        if (!line || !planarNormal(fa) || !planarNormal(fb))
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Only flat walls meeting along straight edges can be converted"));
        const auto key = std::minmax(ws[0], ws[1]);
        if (!seen.insert(key).second)
            continue;
        const bool tree = parent[ws[0]] == ws[1] || parent[ws[1]] == ws[0];
        (tree ? bends : rips).push_back({ws[0], ws[1], *line});
    }

    // Rips: the block where two walls overlap goes, with a little gap; where a rip ends at
    // a bend, a corner relief as deep as the bend zone (so the bends that meet there end
    // cleanly). Worked out on the walls as they are, cut once they are bent.
    const double gap = std::max(0.05, 0.1 * t);
    const double eps = 1e-3;
    std::vector<TopoDS_Shape> tools; // overlapping: cut one after another
    for (const Meeting& m : rips) {
        const gp_Dir n1 = *planarNormal(TopoDS::Face(faceIndex(m.a)));
        const gp_Dir n2 = *planarNormal(TopoDS::Face(faceIndex(m.b)));
        const gp_Dir d = m.line.Direction();
        const gp_Pnt o = m.line.Location();
        double lo = std::numeric_limits<double>::max(), hi = -lo;
        for (int i = 1; i <= edgeFaces.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edgeFaces.FindKey(i));
            if (!onLine(edge, m.line))
                continue;
            TopoDS_Vertex va, vb;
            TopExp::Vertices(edge, va, vb);
            for (const TopoDS_Vertex& v : {va, vb}) {
                const double s = gp_Vec(o, BRep_Tool::Pnt(v)).Dot(gp_Vec(d));
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
        }
        const double c1 = gp_Vec(n1).Dot(gp_Vec(o.XYZ())), c2 = gp_Vec(n2).Dot(gp_Vec(o.XYZ()));
        const double c3 = gp_Vec(d).Dot(gp_Vec(o.XYZ()));
        // A block of the corner `depth` into both walls, from `from` to `to` along the edge.
        auto block = [&](double depth, double from, double to) {
            std::vector<gp_Pnt> section;
            for (const auto& [a, b] : std::vector<std::pair<double, double>>{{eps, eps}, {-depth, eps}, {-depth, -depth}, {eps, -depth}}) {
                const auto p = solve3(n1, c1 + a, n2, c2 + b, d, c3 + from);
                if (!p)
                    return false;
                section.push_back(*p);
            }
            const auto prism = polygonPrism(section, gp_Vec(d) * (to - from));
            if (!prism)
                return false;
            tools.push_back(*prism);
            return true;
        };
        if (!block(t + gap, lo - eps, hi + eps))
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Walls that meet flat can't be ripped"));
        // Its ends where it meets a bend.
        const double zone = r + t + gap;
        for (const auto& [end, inward] : {std::pair(lo, 1.0), std::pair(hi, -1.0)}) {
            const gp_Pnt at = o.Translated(gp_Vec(d) * end);
            const bool atBend = std::any_of(bends.begin(), bends.end(), [&](const Meeting& b) { return b.line.Distance(at) < 1e-6; });
            if (atBend)
                block(zone, inward > 0 ? end - eps : end - zone, inward > 0 ? end + zone : end + eps);
        }
    }

    // Bends: the outside corner rounded with the radius plus the thickness, the inside one
    // with the radius.
    TopoDS_Shape result = shape;
    if (!bends.empty() && r > 0.0) {
        struct Round
        {
            gp_Lin line;
            double radius;
        };
        std::vector<Round> rounds;
        for (const Meeting& m : bends) {
            const gp_Dir n1 = *planarNormal(TopoDS::Face(faceIndex(m.a)));
            const gp_Dir n2 = *planarNormal(TopoDS::Face(faceIndex(m.b)));
            rounds.push_back({m.line, r + t});
            const gp_Pnt o = m.line.Location();
            const auto inner = planesMeet(o.Translated(gp_Vec(n1) * -t), n1, o.Translated(gp_Vec(n2) * -t), n2);
            if (inner)
                rounds.push_back({*inner, r});
        }
        auto roundAll = [](const TopoDS_Shape& s, const std::vector<Round>& which) -> std::optional<TopoDS_Shape> {
            BRepFilletAPI_MakeFillet fillet(s);
            int added = 0;
            TopTools_IndexedMapOfShape edges;
            TopExp::MapShapes(s, TopAbs_EDGE, edges);
            for (int i = 1; i <= edges.Extent(); ++i) {
                const TopoDS_Edge edge = TopoDS::Edge(edges(i));
                for (const Round& w : which) {
                    if (onLine(edge, w.line)) {
                        fillet.Add(w.radius, edge);
                        ++added;
                        break;
                    }
                }
            }
            if (added == 0)
                return s;
            try {
                fillet.Build();
            } catch (const Standard_Failure&) {
                return std::nullopt;
            }
            if (!fillet.IsDone() || !usable(fillet.Shape()))
                return std::nullopt;
            return fillet.Shape();
        };
        auto all = roundAll(result, rounds);
        if (!all) {
            // One bend at a time: each one's fillets are simpler on their own.
            TopoDS_Shape step = result;
            for (const Round& w : rounds) {
                const auto next = roundAll(step, {w});
                if (!next)
                    return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not bend the walls: try a smaller bend radius"));
                step = *next;
            }
            all = step;
        }
        result = *all;
    }
    for (const TopoDS_Shape& tool : tools) {
        const auto cut = cutOut(result, tool);
        if (!cut || !usable(*cut))
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Could not rip the walls apart at their corners"));
        result = *cut;
    }
    return replaced(bodies, body, result);
}

// --- Unfolding ------------------------------------------------------------------------

namespace {

// How the points of one face land in the flat pattern. A flat face: moved rigidly. A
// bend: rolled out from the line where it starts (`start`, on the side being unfolded,
// along the axis), `radius` (of the neutral layer) times the angle turned.
struct Placement
{
    bool bend = false;
    gp_Trsf toFlat; // flat: the face; bend: the face it starts from
    gp_Ax1 axis;
    gp_Pnt start;
    gp_Dir radial; // from the axis to the start line
    double sense = 1.0; // which way about the axis the bend turns
    double neutral = 0.0;
    double inner = 0.0;
    bool up = true;
};

Placement flatAt(const gp_Trsf& toFlat)
{
    Placement p;
    p.toFlat = toFlat;
    return p;
}

gp_Dir radialOf(const gp_Ax1& axis, const gp_Pnt& p)
{
    gp_Vec v(axis.Location(), p);
    v -= gp_Vec(axis.Direction()) * v.Dot(gp_Vec(axis.Direction()));
    return gp_Dir(v);
}

// The angle turned from the start line to `p`, the bend's way (0 … 2π).
double turned(const Placement& b, const gp_Pnt& p)
{
    const gp_Dir r = radialOf(b.axis, p);
    double a = gp_Vec(b.radial).AngleWithRef(gp_Vec(r), gp_Vec(b.axis.Direction()) * b.sense);
    if (a < -1e-6)
        a += 2.0 * kPi;
    return std::max(a, 0.0);
}

gp_Pnt flatPoint(const Placement& place, const gp_Pnt& p)
{
    if (!place.bend)
        return p.Transformed(place.toFlat);
    const gp_Vec axis(place.axis.Direction());
    const gp_Pnt foot = place.start.Translated(axis * gp_Vec(place.start, p).Dot(axis));
    const gp_Vec out = axis.Crossed(gp_Vec(place.radial)) * place.sense;
    return foot.Translated(out * (turned(place, p) * place.neutral)).Transformed(place.toFlat);
}

// Where the face beyond the bend, along its line at `p`, is moved to lie flat.
gp_Trsf beyond(const Placement& bend, const gp_Pnt& p)
{
    const double a = turned(bend, p);
    gp_Trsf turn, shift;
    turn.SetRotation(bend.axis, -a * bend.sense);
    const gp_Vec out = gp_Vec(bend.axis.Direction()).Crossed(gp_Vec(bend.radial)) * bend.sense;
    shift.SetTranslation(out * (a * bend.neutral));
    return bend.toFlat * shift * turn;
}

std::optional<Placement> bendFrom(const gp_Trsf& toFlat, const TopoDS_Edge& edge, const TopoDS_Face& face,
                                  const SheetSpec& spec, std::string& error)
{
    const BRepAdaptor_Surface surface(face);
    const gp_Cylinder cylinder = surface.Cylinder();
    const gp_Ax1 axis = cylinder.Axis();
    const auto line = lineOf(edge);
    if (!line || std::abs(line->Direction().Dot(axis.Direction())) < 1.0 - 1e-6) {
        error = QT_TRANSLATE_NOOP("Model", "A bend doesn't run straight along its edge");
        return std::nullopt;
    }
    Placement b;
    b.bend = true;
    b.toFlat = toFlat;
    b.axis = axis;
    const BRepAdaptor_Curve c(edge);
    b.start = c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
    b.radial = radialOf(axis, b.start);
    // Which way it turns: toward its middle.
    double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const gp_Pnt middle = surface.Value((u0 + u1) / 2.0, (v0 + v1) / 2.0);
    const double toward = gp_Vec(b.radial).AngleWithRef(gp_Vec(radialOf(axis, middle)), gp_Vec(axis.Direction()));
    b.sense = toward >= 0.0 ? 1.0 : -1.0;
    // On the outside of the bend, the face looks away from the axis.
    const auto normal = normalAt(face, (u0 + u1) / 2.0, (v0 + v1) / 2.0);
    const bool outside = normal && normal->Dot(radialOf(axis, middle)) > 0.0;
    const double radius = cylinder.Radius();
    b.inner = outside ? radius - spec.thickness : radius;
    if (b.inner < -1e-6) {
        error = QT_TRANSLATE_NOOP("Model", "A bend is tighter than the sheet is thick");
        return std::nullopt;
    }
    b.inner = std::max(0.0, b.inner);
    b.neutral = b.inner + spec.kFactor * spec.thickness;
    b.up = !outside;
    return b;
}

// The face, its edges through the placement, on the flat plane.
std::optional<TopoDS_Face> flatFace(const TopoDS_Face& face, const Placement& place)
{
    if (!place.bend) {
        const TopoDS_Shape moved = BRepBuilderAPI_Transform(face, place.toFlat, true).Shape();
        return TopoDS::Face(moved);
    }
    auto mapEdge = [&](const TopoDS_Edge& edge) -> std::optional<TopoDS_Edge> {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2, true);
        const gp_Pnt a = flatPoint(place, BRep_Tool::Pnt(v1));
        const gp_Pnt b = flatPoint(place, BRep_Tool::Pnt(v2));
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line) {
            if (a.Distance(b) < kTol)
                return std::nullopt;
            return BRepBuilderAPI_MakeEdge(a, b).Edge();
        }
        // Rolled out: through points along it.
        constexpr int kSamples = 32;
        const bool closed = BRep_Tool::IsClosed(edge) || v1.IsSame(v2);
        const bool reversed = edge.Orientation() == TopAbs_REVERSED;
        const double f = curve.FirstParameter(), l = curve.LastParameter();
        std::vector<gp_Pnt> pts;
        const int count = closed ? kSamples : kSamples + 1;
        for (int i = 0; i < count; ++i) {
            double s = static_cast<double>(i) / kSamples;
            if (reversed)
                s = 1.0 - s;
            pts.push_back(flatPoint(place, curve.Value(f + (l - f) * s)));
        }
        if (!closed) {
            pts.front() = a;
            pts.back() = b;
            // Straight after all (a bend's end).
            const gp_Lin chord(a, gp_Dir(gp_Vec(a, b)));
            if (a.Distance(b) > kTol
                && std::all_of(pts.begin(), pts.end(), [&](const gp_Pnt& p) { return chord.Distance(p) < 1e-7; }))
                return BRepBuilderAPI_MakeEdge(a, b).Edge();
        }
        Handle(TColgp_HArray1OfPnt) array = new TColgp_HArray1OfPnt(1, static_cast<int>(pts.size()));
        for (std::size_t i = 0; i < pts.size(); ++i)
            array->SetValue(static_cast<int>(i) + 1, pts[i]);
        try {
            GeomAPI_Interpolate interpolate(array, closed, 1e-9);
            interpolate.Perform();
            if (!interpolate.IsDone())
                return std::nullopt;
            return BRepBuilderAPI_MakeEdge(interpolate.Curve()).Edge();
        } catch (const Standard_Failure&) {
            return std::nullopt;
        }
    };
    const TopoDS_Wire outer = BRepTools::OuterWire(face);
    std::optional<TopoDS_Wire> flatOuter;
    std::vector<TopoDS_Wire> holes;
    for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(w.Current());
        BRepBuilderAPI_MakeWire make;
        for (BRepTools_WireExplorer ex(wire, face); ex.More(); ex.Next()) {
            if (const auto e = mapEdge(ex.Current()))
                make.Add(*e);
        }
        if (!make.IsDone())
            return std::nullopt;
        if (wire.IsSame(outer))
            flatOuter = make.Wire();
        else
            holes.push_back(make.Wire());
    }
    if (!flatOuter)
        return std::nullopt;
    BRepBuilderAPI_MakeFace make(gp_Pln(gp::Origin(), gp::DZ()), *flatOuter, true);
    for (const TopoDS_Wire& h : holes)
        make.Add(h);
    if (!make.IsDone())
        return std::nullopt;
    ShapeFix_Face fix(make.Face());
    fix.Perform();
    return fix.Face();
}

} // namespace

FlatPattern unfoldSheet(const TopoDS_Shape& body, const TopoDS_Face& fixed, const SheetSpec& spec)
{
    FlatPattern flat;
    const auto normal = planarNormal(fixed);
    if (!normal) {
        flat.error = QT_TRANSLATE_NOOP("Model", "The sheet's fixed face isn't flat");
        return flat;
    }
    if (spec.thickness <= 0.0) {
        flat.error = QT_TRANSLATE_NOOP("Model", "The sheet needs a thickness");
        return flat;
    }
    TopTools_IndexedMapOfShape faceIndex;
    TopExp::MapShapes(body, TopAbs_FACE, faceIndex);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const int start = faceIndex.FindIndex(fixed);
    if (start == 0) {
        flat.error = QT_TRANSLATE_NOOP("Model", "The sheet's fixed face is gone");
        return flat;
    }

    // From the fixed face across every smooth edge: flat walls and the bends between them.
    std::vector<std::optional<Placement>> placed(faceIndex.Extent() + 1);
    {
        const BRepAdaptor_Surface s(fixed);
        gp_Trsf toFlat;
        toFlat.SetTransformation(gp_Ax3(centroid(fixed), *normal, s.Plane().XAxis().Direction()));
        placed[start] = flatAt(toFlat);
    }
    std::deque<int> queue{start};
    std::string error;
    while (!queue.empty()) {
        const int fi = queue.front();
        queue.pop_front();
        const TopoDS_Face f = TopoDS::Face(faceIndex(fi));
        const Placement here = *placed[fi];
        for (TopExp_Explorer it(f, TopAbs_EDGE); it.More(); it.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(it.Current());
            for (const TopoDS_Shape& gs : edgeFaces.FindFromKey(edge)) {
                const int gi = faceIndex.FindIndex(gs);
                if (gi == fi || placed[gi])
                    continue;
                const TopoDS_Face g = TopoDS::Face(gs);
                if (!smoothAcross(edge, f, g))
                    continue;
                const GeomAbs_SurfaceType type = BRepAdaptor_Surface(g).GetType();
                if (type != GeomAbs_Plane && type != GeomAbs_Cylinder) {
                    flat.error = QT_TRANSLATE_NOOP("Model", "Only flat walls and round bends unfold");
                    return flat;
                }
                // Where the face it continues lies flat (beyond a bend: turned back and moved on).
                gp_Trsf base = here.toFlat;
                bool sameBend = false;
                if (here.bend) {
                    const BRepAdaptor_Curve c(edge);
                    const gp_Pnt mid = c.Value((c.FirstParameter() + c.LastParameter()) / 2.0);
                    if (type == GeomAbs_Cylinder) {
                        const gp_Cylinder cyl = BRepAdaptor_Surface(g).Cylinder();
                        const gp_Lin a(here.axis), b(cyl.Axis());
                        const double radius = gp_Lin(here.axis).Distance(here.start);
                        sameBend = a.Direction().IsParallel(b.Direction(), 1e-6) && a.Distance(b.Location()) < 1e-6
                                && std::abs(cyl.Radius() - radius) < 1e-6;
                    }
                    if (!sameBend)
                        base = beyond(here, mid);
                }
                if (sameBend) {
                    placed[gi] = here;
                } else if (type == GeomAbs_Plane) {
                    placed[gi] = flatAt(base);
                } else {
                    const auto b = bendFrom(base, edge, g, spec, error);
                    if (!b) {
                        flat.error = error;
                        return flat;
                    }
                    placed[gi] = *b;
                }
                queue.push_back(gi);
            }
        }
    }

    // The faces laid flat, sewn into one.
    BRepBuilderAPI_Sewing sewing(1e-5);
    int count = 0;
    for (int i = 1; i <= faceIndex.Extent(); ++i) {
        if (!placed[i])
            continue;
        const TopoDS_Face face = TopoDS::Face(faceIndex(i));
        const auto laid = flatFace(face, *placed[i]);
        if (!laid) {
            flat.error = QT_TRANSLATE_NOOP("Model", "A bend couldn't be rolled out");
            return flat;
        }
        sewing.Add(*laid);
        ++count;
        if (placed[i]->bend) {
            // Its bend line, halfway across.
            const Placement& b = *placed[i];
            double angle = 0.0, lo = std::numeric_limits<double>::max(), hi = -lo;
            const gp_Vec axis(b.axis.Direction());
            for (TopExp_Explorer v(face, TopAbs_VERTEX); v.More(); v.Next()) {
                const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
                angle = std::max(angle, turned(b, p));
                const double s = gp_Vec(b.start, p).Dot(axis);
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
            if (angle > 1e-6 && hi > lo) {
                const gp_Vec out = axis.Crossed(gp_Vec(b.radial)) * b.sense;
                auto line = [&](double s) {
                    const gp_Pnt p = b.start.Translated(axis * s + out * (angle * b.neutral / 2.0)).Transformed(b.toFlat);
                    return gp_Pnt2d(p.X(), p.Y());
                };
                flat.bends.push_back({line(lo), line(hi), angle * 180.0 / kPi, b.inner, b.up});
            }
        }
    }
    if (count == 0) {
        flat.error = QT_TRANSLATE_NOOP("Model", "Nothing to unfold");
        return flat;
    }
    TopoDS_Shape sheet;
    try {
        sewing.Perform();
        sheet = tidy(sewing.SewedShape());
    } catch (const Standard_Failure&) {
        flat.error = QT_TRANSLATE_NOOP("Model", "The unfolded faces don't fit together");
        return flat;
    }
    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer it(sheet, TopAbs_FACE); it.More(); it.Next())
        faces.push_back(TopoDS::Face(it.Current()));
    if (faces.size() > 1) {
        // Pieces the sewing left apart: fused flat into one.
        TopTools_ListOfShape args, tools;
        args.Append(faces.front());
        for (std::size_t i = 1; i < faces.size(); ++i)
            tools.Append(faces[i]);
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(args);
        fuse.SetTools(tools);
        fuse.Build();
        if (fuse.IsDone() && !fuse.HasErrors()) {
            const TopoDS_Shape one = tidy(fuse.Shape());
            faces.clear();
            for (TopExp_Explorer it(one, TopAbs_FACE); it.More(); it.Next())
                faces.push_back(TopoDS::Face(it.Current()));
        }
    }
    if (faces.size() != 1) {
        flat.error = QT_TRANSLATE_NOOP("Model", "The unfolded sheet doesn't come out as one piece (does it overlap itself?)");
        return flat;
    }
    TopoDS_Face top = faces.front();
    // Facing up.
    if (const auto n = planarNormal(top); n && n->Z() < 0.0)
        top.Reverse();

    // Its lower left corner to the origin.
    Bnd_Box box;
    BRepBndLib::AddOptimal(top, box, false, false);
    double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    box.Get(x0, y0, z0, x1, y1, z1);
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(-x0, -y0, 0.0));
    flat.face = TopoDS::Face(BRepBuilderAPI_Transform(top, shift, true).Shape());
    for (FlatBend& b : flat.bends) {
        b.a.Translate(gp_Vec2d(-x0, -y0));
        b.b.Translate(gp_Vec2d(-x0, -y0));
    }
    flat.width = x1 - x0;
    flat.height = y1 - y0;
    flat.solid = BRepPrimAPI_MakePrism(flat.face, gp_Vec(0.0, 0.0, -spec.thickness)).Shape();
    return flat;
}
