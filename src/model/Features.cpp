#include "model/Features.h"
#include "model/Tr.h"

#include "model/Profiles.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
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
    for (EntityId id : chain)
        edges.Append(toEdge(sketch, *sketch.entity(id)));
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

FeatureResult applyFeature(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies, const Feature& f,
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
    case Feature::Kind::Sketch:
        return missing; // nothing to build
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
        return wrap(extrude(bodies, profiles, f.value, f.operation));
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
        return wrap(f.kind == Feature::Kind::Fillet ? filletEdges(bodies, edges, f.value)
                                                    : chamferEdges(bodies, edges, f.value));
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
        BodyOperation operation = f.operation;
        if (f.source != 0) {
            const auto it = context.tools.find(f.source);
            if (it == context.tools.end())
                return failed(bodies, QT_TRANSLATE_NOOP("Model", "The patterned feature didn't apply"));
            for (const gp_Trsf& t : steps)
                copies.push_back(transformed(it->second.shape, t));
            operation = it->second.operation;
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
        return wrap(applyCopies(bodies, copies, operation));
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
        if (at.empty() || f.hole.diameter <= 0.0)
            return failed(bodies, QT_TRANSLATE_NOOP("Model", "Pick where the hole goes"));
        const double through = reachOf(bodies);
        std::vector<TopoDS_Shape> cutters;
        for (const gp_Pnt& p : at) {
            // Onto the face's plane, then into the material.
            const gp_Pnt onFace = p.Translated(gp_Vec(normal) * -gp_Vec(origin, p).Dot(gp_Vec(normal)));
            cutters.push_back(holeCutter(onFace, normal.Reversed(), f.hole, through));
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
        if (!result.ok())
            result.error = QT_TRANSLATE_NOOP("Model", "The hole doesn't reach any body");
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

void remember(ModelContext& context, const Feature& feature, const FeatureResult& result)
{
    if (result.datum) {
        std::erase_if(context.datums, [&](const Datum& d) { return d.id == result.datum->id; });
        context.datums.push_back(*result.datum);
    }
    if (result.tool)
        context.tools[feature.id] = *result.tool;
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
