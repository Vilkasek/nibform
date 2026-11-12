#include "model/Solids.h"
#include "model/Tr.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRep_Tool.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRep_Builder.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepTools.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <Bnd_Box.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>

namespace {

constexpr double kMinVolume = 1e-6; // mm³: anything smaller is a touch, not an overlap

// Merges coplanar faces and collinear edges a boolean leaves behind, so the
// result can be pushed / pulled face by face again.
TopoDS_Shape tidy(const TopoDS_Shape& shape)
{
    ShapeUpgrade_UnifySameDomain unify(shape, true, true, false);
    unify.Build();
    return unify.Shape();
}

// Whether a boolean's result is fit to build on. The faces it kept from its inputs were
// checked when those were made, so only the new ones are (and that the shells still
// close): on a body with hundreds of faces, checking all of them took as long as the
// boolean itself.
bool sound(const TopoDS_Shape& result, const TopTools_ListOfShape& inputs)
{
    TopTools_MapOfShape known;
    for (const TopoDS_Shape& input : inputs) {
        for (TopExp_Explorer f(input, TopAbs_FACE); f.More(); f.Next())
            known.Add(f.Current());
    }
    TopoDS_Compound fresh;
    BRep_Builder builder;
    builder.MakeCompound(fresh);
    for (TopExp_Explorer f(result, TopAbs_FACE); f.More(); f.Next()) {
        if (!known.Contains(f.Current()))
            builder.Add(fresh, f.Current());
    }
    if (!BRepCheck_Analyzer(fresh).IsValid())
        return false;
    for (TopExp_Explorer s(result, TopAbs_SHELL); s.More(); s.Next()) {
        BRepCheck_Shell shell(TopoDS::Shell(s.Current()));
        if (shell.Closed() != BRepCheck_NoError)
            return false;
    }
    return true;
}

std::optional<TopoDS_Shape> boolean(BooleanKind kind, const TopoDS_Shape& a, const TopTools_ListOfShape& tools)
{
    TopTools_ListOfShape args;
    args.Append(a);
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> op;
    switch (kind) {
    case BooleanKind::Union:
        op = std::make_unique<BRepAlgoAPI_Fuse>();
        break;
    case BooleanKind::Subtract:
        op = std::make_unique<BRepAlgoAPI_Cut>();
        break;
    case BooleanKind::Intersect:
        op = std::make_unique<BRepAlgoAPI_Common>();
        break;
    }
    op->SetArguments(args);
    op->SetTools(tools);
    op->Build();
    if (!op->IsDone() || op->HasErrors())
        return std::nullopt;
    TopoDS_Shape result = tidy(op->Shape());
    TopTools_ListOfShape inputs = tools;
    inputs.Append(a);
    if (!sound(result, inputs))
        return std::nullopt;
    return result;
}

std::optional<TopoDS_Shape> boolean(BooleanKind kind, const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    TopTools_ListOfShape tools;
    tools.Append(b);
    return boolean(kind, a, tools);
}

// Each solid of `shape` as a body of its own.
void appendSolids(std::vector<TopoDS_Shape>& out, const TopoDS_Shape& shape)
{
    // Slivers have a handful of faces; a body with many is no sliver, and measuring its
    // volume to find that out costs as much as a boolean.
    constexpr int kSliverFaces = 32;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        int faces = 0;
        for (TopExp_Explorer f(it.Current(), TopAbs_FACE); f.More() && faces <= kSliverFaces; f.Next())
            ++faces;
        if (faces > kSliverFaces || volumeOf(it.Current()) > kMinVolume)
            out.push_back(it.Current());
    }
}

// Whether two shapes are certainly apart (their boxes don't meet): then neither overlap()
// nor touches() needs to run a boolean or a distance search on them.
bool apart(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    Bnd_Box boxA, boxB;
    BRepBndLib::Add(a, boxA, false);
    BRepBndLib::Add(b, boxB, false);
    boxA.Enlarge(1e-3);
    return boxA.IsOut(boxB);
}

double overlap(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (apart(a, b))
        return 0.0;
    const auto common = boolean(BooleanKind::Intersect, a, b);
    return common ? volumeOf(*common) : 0.0;
}

bool touches(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (apart(a, b))
        return false;
    BRepExtrema_DistShapeShape distance(a, b);
    return distance.IsDone() && distance.Value() < 1e-6;
}

// Applies `round` (a fillet or chamfer maker filled with the body's edges) body by body.
template <typename Maker>
ModelResult roundEdges(const std::vector<TopoDS_Shape>& bodies, const std::vector<EdgeRef>& edges, double size,
                       const char* failure)
{
    ModelResult result;
    result.bodies = bodies;
    if (edges.empty() || size <= 0.0) {
        result.error = QT_TRANSLATE_NOOP("Model", "Pick edges of a body");
        return result;
    }
    std::map<int, std::vector<TopoDS_Edge>> byBody;
    for (const EdgeRef& e : edges) {
        if (e.body >= 0 && e.body < static_cast<int>(bodies.size()))
            byBody[e.body].push_back(e.edge);
    }
    for (const auto& [body, list] : byBody) {
        Maker maker(bodies[body]);
        for (const TopoDS_Edge& edge : list)
            maker.Add(size, edge);
        try {
            maker.Build();
        } catch (const Standard_Failure&) {
            // OCCT also throws for impossible sizes; IsDone() below reports it.
        }
        if (!maker.IsDone() || !BRepCheck_Analyzer(maker.Shape()).IsValid()) {
            result.error = failure;
            return result;
        }
        result.bodies[body] = maker.Shape();
    }
    return result;
}

} // namespace

std::optional<gp_Dir> planarNormal(const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face, false);
    if (surface.GetType() != GeomAbs_Plane)
        return std::nullopt;
    gp_Dir n = surface.Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    return n;
}

std::optional<std::pair<gp_Pnt, gp_Dir>> edgeHandle(const TopoDS_Shape& body, const TopoDS_Edge& edge)
{
    TopTools_IndexedDataMapOfShapeListOfShape faces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, faces);
    const int index = faces.FindIndex(edge);
    if (index == 0 || faces(index).Extent() != 2)
        return std::nullopt;

    const BRepAdaptor_Curve curve(edge);
    const gp_Pnt middle = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);

    // Outward normals of both faces at the edge's middle; into the material is minus their sum.
    gp_Vec sum(0, 0, 0);
    for (const TopoDS_Shape& f : faces(index)) {
        const TopoDS_Face face = TopoDS::Face(f);
        const Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
        const gp_Pnt2d uv = ShapeAnalysis_Surface(surface).ValueOfUV(middle, 1e-7);
        BRepLProp_SLProps props(BRepAdaptor_Surface(face), uv.X(), uv.Y(), 1, 1e-9);
        if (!props.IsNormalDefined())
            return std::nullopt;
        gp_Dir n = props.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
            n.Reverse();
        sum += gp_Vec(n);
    }
    if (sum.Magnitude() < 1e-9)
        return std::nullopt; // the faces are flat to each other: no corner to round
    return std::pair(middle, gp_Dir(-sum));
}

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return std::abs(props.Mass());
}

bool startsInside(const std::vector<TopoDS_Shape>& bodies, const std::vector<Profile>& profiles, double distance)
{
    // A thin slab off each profile, the way the extrusion goes: mostly in a body means the
    // extrusion begins in material (a sketch on a face pushed in, or a sketch inside a
    // body), however far it goes after that.
    if (std::abs(distance) < 1e-9)
        return false;
    const double depth = std::copysign(std::min(std::abs(distance), 0.05), distance);
    double total = 0.0;
    double inside = 0.0;
    for (const Profile& p : profiles) {
        BRepPrimAPI_MakePrism slab(p.face, gp_Vec(p.normal) * depth);
        if (!slab.IsDone())
            continue;
        total += volumeOf(slab.Shape());
        for (const TopoDS_Shape& body : bodies)
            inside += overlap(body, slab.Shape());
    }
    // Some of it is enough: a circle across a face's edge cuts a half-round notch.
    return total > 0.0 && inside > 0.1 * total;
}

ModelResult extrude(const std::vector<TopoDS_Shape>& bodies, const std::vector<Profile>& profiles,
                    double distance, BodyOperation operation)
{
    ModelResult result;
    if (profiles.empty() || std::abs(distance) < 1e-9) {
        result.error = QT_TRANSLATE_NOOP("Model", "Nothing to extrude");
        return result;
    }

    // One prism per profile, grouped by the body its face came from (-1: sketch).
    std::map<int, TopTools_ListOfShape> prisms;
    std::map<int, std::vector<Profile>> groups;
    for (const Profile& p : profiles) {
        groups[p.body].push_back(p);
        BRepPrimAPI_MakePrism prism(p.face, gp_Vec(p.normal) * distance);
        if (!prism.IsDone()) {
            result.error = QT_TRANSLATE_NOOP("Model", "Could not extrude the profile");
            return result;
        }
        prisms[p.body].Append(prism.Shape());
    }

    const std::vector<TopoDS_Shape>& current = bodies;
    std::set<int> consumed; // bodies replaced by a result
    std::vector<TopoDS_Shape> added;

    for (auto& [owner, list] : prisms) {
        // Several touching profiles extrude into one tool.
        TopoDS_Shape tool = list.First();
        if (list.Extent() > 1) {
            TopTools_ListOfShape rest = list;
            rest.RemoveFirst();
            const auto fused = boolean(BooleanKind::Union, tool, rest);
            if (!fused) {
                result.error = QT_TRANSLATE_NOOP("Model", "Could not merge the profiles");
                return result;
            }
            tool = *fused;
        }

        // Which bodies the tool meets (overlapping, or just touching, e.g. a sketch on a
        // body's face pulled outward), and what to do with them.
        std::vector<int> touched;
        std::vector<TopoDS_Shape> untouched; // not yet replaced by a result
        double inside = 0.0;
        if (owner >= 0) {
            touched.push_back(owner);
        } else if (operation != BodyOperation::NewBody) {
            for (int i = 0; i < static_cast<int>(current.size()); ++i) {
                if (consumed.count(i))
                    continue;
                untouched.push_back(current[i]);
                const double v = overlap(current[i], tool);
                if (v > kMinVolume || touches(current[i], tool)) {
                    touched.push_back(i);
                    inside += v;
                }
            }
        }

        BodyOperation op = operation;
        if (op == BodyOperation::Auto) {
            if (owner >= 0)
                op = distance > 0.0 ? BodyOperation::Join : BodyOperation::Cut;
            else if (touched.empty())
                op = BodyOperation::NewBody;
            else if (inside >= 0.99 * volumeOf(tool) || startsInside(untouched, groups[owner], distance))
                op = BodyOperation::Cut;
            else
                op = BodyOperation::Join;
        }
        if (touched.empty() && op != BodyOperation::Cut)
            op = BodyOperation::NewBody;
        if (owner < 0 && prisms.size() == 1)
            result.tool = FeatureTool{tool, op}; // from a sketch: a pattern can repeat it

        switch (op) {
        case BodyOperation::NewBody:
            appendSolids(added, tool);
            break;
        case BodyOperation::Join: {
            TopTools_ListOfShape others;
            for (std::size_t k = 1; k < touched.size(); ++k)
                others.Append(current[touched[k]]);
            others.Append(tool);
            const auto fused = boolean(BooleanKind::Union, current[touched[0]], others);
            if (!fused) {
                result.error = QT_TRANSLATE_NOOP("Model", "Could not join with the body");
                return result;
            }
            for (int i : touched)
                consumed.insert(i);
            appendSolids(added, *fused);
            break;
        }
        case BodyOperation::Cut:
            if (touched.empty()) {
                result.error = QT_TRANSLATE_NOOP("Model", "The cut doesn't reach any body");
                return result;
            }
            for (int i : touched) {
                const auto cut = boolean(BooleanKind::Subtract, current[i], tool);
                if (!cut) {
                    result.error = QT_TRANSLATE_NOOP("Model", "Could not cut from the body");
                    return result;
                }
                consumed.insert(i);
                appendSolids(added, *cut); // may split in pieces, or vanish
            }
            break;
        case BodyOperation::Auto:
            break;
        }
    }

    for (int i = 0; i < static_cast<int>(current.size()); ++i) {
        if (!consumed.count(i))
            result.bodies.push_back(current[i]);
    }
    result.bodies.insert(result.bodies.end(), added.begin(), added.end());
    return result;
}

ModelResult filletEdges(const std::vector<TopoDS_Shape>& bodies, const std::vector<EdgeRef>& edges, double radius)
{
    return roundEdges<BRepFilletAPI_MakeFillet>(bodies, edges, radius,
                                                QT_TRANSLATE_NOOP("Model", "Can't fillet: the radius is too big for these edges"));
}

ModelResult chamferEdges(const std::vector<TopoDS_Shape>& bodies, const std::vector<EdgeRef>& edges,
                         double distance)
{
    return roundEdges<BRepFilletAPI_MakeChamfer>(bodies, edges, distance,
                                                 QT_TRANSLATE_NOOP("Model", "Can't chamfer: the distance is too big for these edges"));
}

namespace {

// Shell built from booleans, for bodies OCCT's thick-solid maker gives up on (an opened
// face carrying a boss, holes through it...): the body minus its inward offset (the
// core), minus, for each opened face, the core's matching face pulled out through it.
std::optional<TopoDS_Shape> shellByOffset(const TopoDS_Shape& body, const TopTools_ListOfShape& open, double thickness)
{
    BRepOffsetAPI_MakeOffsetShape core;
    try {
        core.PerformByJoin(body, -thickness, 1e-4, BRepOffset_Skin, false, false, GeomAbs_Intersection);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    if (!core.IsDone() || volumeOf(core.Shape()) <= kMinVolume)
        return std::nullopt;

    TopTools_ListOfShape cut;
    cut.Append(core.Shape());
    for (const TopoDS_Shape& shape : open) {
        const TopoDS_Face face = TopoDS::Face(shape);
        const auto normal = planarNormal(face);
        if (!normal)
            return std::nullopt;
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        const gp_Pnt middle = props.CentreOfMass();
        // The core's face lying `thickness` under this one, facing the same way.
        TopoDS_Face inner;
        double nearest = std::numeric_limits<double>::max();
        for (TopExp_Explorer it(core.Shape(), TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face candidate = TopoDS::Face(it.Current());
            const auto n = planarNormal(candidate);
            if (!n || n->Dot(*normal) < 0.9999)
                continue;
            GProp_GProps p;
            BRepGProp::SurfaceProperties(candidate, p);
            const double depth = gp_Vec(p.CentreOfMass(), middle).Dot(gp_Vec(*normal));
            if (std::abs(depth - thickness) > 1e-4)
                continue;
            if (p.CentreOfMass().Distance(middle) < nearest) {
                nearest = p.CentreOfMass().Distance(middle);
                inner = candidate;
            }
        }
        if (inner.IsNull())
            return std::nullopt;
        cut.Append(BRepPrimAPI_MakePrism(inner, gp_Vec(*normal) * (2.0 * thickness)).Shape());
    }
    return boolean(BooleanKind::Subtract, body, cut);
}

} // namespace

ModelResult shellBodies(const std::vector<TopoDS_Shape>& bodies, const std::vector<std::pair<int, TopoDS_Face>>& open,
                        double thickness)
{
    ModelResult result;
    result.bodies = bodies;
    if (open.empty() || thickness <= 0.0) {
        result.error = QT_TRANSLATE_NOOP("Model", "Pick the faces to open");
        return result;
    }
    std::map<int, TopTools_ListOfShape> byBody;
    for (const auto& [body, face] : open) {
        if (body >= 0 && body < static_cast<int>(bodies.size()))
            byBody[body].Append(face);
    }
    for (const auto& [body, faces] : byBody) {
        // Too thick a wall can also come back as the untouched body: a shell has to
        // take material away.
        const double before = volumeOf(bodies[body]);
        auto usable = [&](const TopoDS_Shape& shape) {
            const double v = volumeOf(shape);
            return BRepCheck_Analyzer(shape).IsValid() && v > kMinVolume && v < before - kMinVolume;
        };
        BRepOffsetAPI_MakeThickSolid maker;
        try {
            // Negative offset: the walls grow inward, the outside stays where it was.
            maker.MakeThickSolidByJoin(bodies[body], faces, -thickness, 1e-4);
            maker.Build();
        } catch (const Standard_Failure&) {
            // Falls back to the offset below, as when it isn't done.
        }
        if (maker.IsDone() && usable(maker.Shape())) {
            result.bodies[body] = maker.Shape();
            continue;
        }
        const auto fallback = shellByOffset(bodies[body], faces, thickness);
        if (!fallback || !usable(*fallback)) {
            result.error = QT_TRANSLATE_NOOP("Model", "Can't shell this body; try thinner walls");
            return result;
        }
        result.bodies[body] = *fallback;
    }
    return result;
}

ModelResult combine(const std::vector<TopoDS_Shape>& bodies, const std::vector<int>& selected, BooleanKind kind)
{
    ModelResult result;
    std::vector<int> unique;
    for (int i : selected) {
        if (i >= 0 && i < static_cast<int>(bodies.size()) && std::find(unique.begin(), unique.end(), i) == unique.end())
            unique.push_back(i);
    }
    if (unique.size() < 2) {
        result.error = QT_TRANSLATE_NOOP("Model", "Pick at least two bodies");
        return result;
    }
    TopTools_ListOfShape tools;
    for (std::size_t k = 1; k < unique.size(); ++k)
        tools.Append(bodies[unique[k]]);
    const auto shape = boolean(kind, bodies[unique[0]], tools);
    if (!shape) {
        result.error = QT_TRANSLATE_NOOP("Model", "The body operation failed");
        return result;
    }
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
        if (std::find(unique.begin(), unique.end(), i) == unique.end())
            result.bodies.push_back(bodies[i]);
    }
    const std::size_t before = result.bodies.size();
    appendSolids(result.bodies, *shape);
    if (result.bodies.size() == before && kind == BooleanKind::Intersect)
        result.error = QT_TRANSLATE_NOOP("Model", "The bodies don't intersect");
    return result;
}

// --- Tool solids --------------------------------------------------------------------

ModelResult applyTool(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Shape& tool, BodyOperation operation)
{
    ModelResult result;
    std::vector<int> touched;
    double inside = 0.0;
    if (operation != BodyOperation::NewBody) {
        for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
            const double v = overlap(bodies[i], tool);
            if (v > kMinVolume || touches(bodies[i], tool)) {
                touched.push_back(i);
                inside += v;
            }
        }
    }
    BodyOperation op = operation;
    if (op == BodyOperation::Auto) {
        if (touched.empty())
            op = BodyOperation::NewBody;
        else if (inside >= 0.99 * volumeOf(tool))
            op = BodyOperation::Cut;
        else
            op = BodyOperation::Join;
    }
    if (touched.empty() && op == BodyOperation::Join)
        op = BodyOperation::NewBody;
    if (touched.empty() && op == BodyOperation::Cut) {
        result.bodies = bodies;
        result.error = QT_TRANSLATE_NOOP("Model", "The cut doesn't reach any body");
        return result;
    }

    std::vector<TopoDS_Shape> added;
    std::set<int> consumed;
    switch (op) {
    case BodyOperation::NewBody:
    case BodyOperation::Auto:
        appendSolids(added, tool);
        break;
    case BodyOperation::Join: {
        TopTools_ListOfShape others;
        for (std::size_t k = 1; k < touched.size(); ++k)
            others.Append(bodies[touched[k]]);
        others.Append(tool);
        const auto fused = boolean(BooleanKind::Union, bodies[touched[0]], others);
        if (!fused) {
            result.bodies = bodies;
            result.error = QT_TRANSLATE_NOOP("Model", "Could not join with the body");
            return result;
        }
        consumed.insert(touched.begin(), touched.end());
        appendSolids(added, *fused);
        break;
    }
    case BodyOperation::Cut:
        for (int i : touched) {
            const auto cut = boolean(BooleanKind::Subtract, bodies[i], tool);
            if (!cut) {
                result.bodies = bodies;
                result.error = QT_TRANSLATE_NOOP("Model", "Could not cut from the body");
                return result;
            }
            consumed.insert(i);
            appendSolids(added, *cut);
        }
        break;
    }
    for (int i = 0; i < static_cast<int>(bodies.size()); ++i) {
        if (!consumed.count(i))
            result.bodies.push_back(bodies[i]);
    }
    result.bodies.insert(result.bodies.end(), added.begin(), added.end());
    result.tool = FeatureTool{tool, op};
    return result;
}

namespace {

// One solid out of several (each profile's own), or the only one.
std::optional<TopoDS_Shape> fuseAll(const std::vector<TopoDS_Shape>& solids)
{
    if (solids.empty())
        return std::nullopt;
    if (solids.size() == 1)
        return solids.front();
    TopTools_ListOfShape rest;
    for (std::size_t i = 1; i < solids.size(); ++i)
        rest.Append(solids[i]);
    return boolean(BooleanKind::Union, solids.front(), rest);
}

bool usableSolid(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && BRepCheck_Analyzer(shape).IsValid() && volumeOf(shape) > kMinVolume;
}

} // namespace

std::optional<TopoDS_Shape> revolveProfiles(const std::vector<TopoDS_Face>& profiles, const gp_Ax1& axis,
                                            double degrees, std::string& error)
{
    if (profiles.empty() || std::abs(degrees) < 1e-6) {
        error = QT_TRANSLATE_NOOP("Model", "Nothing to revolve");
        return std::nullopt;
    }
    const double angle = std::clamp(degrees, -360.0, 360.0) * M_PI / 180.0;
    std::vector<TopoDS_Shape> solids;
    for (const TopoDS_Face& face : profiles) {
        // An axis through the profile's inside would make a self-intersecting solid.
        Bnd_Box box;
        BRepBndLib::Add(face, box);
        const double reach = std::sqrt(box.SquareExtent());
        const gp_Pnt middle = box.IsVoid() ? axis.Location() : gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0);
        const double t0 = gp_Vec(axis.Location(), middle).Dot(gp_Vec(axis.Direction()));
        for (int k = -50; k <= 50; ++k) {
            const gp_Pnt p = axis.Location().Translated(gp_Vec(axis.Direction()) * (t0 + reach * k / 50.0));
            if (BRepClass_FaceClassifier(face, p, 1e-6).State() == TopAbs_IN) {
                error = QT_TRANSLATE_NOOP("Model", "Can't revolve: the axis must not run through the profile");
                return std::nullopt;
            }
        }
        BRepPrimAPI_MakeRevol revol(face, axis, std::abs(angle) >= 2.0 * M_PI - 1e-9 ? 2.0 * M_PI : angle, true);
        if (!revol.IsDone() || !usableSolid(revol.Shape())) {
            error = QT_TRANSLATE_NOOP("Model", "Can't revolve: the axis must not run through the profile");
            return std::nullopt;
        }
        solids.push_back(revol.Shape());
    }
    auto fused = fuseAll(solids);
    if (!fused)
        error = QT_TRANSLATE_NOOP("Model", "Could not merge the profiles");
    return fused;
}

std::optional<TopoDS_Shape> sweepProfiles(const std::vector<TopoDS_Face>& profiles, const TopoDS_Wire& path,
                                          std::string& error)
{
    if (profiles.empty() || path.IsNull()) {
        error = QT_TRANSLATE_NOOP("Model", "Pick the profile and the path");
        return std::nullopt;
    }
    std::vector<TopoDS_Shape> solids;
    for (const TopoDS_Face& face : profiles) {
        try {
            BRepOffsetAPI_MakePipe pipe(path, face);
            pipe.Build();
            if (pipe.IsDone() && usableSolid(pipe.Shape())) {
                solids.push_back(pipe.Shape());
                continue;
            }
        } catch (const Standard_Failure&) {
        }
        error = QT_TRANSLATE_NOOP("Model", "Can't sweep: the path may be too tight for the profile");
        return std::nullopt;
    }
    auto fused = fuseAll(solids);
    if (!fused)
        error = QT_TRANSLATE_NOOP("Model", "Could not merge the profiles");
    return fused;
}

std::optional<TopoDS_Shape> loftProfiles(const std::vector<TopoDS_Face>& profiles, std::string& error)
{
    if (profiles.size() < 2) {
        error = QT_TRANSLATE_NOOP("Model", "A loft needs two profiles or more");
        return std::nullopt;
    }
    try {
        BRepOffsetAPI_ThruSections loft(true, false);
        for (const TopoDS_Face& face : profiles)
            loft.AddWire(BRepTools::OuterWire(face));
        loft.Build();
        if (loft.IsDone() && usableSolid(loft.Shape()))
            return loft.Shape();
    } catch (const Standard_Failure&) {
    }
    error = QT_TRANSLATE_NOOP("Model", "Can't loft through these profiles");
    return std::nullopt;
}

TopoDS_Shape holeCutter(const gp_Pnt& at, const gp_Dir& into, const HoleSpec& spec, double through)
{
    // Starts a little above the surface, so the cut opens cleanly.
    constexpr double lead = 0.05;
    const gp_Pnt start = at.Translated(gp_Vec(into) * -lead);
    const double depth = (spec.depth > 0.0 ? spec.depth : through) + lead;
    const gp_Ax2 axis(start, into);
    const double r = spec.diameter / 2.0;
    TopoDS_Shape cutter = BRepPrimAPI_MakeCylinder(axis, r, depth).Shape();
    const double head = spec.headDiameter / 2.0;
    if (spec.kind == HoleKind::Counterbore && head > r && spec.headDepth > 0.0) {
        const TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(axis, head, spec.headDepth + lead).Shape();
        if (const auto fused = boolean(BooleanKind::Union, cutter, bore))
            cutter = *fused;
    } else if (spec.kind == HoleKind::Countersink && head > r) {
        // 90°: the cone narrows by as much as it goes down.
        const TopoDS_Shape sink = BRepPrimAPI_MakeCone(axis, head + lead, r, head - r + lead).Shape();
        if (const auto fused = boolean(BooleanKind::Union, cutter, sink))
            cutter = *fused;
    }
    return cutter;
}
