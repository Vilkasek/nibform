#include "model/Features.h"
#include "model/Tr.h"

#include "model/Details.h"
#include "model/Export.h"
#include "model/Profiles.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Trsf.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <set>

namespace {

constexpr double kTolerance = 1e-6;

gp_Pnt centroid(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

bool isIn(const TopoDS_Face& face, const gp_Pnt& p)
{
    return BRepClass_FaceClassifier(face, p, kTolerance).State() == TopAbs_IN;
}

} // namespace

// --- Recording ----------------------------------------------------------------------

std::optional<gp_Pnt2d> insidePoint(const Sketch& sketch, const TopoDS_Face& region)
{
    // The middle when it is inside (most shapes), else the middle of a mesh triangle
    // (rings, crescents), which always is.
    const gp_Pnt middle = centroid(region);
    if (isIn(region, middle))
        return sketch.to2d(middle);

    const TopoDS_Face copy = TopoDS::Face(BRepBuilderAPI_Copy(region).Shape());
    const BRepMesh_IncrementalMesh mesher(copy, 0.1); // meshes `copy` in place
    TopLoc_Location location;
    const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(copy, location);
    if (mesh.IsNull())
        return std::nullopt;
    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
        int a = 0, b = 0, c = 0;
        mesh->Triangle(i).Get(a, b, c);
        const gp_XYZ sum = mesh->Node(a).XYZ() + mesh->Node(b).XYZ() + mesh->Node(c).XYZ();
        const gp_Pnt p = gp_Pnt(sum / 3.0).Transformed(location.Transformation());
        if (isIn(region, p))
            return sketch.to2d(p);
    }
    return std::nullopt;
}

FacePick facePick(const TopoDS_Face& face)
{
    return {centroid(face), planarNormal(face).value_or(gp::DZ())};
}

EdgePick edgePick(const TopoDS_Edge& edge)
{
    const BRepAdaptor_Curve curve(edge);
    gp_Pnt middle;
    gp_Vec tangent;
    curve.D1((curve.FirstParameter() + curve.LastParameter()) / 2.0, middle, tangent);
    return {middle, tangent.Magnitude() > 1e-12 ? gp_Dir(tangent) : gp::DX()};
}

std::optional<gp_Pnt> insidePoint(const TopoDS_Shape& solid)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(solid, props);
    BRepClass3d_SolidClassifier classifier(solid);
    classifier.Perform(props.CentreOfMass(), kTolerance);
    if (classifier.State() == TopAbs_IN)
        return props.CentreOfMass();

    // Hollow or bent shapes: step a little inside from the middle of a face.
    const double step = std::cbrt(std::max(props.Mass(), 1e-9)) * 1e-3;
    for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Current());
        const auto normal = planarNormal(face);
        if (!normal)
            continue;
        const gp_Pnt p = centroid(face).Translated(gp_Vec(*normal) * -step);
        classifier.Perform(p, kTolerance);
        if (classifier.State() == TopAbs_IN)
            return p;
    }
    return std::nullopt;
}

// --- Finding again ------------------------------------------------------------------

std::optional<TopoDS_Face> findRegion(const SketchLookup& sketches, const RegionPick& pick)
{
    const Sketch* sketch = sketches(pick.sketch);
    if (!sketch)
        return std::nullopt;
    const std::vector<TopoDS_Face> regions = findRegions(*sketch);
    if (const auto index = regionAt(regions, sketch->to3d(pick.inside)))
        return regions[*index];
    return std::nullopt;
}

std::optional<std::pair<TopoDS_Face, int>> findFace(const std::vector<TopoDS_Shape>& bodies, const FacePick& pick)
{
    // A planar face facing the same way; nearest plane first, then nearest middle.
    std::optional<std::pair<TopoDS_Face, int>> best;
    double bestScore = std::numeric_limits<double>::max();
    for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
        for (TopExp_Explorer it(bodies[b], TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            const auto normal = planarNormal(face);
            if (!normal || normal->Dot(pick.normal) < 0.9999)
                continue;
            const gp_Pnt middle = centroid(face);
            const double planeGap = std::abs(gp_Vec(middle, pick.middle).Dot(gp_Vec(*normal)));
            const double score = planeGap + 0.01 * middle.Distance(pick.middle);
            if (score < bestScore) {
                bestScore = score;
                best = std::pair(face, b);
            }
        }
    }
    return best;
}

std::optional<EdgeRef> findEdge(const std::vector<TopoDS_Shape>& bodies, const EdgePick& pick)
{
    // An edge running the same way (either sense), nearest middle.
    std::optional<EdgeRef> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(bodies[b], TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            const EdgePick here = edgePick(edge);
            if (std::abs(here.direction.Dot(pick.direction)) < 0.999)
                continue;
            const double d = here.middle.Distance(pick.middle);
            if (d < bestDistance) {
                bestDistance = d;
                best = EdgeRef{b, edge};
            }
        }
    }
    return best;
}

std::optional<int> findBody(const std::vector<TopoDS_Shape>& bodies, const gp_Pnt& inside)
{
    std::optional<int> nearest;
    double nearestDistance = std::numeric_limits<double>::max();
    for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
        BRepClass3d_SolidClassifier classifier(bodies[b], inside, kTolerance);
        if (classifier.State() == TopAbs_IN)
            return b;
        // Changed upstream so the point fell out: the nearest body is the best guess.
        BRepExtrema_DistShapeShape distance(bodies[b], BRepBuilderAPI_MakeVertex(inside).Vertex());
        if (distance.IsDone() && distance.Value() < nearestDistance) {
            nearestDistance = distance.Value();
            nearest = b;
        }
    }
    return nearest;
}

FacePick surfacePick(const TopoDS_Face& face, const gp_Pnt& point)
{
    FacePick pick{point, gp::DZ()};
    if (const auto flat = planarNormal(face)) {
        pick.normal = *flat;
        return pick;
    }
    const Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    ShapeAnalysis_Surface analysis(surface);
    const gp_Pnt2d uv = analysis.ValueOfUV(point, 1e-6);
    BRepAdaptor_Surface adaptor(face);
    BRepLProp_SLProps props(adaptor, uv.X(), uv.Y(), 1, 1e-9);
    if (props.IsNormalDefined()) {
        gp_Dir n = props.Normal();
        if (face.Orientation() == TopAbs_REVERSED)
            n.Reverse();
        pick.normal = n;
    }
    pick.middle = surface->Value(uv.X(), uv.Y());
    return pick;
}

std::optional<std::pair<TopoDS_Face, int>> findSurface(const std::vector<TopoDS_Shape>& bodies, const FacePick& pick)
{
    // The face nearest the point (it moved with the model a little at most), facing the
    // same way there.
    std::optional<std::pair<TopoDS_Face, int>> best;
    double bestDistance = std::numeric_limits<double>::max();
    const TopoDS_Shape at = BRepBuilderAPI_MakeVertex(pick.middle).Vertex();
    for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
        for (TopExp_Explorer it(bodies[b], TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            Bnd_Box box;
            BRepBndLib::Add(face, box);
            if (box.Distance(Bnd_Box(pick.middle, pick.middle)) > bestDistance)
                continue;
            BRepExtrema_DistShapeShape distance(face, at);
            if (!distance.IsDone() || distance.Value() >= bestDistance)
                continue;
            const FacePick here = surfacePick(face, distance.PointOnShape1(1));
            if (here.normal.Dot(pick.normal) < 0.9)
                continue;
            bestDistance = distance.Value();
            best = std::pair(face, b);
        }
    }
    return best;
}

std::vector<ThreadMark> ModelContext::allThreads() const
{
    std::vector<ThreadMark> all;
    for (const auto& [id, marks] : threads)
        all.insert(all.end(), marks.begin(), marks.end());
    return all;
}

// --- What a feature made ------------------------------------------------------------

namespace {

constexpr double kOnTool = 1e-4; // how far off a tool's surface a face still lies on it

// A point well inside the face (on its surface), from a grid over its parameters.
std::optional<gp_Pnt> pointOnFace(const TopoDS_Face& face)
{
    double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    const BRepAdaptor_Surface surface(face);
    constexpr int kSteps = 7;
    // The middle first, then outwards.
    for (int ring = 0; ring <= kSteps / 2; ++ring) {
        for (int i = -ring; i <= ring; ++i) {
            for (int j = -ring; j <= ring; ++j) {
                if (std::max(std::abs(i), std::abs(j)) != ring)
                    continue;
                const double u = u0 + (u1 - u0) * (0.5 + i / double(kSteps + 1));
                const double v = v0 + (v1 - v0) * (0.5 + j / double(kSteps + 1));
                if (BRepClass_FaceClassifier(face, gp_Pnt2d(u, v), kTolerance).State() == TopAbs_IN)
                    return surface.Value(u, v);
            }
        }
    }
    return std::nullopt;
}

// The tool's faces, as one shape to measure distances to (a solid would count its inside).
TopoDS_Shape surfaceOf(const TopoDS_Shape& tool)
{
    TopoDS_Compound faces;
    BRep_Builder().MakeCompound(faces);
    for (TopExp_Explorer it(tool, TopAbs_FACE); it.More(); it.Next())
        BRep_Builder().Add(faces, it.Current());
    return faces;
}

bool liesOn(const gp_Pnt& p, const TopoDS_Shape& surface, const Bnd_Box& box)
{
    if (box.IsOut(p))
        return false;
    BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(), surface);
    return distance.IsDone() && distance.Value() < kOnTool;
}

Bnd_Box boxOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    box.Enlarge(10.0 * kOnTool);
    return box;
}

} // namespace

std::optional<int> featureOfFace(const std::vector<Feature>& features, const ModelContext& context,
                                 const TopoDS_Face& face)
{
    const auto p = pointOnFace(face);
    if (!p)
        return std::nullopt;
    for (auto f = features.rbegin(); f != features.rend(); ++f) {
        const auto tool = context.tools.find(f->id);
        if (tool == context.tools.end() || tool->second.operation == BodyOperation::NewBody)
            continue;
        if (liesOn(*p, surfaceOf(tool->second.shape), boxOf(tool->second.shape)))
            return f->id;
    }
    return std::nullopt;
}

std::vector<TopoDS_Face> facesOnTool(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Shape& tool)
{
    std::vector<TopoDS_Face> faces;
    const TopoDS_Shape surface = surfaceOf(tool);
    const Bnd_Box box = boxOf(tool);
    for (const TopoDS_Shape& body : bodies) {
        for (TopExp_Explorer it(body, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            Bnd_Box faceBox;
            BRepBndLib::Add(face, faceBox);
            if (box.IsOut(faceBox))
                continue;
            if (const auto p = pointOnFace(face); p && liesOn(*p, surface, box))
                faces.push_back(face);
        }
    }
    return faces;
}

// --- Construction geometry ---------------------------------------------------------

std::optional<Datum> baseDatum(int id)
{
    // The planes face as the sketches made on them do (SketchEditor::basePlane).
    switch (id) {
    case kBaseXY: return Datum{id, true, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())};
    case kBaseXZ: return Datum{id, true, gp_Ax3(gp::Origin(), -gp::DY(), gp::DX())};
    case kBaseYZ: return Datum{id, true, gp_Ax3(gp::Origin(), gp::DX(), gp::DY())};
    case kBaseX:  return Datum{id, false, gp_Ax3(gp::Origin(), gp::DX(), gp::DY())};
    case kBaseY:  return Datum{id, false, gp_Ax3(gp::Origin(), gp::DY(), gp::DZ())};
    case kBaseZ:  return Datum{id, false, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())};
    default: return std::nullopt;
    }
}

const Datum* ModelContext::datum(int id) const
{
    for (const Datum& d : datums) {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

namespace {

std::optional<Datum> lookupDatum(const ModelContext& context, int id)
{
    if (id < 0)
        return baseDatum(id);
    if (const Datum* d = context.datum(id))
        return *d;
    return std::nullopt;
}

} // namespace

std::optional<gp_Ax1> resolveAxis(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies,
                                  const AxisRef& ref, const ModelContext& context)
{
    switch (ref.kind) {
    case AxisRef::Kind::None:
        return std::nullopt;
    case AxisRef::Kind::Edge: {
        const auto edge = findEdge(bodies, ref.edge);
        if (!edge)
            return std::nullopt;
        const BRepAdaptor_Curve curve(edge->edge);
        if (curve.GetType() == GeomAbs_Line) {
            const gp_Pnt a = curve.Value(curve.FirstParameter()), b = curve.Value(curve.LastParameter());
            if (a.Distance(b) < 1e-9)
                return std::nullopt;
            return gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
        }
        if (curve.GetType() == GeomAbs_Circle)
            return curve.Circle().Axis();
        return std::nullopt;
    }
    case AxisRef::Kind::SketchLine: {
        const Sketch* sketch = sketches(ref.sketch);
        const SketchEntity* line = sketch ? sketch->entity(ref.entity) : nullptr;
        if (!line || line->kind != EntityKind::Line)
            return std::nullopt;
        const gp_Pnt a = sketch->to3d(sketch->pos(line->a)), b = sketch->to3d(sketch->pos(line->b));
        if (a.Distance(b) < 1e-9)
            return std::nullopt;
        return gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
    }
    case AxisRef::Kind::Datum: {
        const auto d = lookupDatum(context, ref.datum);
        if (!d || d->plane)
            return std::nullopt;
        return d->axis();
    }
    }
    return std::nullopt;
}

std::optional<gp_Ax3> resolvePlane(const std::vector<TopoDS_Shape>& bodies, const PlaneRef& ref,
                                   const ModelContext& context)
{
    switch (ref.kind) {
    case PlaneRef::Kind::None:
        return std::nullopt;
    case PlaneRef::Kind::Face: {
        const auto face = findFace(bodies, ref.face);
        if (!face)
            return std::nullopt;
        const gp_Dir normal = *planarNormal(face->first);
        const gp_Dir x = BRepAdaptor_Surface(face->first).Plane().XAxis().Direction();
        return gp_Ax3(centroid(face->first), normal, x);
    }
    case PlaneRef::Kind::Datum: {
        const auto d = lookupDatum(context, ref.datum);
        if (!d || !d->plane)
            return std::nullopt;
        return d->frame;
    }
    }
    return std::nullopt;
}

bool samePlane(const gp_Ax3& a, const gp_Ax3& b)
{
    return a.Location().Distance(b.Location()) < 1e-9 && a.Direction().IsEqual(b.Direction(), 1e-12)
        && a.XDirection().IsEqual(b.XDirection(), 1e-12);
}

std::optional<PlaneRef> planeUnder(const gp_Ax3& plane, const std::vector<TopoDS_Shape>& bodies,
                                   const ModelContext& context)
{
    // The base planes go through the origin; a sketch on a face or a datum starts in the
    // middle of it.
    if (plane.Location().Distance(gp::Origin()) < 1e-9)
        return std::nullopt;
    const gp_Dir normal = plane.Direction();
    auto inPlane = [&](const gp_Pnt& p, const gp_Dir& n) {
        return n.Dot(normal) > 0.9999 && std::abs(gp_Vec(p, plane.Location()).Dot(gp_Vec(n))) < 1e-6;
    };
    for (const Datum& d : context.datums) {
        if (d.plane && inPlane(d.frame.Location(), d.frame.Direction()))
            return PlaneRef{PlaneRef::Kind::Datum, {}, d.id};
    }
    // The face in that plane with its middle nearest the sketch's origin.
    std::optional<TopoDS_Face> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (const TopoDS_Shape& body : bodies) {
        for (TopExp_Explorer it(body, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            const auto n = planarNormal(face);
            if (!n)
                continue;
            const gp_Pnt middle = centroid(face);
            if (!inPlane(middle, *n))
                continue;
            const double d = middle.Distance(plane.Location());
            if (d < bestDistance) {
                bestDistance = d;
                best = face;
            }
        }
    }
    if (!best)
        return std::nullopt;
    return PlaneRef{PlaneRef::Kind::Face, facePick(*best), 0};
}

std::vector<SheetFound> findSheets(const std::vector<TopoDS_Shape>& bodies, const ModelContext& context)
{
    std::vector<SheetFound> out;
    for (const SheetBody& sheet : context.sheets) {
        const auto face = findFace(bodies, sheet.fixed);
        // Still on its plane (a face elsewhere facing that way isn't it).
        if (!face || std::abs(gp_Vec(centroid(face->first), sheet.fixed.middle).Dot(gp_Vec(sheet.fixed.normal))) > 1e-4)
            continue;
        if (std::any_of(out.begin(), out.end(), [&](const SheetFound& f) { return f.body == face->second; }))
            continue;
        out.push_back({sheet, face->second, face->first});
    }
    return out;
}

std::optional<SheetFound> sheetOf(const std::vector<TopoDS_Shape>& bodies, const ModelContext& context, int body)
{
    for (const SheetFound& f : findSheets(bodies, context)) {
        if (f.body == body)
            return f;
    }
    return std::nullopt;
}

namespace {

// Where a sketch on `now` goes when what it was drawn on is at `target`: facing the same
// way, it only slides along its normal (its curves keep their place on the face); turned,
// it takes the new plane's axes.
gp_Ax3 followPlane(const gp_Ax3& now, const gp_Ax3& target)
{
    const gp_Dir n = target.Direction();
    if (!now.Direction().IsEqual(n, 1e-6))
        return target;
    gp_Ax3 moved = now;
    moved.Translate(gp_Vec(n) * gp_Vec(now.Location(), target.Location()).Dot(gp_Vec(n)));
    return moved;
}

} // namespace

std::optional<TopoDS_Wire> sketchPath(const Sketch& sketch, int entity)
{
    const SketchEntity* seed = sketch.entity(entity);
    if (!seed)
        return std::nullopt;
    // Every curve connected to the seed by its ends (construction curves only when the
    // seed is one: a path drawn as construction stays apart from the outlines).
    std::set<EntityId> chain{seed->id};
    std::deque<EntityId> todo{seed->id};
    while (!todo.empty()) {
        const SketchEntity* e = sketch.entity(todo.front());
        todo.pop_front();
        for (PointId end : {e->a, e->b}) {
            if (end == kNoId)
                continue;
            for (EntityId next : sketch.entitiesAt(end)) {
                const SketchEntity* n = sketch.entity(next);
                if (n && n->construction == seed->construction && (n->a == end || n->b == end) && chain.insert(next).second)
                    todo.push_back(next);
            }
        }
    }
    TopTools_ListOfShape edges;
    for (EntityId id : chain) {
        const TopoDS_Edge edge = toEdge(sketch, *sketch.entity(id));
        if (edge.IsNull()) // a curve with no shape (from a damaged file) breaks the path
            return std::nullopt;
        edges.Append(edge);
    }
    BRepBuilderAPI_MakeWire wire;
    wire.Add(edges);
    if (!wire.IsDone())
        return std::nullopt;
    return wire.Wire();
}

// --- Replaying ----------------------------------------------------------------------

namespace {

FeatureResult wrap(ModelResult result)
{
    FeatureResult out;
    static_cast<ModelResult&>(out) = std::move(result);
    return out;
}

FeatureResult failed(const std::vector<TopoDS_Shape>& bodies, const char* error)
{
    FeatureResult out;
    out.bodies = bodies;
    out.error = error;
    return out;
}

TopoDS_Shape transformed(const TopoDS_Shape& shape, const gp_Trsf& trsf)
{
    return BRepBuilderAPI_Transform(shape, trsf, true).Shape();
}

// Combines each copy with the bodies in turn; copies a cut can't reach are skipped.
ModelResult applyCopies(std::vector<TopoDS_Shape> bodies, const std::vector<TopoDS_Shape>& copies,
                        BodyOperation operation)
{
    ModelResult result;
    for (const TopoDS_Shape& copy : copies) {
        ModelResult step = applyTool(bodies, copy, operation);
        if (!step.ok()) {
            if (operation == BodyOperation::Cut)
                continue; // that copy falls off the part
            step.bodies = bodies;
            return step;
        }
        bodies = std::move(step.bodies);
    }
    result.bodies = std::move(bodies);
    return result;
}

double reachOf(const std::vector<TopoDS_Shape>& bodies)
{
    Bnd_Box box;
    for (const TopoDS_Shape& b : bodies)
        BRepBndLib::Add(b, box);
    return box.IsVoid() ? 100.0 : 2.0 * std::sqrt(box.SquareExtent()) + 1.0;
}

} // namespace

namespace {

// Imported geometry, read once: the text is shared by every copy of the feature.
std::optional<TopoDS_Shape> importedShape(const std::shared_ptr<const std::string>& text)
{
    if (!text)
        return std::nullopt;
    struct Entry
    {
        std::weak_ptr<const std::string> text;
        TopoDS_Shape shape;
    };
    static std::map<const std::string*, Entry> cache;
    std::erase_if(cache, [](const auto& item) { return item.second.text.expired(); });
    const auto found = cache.find(text.get());
    if (found != cache.end() && found->second.text.lock() == text)
        return found->second.shape;
    const auto shape = shapeFromText(*text);
    if (shape)
        cache[text.get()] = {text, *shape};
    return shape;
}

// Threads drilled holes (cut into `drilled`): each hole's wall, found again around its
// axis, gets the thread from its mouth; a modelled one is cut, and the hole's tool
// takes the grooves too (patterns repeat them).
FeatureResult tapHoles(FeatureResult drilled, const std::vector<gp_Pnt>& mouths, const gp_Dir& into,
                       const HoleSpec& spec)
{
    const double r = spec.diameter / 2.0;
    ThreadSpec thread = spec.thread;
    if (thread.length <= 0.0 && spec.depth > 0.0)
        thread.length = spec.depth;
    std::vector<ThreadMark> marks;
    for (const gp_Pnt& mouth : mouths) {
        // The wall: a cylinder of the drill's radius around this hole's axis.
        std::optional<std::pair<TopoDS_Face, int>> wall;
        double longest = 0.0;
        for (int b = 0; b < static_cast<int>(drilled.bodies.size()); ++b) {
            for (TopExp_Explorer it(drilled.bodies[b], TopAbs_FACE); it.More(); it.Next()) {
                const TopoDS_Face face = TopoDS::Face(it.Current());
                const BRepAdaptor_Surface surface(face);
                if (surface.GetType() != GeomAbs_Cylinder || std::abs(surface.Cylinder().Radius() - r) > 1e-6)
                    continue;
                const gp_Ax1 axis = surface.Cylinder().Axis();
                if (std::abs(axis.Direction().Dot(into)) < 1.0 - 1e-9
                    || gp_Vec(axis.Location(), mouth).Crossed(gp_Vec(axis.Direction())).Magnitude() > 1e-6)
                    continue;
                double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
                BRepTools::UVBounds(face, u0, u1, v0, v1);
                if (v1 - v0 > longest) {
                    longest = v1 - v0;
                    wall = std::pair(face, b);
                }
            }
        }
        if (!wall)
            continue; // that hole missed the body
        std::string error;
        if (const auto mark = placeThread(drilled.bodies[wall->second], wall->first, thread, mouth, error))
            marks.push_back(*mark);
    }
    if (thread.modelled && !marks.empty()) {
        TopoDS_Compound grooves;
        BRep_Builder().MakeCompound(grooves);
        for (const ThreadMark& m : marks)
            BRep_Builder().Add(grooves, threadGroove(m));
        ModelResult cut = applyTool(drilled.bodies, grooves, BodyOperation::Cut);
        if (!cut.ok()) {
            drilled.error = QT_TRANSLATE_NOOP("Model", "Could not cut the thread");
            return drilled;
        }
        drilled.bodies = std::move(cut.bodies);
        if (drilled.tool) {
            TopoDS_Compound both;
            BRep_Builder().MakeCompound(both);
            BRep_Builder().Add(both, drilled.tool->shape);
            BRep_Builder().Add(both, grooves);
            drilled.tool->shape = both;
        }
    }
    drilled.threads = std::move(marks);
    return drilled;
}

FeatureResult applyUnguarded(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies, const Feature& f,
                             const ModelContext& context)
{
    FeatureResult missing;
    missing.bodies = bodies;
    auto profiles = [&](std::vector<TopoDS_Face>& out) {
        for (const RegionPick& r : f.regions) {
            const auto face = findRegion(sketches, r);
            if (!face)
                return false;
            out.push_back(*face);
        }
        return true;
    };
    switch (f.kind) {
    case Feature::Kind::Sketch: {
        // Builds nothing; one drawn on a face or a datum moves along with it.
        const Sketch* sketch = sketches(f.sketch);
        if (f.plane.kind == PlaneRef::Kind::None || !sketch)
            return missing;
        const auto target = resolvePlane(bodies, f.plane, context);
        if (!target) {
            missing.error = QT_TRANSLATE_NOOP("Model", "The face the sketch was drawn on is gone");
            return missing;
        }
        missing.sketchPlane = followPlane(sketch->plane(), *target);
        return missing;
    }
    case Feature::Kind::Extrude: {
        std::vector<Profile> profiles;
        for (const RegionPick& r : f.regions) {
            const auto face = findRegion(sketches, r);
            if (!face) {
                missing.error = QT_TRANSLATE_NOOP("Model", "The sketch region that was extruded is gone");
                return missing;
            }
            profiles.push_back({*face, sketches(r.sketch)->plane().Direction(), -1});
        }
        for (const FacePick& p : f.faces) {
            const auto face = findFace(bodies, p);
            if (!face) {
                missing.error = QT_TRANSLATE_NOOP("Model", "The face that was extruded is gone");
                return missing;
            }
            profiles.push_back({face->first, *planarNormal(face->first), face->second});
        }
        return wrap(extrude(bodies, offsetProfiles(std::move(profiles), f.startOffset), f.value, f.operation));
    }
    case Feature::Kind::Fillet:
    case Feature::Kind::Chamfer: {
        std::vector<EdgeRef> edges;
        for (const EdgePick& p : f.edges) {
            const auto edge = findEdge(bodies, p);
            if (!edge) {
                missing.error = QT_TRANSLATE_NOOP("Model", "The edge that was rounded is gone");
                return missing;
            }
            edges.push_back(*edge);
        }
        if (f.kind == Feature::Kind::Fillet && f.endValue > 0.0 && std::abs(f.endValue - f.value) > 1e-9) {
            // `value` where each edge starts, `endValue` at the end it was picked toward.
            std::vector<VaryingEdge> varying;
            for (std::size_t i = 0; i < edges.size(); ++i) {
                const EdgePick now = edgePick(edges[i].edge);
                varying.push_back({edges[i], now.direction.Dot(f.edges[i].direction) < 0.0});
            }
            return wrap(setbackCorners(bodies, filletVarying(bodies, varying, f.value, f.endValue), edges, f.setback));
        }
        if (f.kind == Feature::Kind::Fillet)
            return wrap(setbackCorners(bodies, filletEdges(bodies, edges, f.value), edges, f.setback));
        return wrap(chamferEdges(bodies, edges, f.value));
    }
    case Feature::Kind::Shell: {
        std::vector<std::pair<int, TopoDS_Face>> open;
        for (const FacePick& p : f.faces) {
            const auto face = findFace(bodies, p);
            if (!face) {
                missing.error = QT_TRANSLATE_NOOP("Model", "The face that was opened is gone");
                return missing;
            }
            open.push_back({face->second, face->first});
        }
        return wrap(shellBodies(bodies, open, f.value));
    }
    case Feature::Kind::Combine:
    case Feature::Kind::Delete: {
        std::vector<int> picked;
        for (const gp_Pnt& p : f.bodies) {
            const auto body = findBody(bodies, p);
            if (!body) {
                missing.error = QT_TRANSLATE_NOOP("Model", "The body this applied to is gone");
                return missing;
            }
            picked.push_back(*body);
        }
        if (f.kind == Feature::Kind::Combine)
            return wrap(combine(bodies, picked, f.boolean));
        FeatureResult kept;
        for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
            if (std::find(picked.begin(), picked.end(), b) == picked.end())
                kept.bodies.push_back(bodies[b]);
        }
        return kept;
    }
    case Feature::Kind::Revolve:
    case Feature::Kind::Sweep:
    case Feature::Kind::Loft: {
        std::vector<TopoDS_Face> faces;
        if (!profiles(faces))
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sketch region it was made from is gone"));
        std::string error;
        std::optional<TopoDS_Shape> tool;
        if (f.kind == Feature::Kind::Revolve) {
            const auto axis = resolveAxis(sketches, bodies, f.axis, context);
            if (!axis)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The axis is gone"));
            tool = revolveProfiles(faces, *axis, f.value, error);
        } else if (f.kind == Feature::Kind::Sweep) {
            const Sketch* sketch = sketches(f.pathSketch);
            const auto path = sketch ? sketchPath(*sketch, f.pathEntity) : std::nullopt;
            if (!path)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The path is gone"));
            tool = sweepProfiles(faces, *path, error);
        } else {
            tool = loftProfiles(faces, error);
        }
        if (!tool) {
            missing.error = error;
            return missing;
        }
        return wrap(applyTool(bodies, *tool, f.operation));
    }
    case Feature::Kind::Mirror: {
        const auto plane = resolvePlane(bodies, f.plane, context);
        if (!plane)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The mirror plane is gone"));
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(plane->Location(), plane->Direction()));
        std::vector<TopoDS_Shape> copies;
        for (const gp_Pnt& p : f.bodies) {
            const auto body = findBody(bodies, p);
            if (!body)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The body this applied to is gone"));
            copies.push_back(transformed(bodies[*body], mirror));
        }
        if (copies.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick the bodies to mirror"));
        return wrap(applyCopies(bodies, copies, f.operation));
    }
    case Feature::Kind::Pattern: {
        const auto axis = resolveAxis(sketches, bodies, f.axis, context);
        if (!axis)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The pattern's direction is gone"));
        if (f.count < 2)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "A pattern needs two instances or more"));
        std::vector<gp_Trsf> steps;
        const bool full = std::abs(f.value) >= 359.999;
        for (int k = 1; k < f.count; ++k) {
            gp_Trsf t;
            if (f.pattern == Feature::PatternKind::Linear) {
                t.SetTranslation(gp_Vec(axis->Direction()) * (f.value * k));
            } else {
                const double step = full ? 360.0 / f.count : f.value / (f.count - 1);
                t.SetRotation(*axis, step * k * M_PI / 180.0);
            }
            steps.push_back(t);
        }
        std::vector<TopoDS_Shape> copies;
        std::vector<ThreadMark> threads;
        BodyOperation operation = f.operation;
        if (f.source != 0) {
            const auto it = context.tools.find(f.source);
            if (it == context.tools.end())
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The patterned feature didn't apply"));
            for (const gp_Trsf& t : steps)
                copies.push_back(transformed(it->second.shape, t));
            operation = it->second.operation;
            if (const auto marks = context.threads.find(f.source); marks != context.threads.end()) {
                for (const gp_Trsf& t : steps) {
                    for (ThreadMark m : marks->second) {
                        m.axis.Transform(t);
                        threads.push_back(m);
                    }
                }
            }
        } else {
            for (const gp_Pnt& p : f.bodies) {
                const auto body = findBody(bodies, p);
                if (!body)
                    return failed(bodies, QT_TRANSLATE_NOOP("Model", "The body this applied to is gone"));
                for (const gp_Trsf& t : steps)
                    copies.push_back(transformed(bodies[*body], t));
            }
            if (copies.empty())
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick the bodies to pattern"));
        }
        FeatureResult result = wrap(applyCopies(bodies, copies, operation));
        result.threads = std::move(threads);
        return result;
    }
    case Feature::Kind::Hole: {
        if (f.faces.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick a flat face for the hole"));
        const auto face = findFace(bodies, f.faces.front());
        if (!face)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face the hole was on is gone"));
        const gp_Dir normal = *planarNormal(face->first);
        const gp_Pnt origin = centroid(face->first);
        std::vector<gp_Pnt> at = f.points;
        for (const SketchPointRef& ref : f.holeAt) {
            const Sketch* sketch = sketches(ref.sketch);
            if (!sketch || !sketch->point(ref.point))
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sketch point the hole was on is gone"));
            at.push_back(sketch->to3d(sketch->pos(ref.point)));
        }
        HoleSpec spec = f.hole;
        if (spec.tapped) {
            spec.kind = HoleKind::Simple;
            spec.thread = nearestThread(spec.thread.standard, spec.diameter, spec.thread);
            spec.diameter = tapDrill(spec.thread);
        }
        if (at.empty() || spec.diameter <= 0.0)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick where the hole goes"));
        const double through = reachOf(bodies);
        std::vector<TopoDS_Shape> cutters;
        std::vector<gp_Pnt> mouths;
        for (const gp_Pnt& p : at) {
            // Onto the face's plane, then into the material.
            const gp_Pnt onFace = p.Translated(gp_Vec(normal) * -gp_Vec(origin, p).Dot(gp_Vec(normal)));
            cutters.push_back(holeCutter(onFace, normal.Reversed(), spec, through));
            mouths.push_back(onFace);
        }
        TopoDS_Shape tool = cutters.front();
        if (cutters.size() > 1) {
            TopoDS_Compound all;
            BRep_Builder().MakeCompound(all);
            for (const TopoDS_Shape& c : cutters)
                BRep_Builder().Add(all, c);
            tool = all;
        }
        FeatureResult result = wrap(applyTool(bodies, tool, BodyOperation::Cut));
        if (!result.ok()) {
            result.error = QT_TRANSLATE_NOOP("Model", "The hole doesn't reach any body");
            return result;
        }
        if (spec.tapped)
            return tapHoles(std::move(result), mouths, normal.Reversed(), spec);
        return result;
    }
    case Feature::Kind::Move: {
        const auto trsf = trsfOf(f.matrix);
        if (!trsf)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The move isn't a rigid motion"));
        FeatureResult result;
        result.bodies = bodies;
        std::vector<int> picked;
        for (const gp_Pnt& p : f.bodies) {
            const auto body = findBody(bodies, p);
            if (!body)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The body this applied to is gone"));
            if (std::find(picked.begin(), picked.end(), *body) == picked.end())
                picked.push_back(*body);
        }
        if (picked.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick the bodies to move"));
        for (int b : picked) {
            const TopoDS_Shape moved = transformed(bodies[b], *trsf);
            if (f.copy)
                result.bodies.push_back(moved);
            else
                result.bodies[b] = moved; // stays where it was in the list
        }
        return result;
    }
    case Feature::Kind::Import: {
        const auto shape = importedShape(f.shape);
        if (!shape)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The imported geometry can't be read"));
        FeatureResult result;
        result.bodies = bodies;
        if (f.reference) {
            result.reference = *shape;
            return result;
        }
        std::size_t before = result.bodies.size();
        for (TopExp_Explorer it(*shape, TopAbs_SOLID); it.More(); it.Next())
            result.bodies.push_back(it.Current());
        if (result.bodies.size() == before)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The file has no closed solids; import it as a reference"));
        return result;
    }
    case Feature::Kind::Thread: {
        if (f.faces.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick a round face for the thread"));
        const auto face = findSurface(bodies, f.faces.front());
        if (!face)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face the thread was on is gone"));
        std::string error;
        const auto mark = placeThread(bodies[face->second], face->first, f.thread, f.faces.front().middle, error);
        if (!mark) {
            missing.error = error;
            return missing;
        }
        FeatureResult result;
        if (f.thread.modelled) {
            result = wrap(applyTool(bodies, threadGroove(*mark), BodyOperation::Cut));
            if (!result.ok()) {
                result.error = QT_TRANSLATE_NOOP("Model", "Could not cut the thread");
                return result;
            }
        } else {
            result.bodies = bodies;
        }
        result.threads.push_back(*mark);
        return result;
    }
    case Feature::Kind::Draft: {
        const auto plane = resolvePlane(bodies, f.plane, context);
        if (!plane)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The neutral plane is gone"));
        std::vector<std::pair<int, TopoDS_Face>> faces;
        for (const FacePick& p : f.faces) {
            const auto face = findFace(bodies, p);
            if (!face)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face that was drafted is gone"));
            faces.push_back({face->second, face->first});
        }
        return wrap(draftFaces(bodies, faces, gp_Pln(*plane), f.value));
    }
    case Feature::Kind::Rib: {
        const Sketch* sketch = sketches(f.pathSketch);
        const auto path = sketch ? sketchPath(*sketch, f.pathEntity) : std::nullopt;
        if (!path)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The rib's line is gone"));
        return wrap(ribFrom(bodies, *path, sketch->plane().Direction(), f.value, f.flip));
    }
    case Feature::Kind::Emboss: {
        std::vector<TopoDS_Face> faces;
        if (!profiles(faces) || f.regions.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sketch region it was made from is gone"));
        if (f.faces.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick the face to emboss on"));
        const auto face = findSurface(bodies, f.faces.front());
        if (!face)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face it was on is gone"));
        const gp_Dir along = sketches(f.regions.front().sketch)->plane().Direction();
        return wrap(embossProfiles(bodies, face->second, face->first, f.faces.front().middle, faces, along, f.value,
                                   f.operation == BodyOperation::Cut));
    }
    case Feature::Kind::SheetBase: {
        if (f.sheet.thickness <= 0.0)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sheet needs a thickness"));
        SheetBody sheet{f.id, f.sheet, {}};
        FeatureResult result;
        if (!f.regions.empty()) {
            std::vector<TopoDS_Face> faces;
            if (!profiles(faces))
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sketch region it was made from is gone"));
            const gp_Dir n = sketches(f.regions.front().sketch)->plane().Direction();
            const gp_Dir along = f.flip ? n.Reversed() : n;
            std::vector<Profile> plates;
            for (const TopoDS_Face& face : faces)
                plates.push_back({face, along, -1});
            result = wrap(extrude(bodies, plates, f.sheet.thickness, BodyOperation::NewBody));
            // The side facing the way it was made (up from a sketch on the ground): laid
            // flat, it looks as the part does from there.
            sheet.fixed = {centroid(faces.front()).Translated(gp_Vec(along) * f.sheet.thickness), along};
        } else {
            const Sketch* sketch = sketches(f.pathSketch);
            const auto path = sketch ? sketchPath(*sketch, f.pathEntity) : std::nullopt;
            if (!path)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sheet's line is gone"));
            SheetFace fixed;
            std::string error;
            const auto solid = contourSheet(*path, sketch->plane().Direction(), f.value, f.sheet, f.flip, fixed, error);
            if (!solid) {
                missing.error = error;
                return missing;
            }
            result = wrap(applyTool(bodies, *solid, BodyOperation::NewBody));
            sheet.fixed = {fixed.middle, fixed.normal};
        }
        if (result.ok())
            result.sheet = sheet;
        return result;
    }
    case Feature::Kind::Flange:
    case Feature::Kind::Hem: {
        if (f.edges.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick the edge to bend a flange from"));
        const auto edge = findEdge(bodies, f.edges.front());
        if (!edge)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The edge it was bent from is gone"));
        const auto sheet = sheetOf(bodies, context, edge->body);
        if (!sheet)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "That isn't sheet metal: start with a base flange, or convert a shelled body"));
        return wrap(edgeFlange(bodies, edge->body, edge->edge, sheet->sheet.spec, f.value, f.flange, f.flip,
                               f.kind == Feature::Kind::Hem));
    }
    case Feature::Kind::Tab: {
        std::vector<TopoDS_Face> faces;
        if (!profiles(faces) || f.regions.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The sketch region it was made from is gone"));
        const gp_Ax3 plane = sketches(f.regions.front().sketch)->plane();
        // The sheet with a side in the sketch's plane: the tab goes into it, as thick as it.
        for (const SheetFound& found : findSheets(bodies, context)) {
            for (TopExp_Explorer it(bodies[found.body], TopAbs_FACE); it.More(); it.Next()) {
                const TopoDS_Face face = TopoDS::Face(it.Current());
                const auto n = planarNormal(face);
                if (!n || std::abs(n->Dot(plane.Direction())) < 0.9999
                    || std::abs(gp_Vec(plane.Location(), centroid(face)).Dot(gp_Vec(*n))) > 1e-6
                    || isThicknessFace(face, found.sheet.spec.thickness))
                    continue;
                std::vector<Profile> tabs;
                for (const TopoDS_Face& region : faces)
                    tabs.push_back({region, n->Reversed(), -1});
                return wrap(extrude(bodies, tabs, found.sheet.spec.thickness, BodyOperation::Join));
            }
        }
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "Draw the tab on a side of a sheet metal body"));
    }
    case Feature::Kind::Relief: {
        if (f.points.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Tap the corner for the relief"));
        const auto body = findBody(bodies, f.points.front());
        const auto sheet = body ? sheetOf(bodies, context, *body) : std::nullopt;
        if (!sheet)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "That isn't sheet metal: start with a base flange, or convert a shelled body"));
        return wrap(cornerRelief(bodies, *body, f.points.front(), sheet->sheet.spec, f.value, f.flange.square));
    }
    case Feature::Kind::SheetConvert: {
        if (f.faces.empty())
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick a side of a wall of the shelled body"));
        const auto face = findFace(bodies, f.faces.front());
        if (!face)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "The face it was converted from is gone"));
        FeatureResult result = wrap(convertToSheet(bodies, face->second, face->first, f.sheet));
        if (result.ok())
            result.sheet = SheetBody{f.id, f.sheet, f.faces.front()};
        return result;
    }
    case Feature::Kind::Datum: {
        FeatureResult result;
        result.bodies = bodies;
        Datum d;
        d.id = f.id;
        switch (f.datum) {
        case Feature::DatumKind::OffsetPlane:
        case Feature::DatumKind::AnglePlane: {
            const auto plane = resolvePlane(bodies, f.plane, context);
            if (!plane)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The plane it was made from is gone"));
            d.frame = *plane;
            if (f.datum == Feature::DatumKind::OffsetPlane) {
                d.frame.Translate(gp_Vec(plane->Direction()) * f.value);
            } else {
                const auto axis = resolveAxis(sketches, bodies, f.axis, context);
                if (!axis)
                    return failed(bodies, QT_TRANSLATE_NOOP("Model", "The axis is gone"));
                d.frame.Rotate(*axis, f.value * M_PI / 180.0);
            }
            break;
        }
        case Feature::DatumKind::ThreePointPlane: {
            if (f.points.size() < 3)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick three points"));
            const gp_Vec u(f.points[0], f.points[1]), v(f.points[0], f.points[2]);
            const gp_Vec n = u.Crossed(v);
            if (n.Magnitude() < 1e-9 || u.Magnitude() < 1e-9)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The three points are in a line"));
            d.frame = gp_Ax3(f.points[0], gp_Dir(n), gp_Dir(u));
            break;
        }
        case Feature::DatumKind::Axis: {
            const auto axis = resolveAxis(sketches, bodies, f.axis, context);
            if (!axis)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The edge it was made from is gone"));
            d.plane = false;
            d.frame = gp_Ax3(axis->Location(), axis->Direction());
            break;
        }
        case Feature::DatumKind::TwoPointAxis:
            if (f.points.size() < 2 || f.points[0].Distance(f.points[1]) < 1e-9)
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick two different points"));
            d.plane = false;
            d.frame = gp_Ax3(f.points[0], gp_Dir(gp_Vec(f.points[0], f.points[1])));
            break;
        }
        result.datum = d;
        return result;
    }
    }
    return missing;
}

} // namespace

FeatureResult applyFeature(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies, const Feature& f,
                           const ModelContext& context)
{
    // Values the editor never makes (a damaged file can extrude 1e308 mm) make
    // OpenCASCADE throw deep inside its algorithms. The feature fails then, as any
    // feature that doesn't apply, instead of taking the app down.
    // Sketches that followed their face or datum, moved there (made when first asked for).
    std::map<int, Sketch> placed;
    const SketchLookup lookup = [&](int id) -> const Sketch* {
        const Sketch* sketch = sketches(id);
        const auto at = context.sketchPlanes.find(id);
        if (!sketch || at == context.sketchPlanes.end() || samePlane(sketch->plane(), at->second))
            return sketch;
        auto it = placed.find(id);
        if (it == placed.end())
            it = placed.emplace(id, Sketch(at->second, sketch->contents())).first;
        return &it->second;
    };
    try {
        return applyUnguarded(lookup, bodies, f, context);
    } catch (const Standard_Failure&) {
        return failed(bodies, QT_TRANSLATE_NOOP("Model", "This feature can't be built from its values"));
    }
}

void remember(ModelContext& context, const Feature& feature, const FeatureResult& result)
{
    if (result.datum) {
        std::erase_if(context.datums, [&](const Datum& d) { return d.id == result.datum->id; });
        context.datums.push_back(*result.datum);
    }
    if (result.tool)
        context.tools[feature.id] = *result.tool;
    if (result.reference)
        context.references.push_back(*result.reference);
    if (!result.threads.empty())
        context.threads[feature.id] = result.threads;
    else
        context.threads.erase(feature.id);
    if (result.sheet) {
        std::erase_if(context.sheets, [&](const SheetBody& b) { return b.feature == feature.id; });
        context.sheets.push_back(*result.sheet);
    }
    if (feature.kind == Feature::Kind::Sketch) {
        if (result.sketchPlane)
            context.sketchPlanes[feature.sketch] = *result.sketchPlane;
        else
            context.sketchPlanes.erase(feature.sketch);
    }
}

std::vector<double> matrixOf(const gp_Trsf& trsf)
{
    std::vector<double> m;
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 4; ++col)
            m.push_back(trsf.Value(row, col));
    }
    return m;
}

std::optional<gp_Trsf> trsfOf(const std::vector<double>& m)
{
    if (m.size() != 12)
        return std::nullopt;
    // Orthonormal, right-handed: a rotation and a shift, nothing that scales or mirrors.
    const gp_Vec r0(m[0], m[1], m[2]), r1(m[4], m[5], m[6]), r2(m[8], m[9], m[10]);
    if (std::abs(r0.Magnitude() - 1.0) > 1e-6 || std::abs(r1.Magnitude() - 1.0) > 1e-6
        || std::abs(r2.Magnitude() - 1.0) > 1e-6 || std::abs(r0.Dot(r1)) > 1e-6 || std::abs(r0.Dot(r2)) > 1e-6
        || std::abs(r1.Dot(r2)) > 1e-6 || r0.Crossed(r1).Dot(r2) < 0.0)
        return std::nullopt;
    gp_Trsf t;
    try {
        t.SetValues(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    } catch (const Standard_Failure&) {
        return std::nullopt;
    }
    return t;
}

std::optional<int> bodyContaining(const std::vector<TopoDS_Shape>& bodies, const gp_Pnt& p)
{
    for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
        if (BRepClass3d_SolidClassifier(bodies[b], p, kTolerance).State() == TopAbs_IN)
            return b;
    }
    return std::nullopt;
}

std::vector<int> matchBodyStyles(const std::vector<TopoDS_Shape>& bodies, const std::vector<BodyStyle>& styles)
{
    std::vector<int> owner(bodies.size(), -1);
    for (int s = 0; s < static_cast<int>(styles.size()); ++s) {
        const auto body = bodyContaining(bodies, styles[s].inside);
        if (body && owner[*body] < 0)
            owner[*body] = s;
    }
    return owner;
}

void numberFeatures(std::vector<Feature>& features)
{
    int highest = 0;
    for (const Feature& f : features)
        highest = std::max(highest, f.id);
    for (Feature& f : features) {
        if (f.id <= 0)
            f.id = ++highest;
    }
}

RebuildResult rebuild(const SketchLookup& sketches, const std::vector<Feature>& features)
{
    RebuildResult result;
    for (const Feature& f : features) {
        FeatureResult step = applyFeature(sketches, result.bodies, f, result.context);
        result.errors.push_back(step.error);
        if (step.ok()) {
            remember(result.context, f, step);
            result.bodies = std::move(step.bodies);
        }
    }
    return result;
}
