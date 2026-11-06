#include "model/Profiles.h"

#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <limits>

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
    }
    return {};
}

std::vector<TopoDS_Face> findRegions(const Sketch& sketch)
{
    if (sketch.entities().empty())
        return {};

    // Split a sheet larger than the sketch by every curve; each piece is a region,
    // except the one around the outside.
    double umin = std::numeric_limits<double>::max(), vmin = umin;
    double umax = -umin, vmax = -umin;
    TopTools_ListOfShape tools;
    for (const SketchEntity& e : sketch.entities()) {
        const TopoDS_Edge edge = toEdge(sketch, e);
        if (edge.IsNull())
            continue;
        tools.Append(edge);
        const double reach = e.kind == EntityKind::Line ? 0.0 : (e.kind == EntityKind::Arc
                                                                     ? sketch.pos(e.c).Distance(sketch.pos(e.a))
                                                                     : e.radius);
        for (PointId p : {e.a, e.b, e.c}) {
            if (p == kNoId)
                continue;
            const gp_Pnt2d q = sketch.pos(p);
            umin = std::min(umin, q.X() - reach);
            umax = std::max(umax, q.X() + reach);
            vmin = std::min(vmin, q.Y() - reach);
            vmax = std::max(vmax, q.Y() + reach);
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

std::optional<std::size_t> regionAt(const std::vector<TopoDS_Face>& regions, const gp_Pnt& p)
{
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (BRepClass_FaceClassifier(regions[i], p, kTolerance).State() == TopAbs_IN)
            return i;
    }
    return std::nullopt;
}
