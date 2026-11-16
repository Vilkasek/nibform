#include "model/Details.h"
#include "model/Tr.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFill_Filling.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <ShapeFix_Solid.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Sphere.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace {

constexpr double kTouch = 1e-4;

TopoDS_Compound compound()
{
    TopoDS_Compound c;
    BRep_Builder().MakeCompound(c);
    return c;
}

double reachOf(const std::vector<TopoDS_Shape>& shapes)
{
    Bnd_Box box;
    for (const TopoDS_Shape& s : shapes)
        BRepBndLib::Add(s, box);
    return box.IsVoid() ? 100.0 : 2.0 * std::sqrt(box.SquareExtent()) + 1.0;
}

std::optional<TopoDS_Shape> run(BRepAlgoAPI_BooleanOperation&& op, const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    TopTools_ListOfShape args, tools;
    args.Append(a);
    tools.Append(b);
    op.SetArguments(args);
    op.SetTools(tools);
    op.Build();
    if (!op.IsDone() || op.HasErrors())
        return std::nullopt;
    return op.Shape();
}

bool meets(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    BRepExtrema_DistShapeShape distance(a, b);
    return distance.IsDone() && distance.Value() < kTouch;
}

// The solids of `shape` that touch `what`, as one compound (empty: none).
std::optional<TopoDS_Shape> solidsTouching(const TopoDS_Shape& shape, const TopoDS_Shape& what)
{
    TopoDS_Compound kept = compound();
    bool any = false;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        if (volumeOf(it.Current()) > 1e-6 && meets(it.Current(), what)) {
            BRep_Builder().Add(kept, it.Current());
            any = true;
        }
    }
    if (!any)
        return std::nullopt;
    return kept;
}

ModelResult failure(const std::vector<TopoDS_Shape>& bodies, const char* error)
{
    ModelResult result;
    result.bodies = bodies;
    result.error = error;
    return result;
}

// The free ends of an open wire: each end vertex with the direction the wire leaves it
// in (outward), as a straight continuation would go.
std::vector<std::pair<gp_Pnt, gp_Dir>> openEnds(const TopoDS_Wire& wire)
{
    TopTools_IndexedDataMapOfShapeListOfShape edgesAt;
    TopExp::MapShapesAndAncestors(wire, TopAbs_VERTEX, TopAbs_EDGE, edgesAt);
    std::vector<std::pair<gp_Pnt, gp_Dir>> ends;
    for (int i = 1; i <= edgesAt.Extent(); ++i) {
        if (edgesAt(i).Extent() != 1)
            continue;
        const TopoDS_Vertex vertex = TopoDS::Vertex(edgesAt.FindKey(i));
        const TopoDS_Edge edge = TopoDS::Edge(edgesAt(i).First());
        const BRepAdaptor_Curve curve(edge);
        const gp_Pnt at = BRep_Tool::Pnt(vertex);
        const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
        const bool atFirst = curve.Value(t0).Distance(at) <= curve.Value(t1).Distance(at);
        gp_Pnt p;
        gp_Vec tangent;
        curve.D1(atFirst ? t0 : t1, p, tangent);
        if (tangent.Magnitude() < 1e-12)
            continue;
        ends.emplace_back(at, gp_Dir(atFirst ? -tangent : tangent));
    }
    return ends;
}

} // namespace

ModelResult draftFaces(const std::vector<TopoDS_Shape>& bodies, const std::vector<std::pair<int, TopoDS_Face>>& faces,
                       const gp_Pln& neutral, double degrees)
{
    if (faces.empty())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "Pick the faces to draft"));
    if (std::abs(degrees) < 1e-9 || std::abs(degrees) >= 89.0)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The draft angle must be between 0° and 89°"));
    std::map<int, std::vector<TopoDS_Face>> byBody;
    for (const auto& [body, face] : faces) {
        if (body >= 0 && body < static_cast<int>(bodies.size()))
            byBody[body].push_back(face);
    }
    ModelResult result;
    result.bodies = bodies;
    const gp_Dir pull = neutral.Axis().Direction();
    for (const auto& [body, list] : byBody) {
        BRepOffsetAPI_DraftAngle draft(bodies[body]);
        for (const TopoDS_Face& face : list) {
            // OCCT tilts a face by the angle in the sense that narrows the part along
            // the pull direction for faces it is told face outward from it.
            draft.Add(face, pull, degrees * M_PI / 180.0, neutral);
            if (!draft.AddDone())
                return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't draft this face: it must cross the neutral plane at an angle"));
        }
        draft.Build();
        if (!draft.IsDone() || !BRepCheck_Analyzer(draft.Shape()).IsValid())
            return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't draft these faces"));
        TopoDS_Shape shape = draft.Shape();
        // A single solid stays a solid (the drafter can hand back a compound of one).
        TopExp_Explorer solid(shape, TopAbs_SOLID);
        if (solid.More()) {
            const TopoDS_Shape first = solid.Current();
            solid.Next();
            if (!solid.More())
                shape = first;
        }
        result.bodies[body] = shape;
    }
    return result;
}

ModelResult ribFrom(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Wire& path, const gp_Dir& normal,
                    double thickness, bool flip)
{
    if (thickness <= 0.0)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The rib needs a thickness"));
    if (bodies.empty())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "A rib needs a body to stand on"));
    const auto ends = openEnds(path);
    if (ends.size() != 2)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "A rib follows an open line (or a chain of curves)"));
    const double reach = reachOf(bodies);

    // The path carried on straight past both ends, so it meets the walls around.
    BRepBuilderAPI_MakeWire longer(path);
    for (const auto& [at, out] : ends)
        longer.Add(BRepBuilderAPI_MakeEdge(at, at.Translated(gp_Vec(out) * reach)).Edge());
    if (!longer.IsDone())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "A rib follows an open line (or a chain of curves)"));

    // Which way is across: square to the chord, in the sketch plane.
    gp_Vec chord(ends[0].first, ends[1].first);
    if (chord.Magnitude() < 1e-9)
        chord = gp_Vec(ends[0].second);
    gp_Vec across = gp_Vec(normal).Crossed(chord);
    if (across.Magnitude() < 1e-9)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The rib's line must lie in its sketch"));
    across.Normalize();

    struct Side
    {
        TopoDS_Shape piece;
        double volume = 0.0;
        bool enclosed = false;
    };
    TopoDS_Compound all = compound();
    for (const TopoDS_Shape& b : bodies)
        BRep_Builder().Add(all, b);
    auto side = [&](const gp_Vec& toward) -> std::optional<Side> {
        // A sheet from the path across, thickened about the sketch plane.
        const TopoDS_Shape sheet = BRepPrimAPI_MakePrism(longer.Wire(), toward * reach).Shape();
        gp_Trsf back;
        back.SetTranslation(gp_Vec(normal) * (-thickness / 2.0));
        const TopoDS_Shape start = BRepBuilderAPI_Transform(sheet, back, true).Shape();
        TopoDS_Shape slab;
        try {
            slab = BRepPrimAPI_MakePrism(start, gp_Vec(normal) * thickness).Shape();
        } catch (const Standard_Failure&) {
            return std::nullopt;
        }
        const auto outside = run(BRepAlgoAPI_Cut(), slab, all);
        if (!outside)
            return std::nullopt;
        // The part of the slab that starts at the path (not the bits past the walls).
        const auto kept = solidsTouching(*outside, path);
        if (!kept)
            return std::nullopt;
        Side s;
        s.piece = *kept;
        s.volume = volumeOf(*kept);
        if (s.volume <= 1e-6)
            return std::nullopt;
        // Enclosed: the body stops it well before the far edge of the sheet.
        Bnd_Box box;
        BRepBndLib::Add(*kept, box);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double farthest = 0.0;
        for (double x : {xmin, xmax})
            for (double y : {ymin, ymax})
                for (double z : {zmin, zmax})
                    farthest = std::max(farthest, gp_Vec(ends[0].first, gp_Pnt(x, y, z)).Dot(toward));
        s.enclosed = farthest < 0.9 * reach;
        return s;
    };
    std::vector<Side> sides;
    for (const gp_Vec& toward : {across, -across}) {
        if (const auto s = side(toward))
            sides.push_back(*s);
    }
    std::erase_if(sides, [](const Side& s) { return !s.enclosed; });
    if (sides.empty())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The rib doesn't meet the body on either side of its line"));
    std::sort(sides.begin(), sides.end(), [](const Side& a, const Side& b) { return a.volume < b.volume; });
    const Side& chosen = flip && sides.size() > 1 ? sides[1] : sides[0];
    ModelResult result = applyTool(bodies, chosen.piece, BodyOperation::Join);
    if (!result.ok())
        result.error = QT_TRANSLATE_NOOP("Model", "Could not join the rib with the body");
    return result;
}

ModelResult embossProfiles(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Face& target,
                           const gp_Pnt& near, const std::vector<TopoDS_Face>& profiles, const gp_Dir& along,
                           double depth, bool engrave)
{
    if (profiles.empty())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "Pick the sketch regions to emboss"));
    if (depth <= 0.0)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The depth must be more than 0"));
    if (body < 0 || body >= static_cast<int>(bodies.size()))
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The face it was on is gone"));
    const TopoDS_Shape& solid = bodies[body];

    // The layer the profiles fill: on a flat face exactly its prism, on a curved one the
    // gap between the body and its offset.
    std::optional<TopoDS_Shape> layer;
    if (const auto normal = planarNormal(target)) {
        layer = BRepPrimAPI_MakePrism(target, gp_Vec(*normal) * (engrave ? -depth : depth)).Shape();
    } else {
        BRepOffsetAPI_MakeOffsetShape offset;
        try {
            offset.PerformByJoin(solid, engrave ? -depth : depth, 1e-6, BRepOffset_Skin, false, false,
                                 GeomAbs_Intersection);
        } catch (const Standard_Failure&) {
        }
        if (!offset.IsDone())
            return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't follow this curved face that deep"));
        layer = engrave ? run(BRepAlgoAPI_Cut(), solid, offset.Shape()) : run(BRepAlgoAPI_Cut(), offset.Shape(), solid);
    }
    if (!layer)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't follow this curved face that deep"));

    // The profiles carried both ways through everything. A column can cross the face
    // more than once (both sides of a cylinder): only the crossing at the picked point
    // counts.
    const double reach = reachOf(bodies);
    gp_Trsf back;
    back.SetTranslation(gp_Vec(along) * -reach);
    TopoDS_Compound kept = compound();
    bool any = false;
    for (const TopoDS_Face& profile : profiles) {
        const TopoDS_Shape start = BRepBuilderAPI_Transform(profile, back, true).Shape();
        const TopoDS_Shape column = BRepPrimAPI_MakePrism(start, gp_Vec(along) * (2.0 * reach)).Shape();
        const auto common = run(BRepAlgoAPI_Common(), column, *layer);
        if (!common)
            return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't project the profiles onto the face"));
        std::vector<std::pair<double, TopoDS_Shape>> pieces; // how far from the pick along the column
        for (TopExp_Explorer it(*common, TopAbs_SOLID); it.More(); it.Next()) {
            if (volumeOf(it.Current()) <= 1e-9 || !meets(it.Current(), target))
                continue;
            Bnd_Box box;
            BRepBndLib::Add(it.Current(), box);
            const gp_Pnt middle((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0);
            pieces.emplace_back(std::abs(gp_Vec(near, middle).Dot(gp_Vec(along))), it.Current());
        }
        if (pieces.empty())
            continue;
        const double nearest = std::min_element(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) {
                                   return a.first < b.first;
                               })->first;
        for (const auto& [distance, piece] : pieces) {
            if (distance <= nearest + 2.0 * depth + 1e-6) {
                BRep_Builder().Add(kept, piece);
                any = true;
            }
        }
    }
    if (!any)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "The profiles don't land on the face"));
    const TopoDS_Shape tool = kept;
    std::vector<TopoDS_Shape> only{solid};
    ModelResult applied = applyTool(only, tool, engrave ? BodyOperation::Cut : BodyOperation::Join);
    if (!applied.ok())
        return failure(bodies, engrave ? QT_TRANSLATE_NOOP("Model", "Could not cut from the body")
                                       : QT_TRANSLATE_NOOP("Model", "Could not join with the body"));
    ModelResult result;
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
        if (i != body)
            result.bodies.push_back(bodies[i]);
    }
    result.bodies.insert(result.bodies.end(), applied.bodies.begin(), applied.bodies.end());
    result.tool = applied.tool;
    return result;
}

ModelResult filletVarying(const std::vector<TopoDS_Shape>& bodies, const std::vector<VaryingEdge>& edges, double first,
                          double second)
{
    if (edges.empty())
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "Pick edges of a body"));
    if (first <= 0.0 || second <= 0.0)
        return failure(bodies, QT_TRANSLATE_NOOP("Model", "Both radii must be more than 0"));
    std::map<int, std::vector<VaryingEdge>> byBody;
    for (const VaryingEdge& e : edges) {
        if (e.edge.body >= 0 && e.edge.body < static_cast<int>(bodies.size()))
            byBody[e.edge.body].push_back(e);
    }
    ModelResult result;
    result.bodies = bodies;
    for (const auto& [body, list] : byBody) {
        BRepFilletAPI_MakeFillet maker(bodies[body]);
        for (const VaryingEdge& e : list) {
            if (e.flipped)
                maker.Add(second, first, e.edge.edge);
            else
                maker.Add(first, second, e.edge.edge);
        }
        try {
            maker.Build();
        } catch (const Standard_Failure&) {
        }
        if (!maker.IsDone() || !BRepCheck_Analyzer(maker.Shape()).IsValid())
            return failure(bodies, QT_TRANSLATE_NOOP("Model", "Can't fillet: the radius is too big for these edges"));
        result.bodies[body] = maker.Shape();
    }
    return result;
}

namespace {

// The corners (vertices of the body) where three or more of `edges` meet.
std::vector<gp_Pnt> filletCorners(const std::vector<TopoDS_Edge>& edges)
{
    std::vector<std::pair<TopoDS_Vertex, int>> counts;
    for (const TopoDS_Edge& edge : edges) {
        TopoDS_Vertex ends[2];
        TopExp::Vertices(edge, ends[0], ends[1]);
        for (int i = 0; i < 2; ++i) {
            if (ends[i].IsNull() || (i == 1 && ends[1].IsSame(ends[0])))
                continue;
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first.IsSame(ends[i]); });
            if (it == counts.end())
                counts.push_back({ends[i], 1});
            else
                ++it->second;
        }
    }
    std::vector<gp_Pnt> corners;
    for (const auto& [vertex, count] : counts) {
        if (count >= 3)
            corners.push_back(BRep_Tool::Pnt(vertex));
    }
    return corners;
}

bool onBall(const TopoDS_Face& face, const gp_Pnt& center, double radius)
{
    const BRepAdaptor_Surface surface(face, false);
    if (surface.GetType() != GeomAbs_Sphere)
        return false;
    const gp_Sphere sphere = surface.Sphere();
    return sphere.Location().Distance(center) < 1e-6 && std::abs(sphere.Radius() - radius) < 1e-6;
}

// Whether the face lies within `body` (sampled across it).
bool faceWithin(const TopoDS_Face& face, const TopoDS_Shape& body)
{
    const BRepAdaptor_Surface surface(face);
    BRepClass3d_SolidClassifier classifier(body);
    constexpr int kSteps = 10;
    for (int i = 1; i < kSteps; ++i) {
        for (int j = 1; j < kSteps; ++j) {
            const double u = surface.FirstUParameter() + (surface.LastUParameter() - surface.FirstUParameter()) * i / kSteps;
            const double v = surface.FirstVParameter() + (surface.LastVParameter() - surface.FirstVParameter()) * j / kSteps;
            if (BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), 1e-7).State() != TopAbs_IN)
                continue; // the surface goes on past the face's edges
            classifier.Perform(surface.Value(u, v), 1e-3);
            if (classifier.State() == TopAbs_OUT)
                return false;
        }
    }
    return true;
}

// `solid` (rounded from `sharp`) with what lies within `setback` of `corner` replaced by
// a smooth patch.
std::optional<TopoDS_Shape> setBack(const TopoDS_Shape& solid, const TopoDS_Shape& sharp, const gp_Pnt& corner,
                                    double setback)
{
    const TopoDS_Shape ball = BRepPrimAPI_MakeSphere(corner, setback).Shape();
    const auto cut = run(BRepAlgoAPI_Cut(), solid, ball);
    if (!cut)
        return std::nullopt;
    int solids = 0;
    for (TopExp_Explorer it(*cut, TopAbs_SOLID); it.More(); it.Next())
        ++solids;
    if (solids != 1)
        return std::nullopt;

    // The ball's faces go; their edges with the faces that stay bound the patch.
    TopTools_IndexedDataMapOfShapeListOfShape facesOfEdge;
    TopExp::MapShapesAndAncestors(*cut, TopAbs_EDGE, TopAbs_FACE, facesOfEdge);
    std::vector<TopoDS_Face> kept;
    bool any = false;
    for (TopExp_Explorer it(*cut, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        if (onBall(face, corner, setback))
            any = true;
        else
            kept.push_back(face);
    }
    if (!any)
        return solid; // the ball misses the body: nothing to set back

    BRepFill_Filling filling(3, 15, 2, false, 1e-5, 1e-4, 1e-2, 0.1, 8, 9);
    int bounds = 0;
    for (int i = 1; i <= facesOfEdge.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(facesOfEdge.FindKey(i));
        if (BRep_Tool::Degenerated(edge))
            continue;
        TopoDS_Face ballSide, other;
        for (const TopoDS_Shape& f : facesOfEdge(i)) {
            const TopoDS_Face face = TopoDS::Face(f);
            (onBall(face, corner, setback) ? ballSide : other) = face;
        }
        if (ballSide.IsNull() || other.IsNull())
            continue;
        filling.Add(edge, other, GeomAbs_G1, true);
        ++bounds;
    }
    if (bounds < 2)
        return std::nullopt;
    TopoDS_Face patch;
    try {
        filling.Build();
        // Tangent to what is around it (within about 1°), and no further out than the
        // corner before rounding.
        if (!filling.IsDone() || filling.G1Error() > 0.02)
            return std::nullopt;
        patch = filling.Face();
        if (!faceWithin(patch, sharp))
            return std::nullopt;
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }

    BRepBuilderAPI_Sewing sewing(std::max(1e-4, 2.0 * filling.G0Error()));
    for (const TopoDS_Face& face : kept)
        sewing.Add(face);
    sewing.Add(patch);
    try {
        sewing.Perform();
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    TopoDS_Shell shell;
    int shells = 0;
    for (TopExp_Explorer it(sewing.SewedShape(), TopAbs_SHELL); it.More(); it.Next()) {
        shell = TopoDS::Shell(it.Current());
        ++shells;
    }
    if (shells != 1 || !BRep_Tool::IsClosed(shell))
        return std::nullopt;
    ShapeFix_Solid fix;
    fix.SetPrecision(1e-4);
    const TopoDS_Solid made = fix.SolidFromShell(shell);
    if (made.IsNull() || !BRepCheck_Analyzer(made).IsValid())
        return std::nullopt;
    // The patch closes over what the ball took, not into the rest.
    if (volumeOf(made) < volumeOf(*cut) - 1e-6)
        return std::nullopt;
    return made;
}

} // namespace

ModelResult setbackCorners(const std::vector<TopoDS_Shape>& before, const ModelResult& rounded,
                           const std::vector<EdgeRef>& edges, double setback)
{
    if (!rounded.ok() || setback <= 0.0)
        return rounded;
    std::map<int, std::vector<TopoDS_Edge>> byBody;
    for (const EdgeRef& e : edges) {
        if (e.body >= 0 && e.body < static_cast<int>(before.size()))
            byBody[e.body].push_back(e.edge);
    }
    ModelResult result = rounded;
    for (const auto& [body, list] : byBody) {
        if (body >= static_cast<int>(result.bodies.size()))
            continue;
        for (const gp_Pnt& corner : filletCorners(list)) {
            std::optional<TopoDS_Shape> made;
            try {
                made = setBack(result.bodies[body], before[body], corner, setback);
            } catch (const Standard_Failure&) {
            }
            if (!made)
                return failure(before, QT_TRANSLATE_NOOP("Model", "Can't set back this corner: try a smaller setback"));
            result.bodies[body] = *made;
        }
    }
    return result;
}
