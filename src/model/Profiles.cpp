#include "model/Profiles.h"

#include "sketch/Geometry.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <GeomAPI.hxx>

#include <algorithm>
#include <limits>
#include <set>

namespace {

constexpr double kTolerance = 1e-6;

// The splitter leaves curves that bound nothing (dangling ends, stray lines inside a
// region) as INTERNAL edges of the face; extruding those would create inner walls.
TopoDS_Face withoutInternalEdges(const TopoDS_Face& face)
{
    BRep_Builder builder;
    TopoDS_Face out = TopoDS::Face(face.EmptyCopied());
    for (TopoDS_Iterator wire(face, false, false); wire.More(); wire.Next()) {
        if (wire.Value().ShapeType() != TopAbs_WIRE)
            continue;
        TopoDS_Wire kept;
        builder.MakeWire(kept);
        int edges = 0;
        for (TopoDS_Iterator edge(wire.Value(), false, false); edge.More(); edge.Next()) {
            const TopAbs_Orientation o = edge.Value().Orientation();
            if (o == TopAbs_FORWARD || o == TopAbs_REVERSED) {
                builder.Add(kept, edge.Value());
                ++edges;
            }
        }
        if (edges == 0)
            continue;
        kept.Closed(BRep_Tool::IsClosed(kept));
        kept.Orientation(wire.Value().Orientation());
        builder.Add(out, kept);
    }
    return out;
}

} // namespace

TopoDS_Edge toEdge(const Sketch& sketch, const SketchEntity& e)
{
    switch (e.kind) {
    case EntityKind::Line:
        return BRepBuilderAPI_MakeEdge(sketch.to3d(sketch.pos(e.a)), sketch.to3d(sketch.pos(e.b))).Edge();
    case EntityKind::Circle:
    case EntityKind::Arc: {
        // Counter-clockwise in the sketch is a positive turn about the plane's normal.
        const gp_Ax3& plane = sketch.plane();
        const gp_Ax2 axes(sketch.to3d(sketch.pos(e.c)), plane.Direction(), plane.XDirection());
        const double r = e.kind == EntityKind::Arc ? sketch.pos(e.c).Distance(sketch.pos(e.a)) : e.radius;
        const gp_Circ circle(axes, r);
        if (e.kind == EntityKind::Circle)
            return BRepBuilderAPI_MakeEdge(circle).Edge();
        return BRepBuilderAPI_MakeEdge(circle, sketch.to3d(sketch.pos(e.a)), sketch.to3d(sketch.pos(e.b))).Edge();
    }
    case EntityKind::Ellipse:
    case EntityKind::Spline: {
        const Handle(Geom2d_Curve) curve = curve2d(sketch, e);
        if (curve.IsNull())
            return {};
        BRepBuilderAPI_MakeEdge edge(GeomAPI::To3d(curve, gp_Pln(sketch.plane())));
        return edge.IsDone() ? edge.Edge() : TopoDS_Edge();
    }
    }
    return {};
}

std::vector<TopoDS_Face> findRegions(const Sketch& sketch)
{
    if (std::none_of(sketch.entities().begin(), sketch.entities().end(),
                     [](const SketchEntity& e) { return !e.construction; }))
        return {};

    // Split a sheet larger than the sketch by every curve; each piece is a region,
    // except the one around the outside.
    double umin = std::numeric_limits<double>::max(), vmin = umin;
    double umax = -umin, vmax = -umin;
    TopTools_ListOfShape tools;
    for (const SketchEntity& e : sketch.entities()) {
        if (e.construction)
            continue;
        const TopoDS_Edge edge = toEdge(sketch, e);
        if (edge.IsNull())
            continue;
        tools.Append(edge);
        for (const gp_Pnt2d& q : sketch.polyline(e)) {
            umin = std::min(umin, q.X());
            umax = std::max(umax, q.X());
            vmin = std::min(vmin, q.Y());
            vmax = std::max(vmax, q.Y());
        }
    }
    const double margin = std::max({umax - umin, vmax - vmin, 1.0});
    const gp_Pln plane(sketch.plane());
    TopTools_ListOfShape sheet;
    sheet.Append(BRepBuilderAPI_MakeFace(plane, umin - margin, umax + margin, vmin - margin, vmax + margin).Face());

    BRepAlgoAPI_Splitter splitter;
    splitter.SetArguments(sheet);
    splitter.SetTools(tools);
    splitter.SetFuzzyValue(kTolerance);
    splitter.Build();
    if (!splitter.IsDone())
        return {};

    const gp_Pnt outside = sketch.to3d(gp_Pnt2d(umin - margin / 2.0, vmin - margin / 2.0));
    std::vector<TopoDS_Face> regions;
    for (TopExp_Explorer it(splitter.Shape(), TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        if (BRepClass_FaceClassifier(face, outside, kTolerance).State() == TopAbs_IN)
            continue;
        regions.push_back(withoutInternalEdges(face));
    }
    return regions;
}

void projectFaceEdges(Sketch& sketch, const TopoDS_Shape& face)
{
    projectEdges(sketch, face, true);
}

std::set<EntityId> projectEdges(Sketch& sketch, const TopoDS_Shape& shape, bool construction)
{
    const gp_Ax3& plane = sketch.plane();
    const gp_Dir normal = plane.Direction();
    std::set<EntityId> added;
    std::set<PointId> pins;
    std::vector<SketchConstraint> circles; // circles: center and radius pinned together

    auto onPlane = [&](const gp_Pnt& p) { return sketch.to2d(p); };
    auto point = [&](const gp_Pnt2d& p) { return sketch.pointAt(p, 1e-6); };
    // The same curve projected twice (or already in the sketch) is added once.
    auto exists = [&](EntityKind kind, PointId a, PointId b, PointId c, double radius) {
        for (const SketchEntity& e : sketch.entities()) {
            if (e.kind != kind || e.c != c)
                continue;
            const bool ends = (e.a == a && e.b == b) || (kind == EntityKind::Line && e.a == b && e.b == a);
            if (ends && (kind != EntityKind::Circle || std::abs(e.radius - radius) < 1e-6))
                return true;
        }
        return false;
    };
    auto line = [&](const gp_Pnt2d& p, const gp_Pnt2d& q) {
        if (p.Distance(q) < 1e-6)
            return;
        const PointId a = point(p), b = point(q);
        if (a == b || exists(EntityKind::Line, a, b, kNoId, 0.0))
            return;
        added.insert(sketch.addLine(a, b));
        pins.insert(a);
        pins.insert(b);
    };
    // Anything else: a spline through points along it, pinned.
    auto spline = [&](const BRepAdaptor_Curve& curve, bool closed) {
        constexpr int kPieces = 12;
        const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
        std::vector<PointId> through;
        for (int k = 0; k <= (closed ? kPieces - 1 : kPieces); ++k) {
            const PointId p = point(onPlane(curve.Value(t0 + (t1 - t0) * k / kPieces)));
            if (through.empty() || through.back() != p)
                through.push_back(p);
        }
        if (through.size() < 2 || (closed && through.size() < 3))
            return;
        added.insert(sketch.addSpline(through, closed));
        pins.insert(through.begin(), through.end());
    };

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(i));
        if (BRep_Tool::Degenerated(edge))
            continue;
        // Seams of round faces aren't outlines.
        if (shape.ShapeType() == TopAbs_FACE && BRep_Tool::IsClosed(edge, TopoDS::Face(shape)))
            continue;
        const BRepAdaptor_Curve curve(edge);
        const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
        const gp_Pnt start = curve.Value(t0), end = curve.Value(t1);
        const bool closed = start.Distance(end) < 1e-7;

        if (curve.GetType() == GeomAbs_Line) {
            line(onPlane(start), onPlane(end));
            continue;
        }
        if (curve.GetType() == GeomAbs_Circle) {
            const gp_Circ c = curve.Circle();
            const double r = c.Radius();
            const double facing = std::abs(c.Axis().Direction().Dot(normal));
            const gp_Pnt2d center = onPlane(c.Location());
            if (facing > 1.0 - 1e-9) {
                // Square to the sketch: stays a circle / an arc.
                const PointId cp = point(center);
                if (closed) {
                    if (!exists(EntityKind::Circle, kNoId, kNoId, cp, r)) {
                        const EntityId id = sketch.addCircle(cp, r);
                        added.insert(id);
                        SketchConstraint fix;
                        fix.kind = ConstraintKind::Fixed;
                        fix.p1 = cp;
                        fix.e1 = id;
                        fix.value = r;
                        circles.push_back(fix);
                    }
                    continue;
                }
                // Stored counter-clockwise in the sketch: check which way the edge runs.
                const gp_Pnt2d a = onPlane(start), b = onPlane(end);
                const gp_Pnt2d mid = onPlane(curve.Value((t0 + t1) / 2.0));
                const bool ccw = isOnCcwArc(center, a, b, mid);
                const PointId from = point(ccw ? a : b), to = point(ccw ? b : a);
                if (from != to && !exists(EntityKind::Arc, from, to, cp, 0.0)) {
                    added.insert(sketch.addArc(cp, from, to));
                    pins.insert({cp, from, to});
                }
                continue;
            }
            // Seen at a slant: a circle becomes an ellipse, edge-on a line.
            const gp_Dir axis = c.Axis().Direction();
            const gp_Vec along = gp_Vec(normal).Crossed(gp_Vec(axis));
            if (facing < 1e-9) {
                if (along.Magnitude() > 1e-12) {
                    const gp_Vec u = along.Normalized() * r;
                    line(onPlane(c.Location().Translated(-u)), onPlane(c.Location().Translated(u)));
                }
                continue;
            }
            if (closed) {
                const gp_Vec major = along.Normalized() * r;
                const gp_Vec minor = gp_Vec(axis).Crossed(along.Normalized()) * r;
                const PointId cp = point(center);
                const PointId a = point(onPlane(c.Location().Translated(major)));
                const PointId b = point(onPlane(c.Location().Translated(minor)));
                if (a != b && a != cp && b != cp) {
                    added.insert(sketch.addEllipse(cp, a, b));
                    pins.insert({cp, a, b});
                }
                continue;
            }
        }
        spline(curve, closed);
    }

    // Pinned where the body is, and reference geometry unless asked otherwise.
    Sketch::Contents contents = sketch.contents();
    for (SketchEntity& e : contents.entities) {
        if (added.count(e.id))
            e.construction = construction;
    }
    sketch = Sketch(sketch.plane(), std::move(contents));
    for (PointId p : pins) {
        SketchConstraint fix;
        fix.kind = ConstraintKind::Fixed;
        fix.p1 = p;
        fix.target = sketch.pos(p);
        sketch.addConstraint(fix);
    }
    for (SketchConstraint fix : circles) {
        fix.target = sketch.pos(fix.p1);
        sketch.addConstraint(fix);
    }
    return added;
}

std::optional<std::size_t> regionAt(const std::vector<TopoDS_Face>& regions, const gp_Pnt& p)
{
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (BRepClass_FaceClassifier(regions[i], p, kTolerance).State() == TopAbs_IN)
            return i;
    }
    return std::nullopt;
}
