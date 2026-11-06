#include "model/Features.h"

#include "model/Profiles.h"

#include <BRepAdaptor_Curve.hxx>
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

#include <algorithm>
#include <cmath>
#include <limits>

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
    BRepMesh_IncrementalMesh(copy, 0.1);
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

// --- Replaying ----------------------------------------------------------------------

ModelResult applyFeature(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies, const Feature& f)
{
    ModelResult missing;
    missing.bodies = bodies;
    switch (f.kind) {
    case Feature::Kind::Sketch:
        return missing; // nothing to build
    case Feature::Kind::Extrude: {
        std::vector<Profile> profiles;
        for (const RegionPick& r : f.regions) {
            const auto face = findRegion(sketches, r);
            if (!face) {
                missing.error = "Nie ma już obszaru szkicu, który był wyciągany";
                return missing;
            }
            profiles.push_back({*face, sketches(r.sketch)->plane().Direction(), -1});
        }
        for (const FacePick& p : f.faces) {
            const auto face = findFace(bodies, p);
            if (!face) {
                missing.error = "Nie ma już ściany, która była wyciągana";
                return missing;
            }
            profiles.push_back({face->first, *planarNormal(face->first), face->second});
        }
        return extrude(bodies, profiles, f.value, f.operation);
    }
    case Feature::Kind::Fillet:
    case Feature::Kind::Chamfer: {
        std::vector<EdgeRef> edges;
        for (const EdgePick& p : f.edges) {
            const auto edge = findEdge(bodies, p);
            if (!edge) {
                missing.error = "Nie ma już krawędzi, która była zaokrąglana";
                return missing;
            }
            edges.push_back(*edge);
        }
        return f.kind == Feature::Kind::Fillet ? filletEdges(bodies, edges, f.value)
                                               : chamferEdges(bodies, edges, f.value);
    }
    case Feature::Kind::Shell: {
        std::vector<std::pair<int, TopoDS_Face>> open;
        for (const FacePick& p : f.faces) {
            const auto face = findFace(bodies, p);
            if (!face) {
                missing.error = "Nie ma już ściany, która była otwierana";
                return missing;
            }
            open.push_back({face->second, face->first});
        }
        return shellBodies(bodies, open, f.value);
    }
    case Feature::Kind::Combine:
    case Feature::Kind::Delete: {
        std::vector<int> picked;
        for (const gp_Pnt& p : f.bodies) {
            const auto body = findBody(bodies, p);
            if (!body) {
                missing.error = "Nie ma już bryły, której to dotyczyło";
                return missing;
            }
            picked.push_back(*body);
        }
        if (f.kind == Feature::Kind::Combine)
            return combine(bodies, picked, f.boolean);
        ModelResult kept;
        for (int b = 0; b < static_cast<int>(bodies.size()); ++b) {
            if (std::find(picked.begin(), picked.end(), b) == picked.end())
                kept.bodies.push_back(bodies[b]);
        }
        return kept;
    }
    }
    return missing;
}

RebuildResult rebuild(const SketchLookup& sketches, const std::vector<Feature>& features)
{
    RebuildResult result;
    for (const Feature& f : features) {
        ModelResult step = applyFeature(sketches, result.bodies, f);
        result.errors.push_back(step.error);
        if (step.ok())
            result.bodies = std::move(step.bodies);
    }
    return result;
}
