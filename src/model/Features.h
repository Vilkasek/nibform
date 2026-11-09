#pragma once

#include "model/Solids.h"
#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The model's parametric history. Bodies are not stored but rebuilt by replaying the
// features in order, so changing a sketch or a value updates everything after it.
//
// Faces and edges are recreated on every rebuild, so a feature remembers what it was
// applied to as geometry, and finds it again by that: a region by a point inside it,
// a face by its middle and normal, an edge by its middle and direction, a body by a
// point inside it.

struct RegionPick
{
    int sketch = -1; // sketch id
    gp_Pnt2d inside; // in sketch coordinates
};

struct FacePick
{
    gp_Pnt middle;
    gp_Dir normal; // outward
};

struct EdgePick
{
    gp_Pnt middle;
    gp_Dir direction;
};

using SketchLookup = std::function<const Sketch*(int id)>;

// Construction geometry: base planes and axes through the origin (negative ids), and
// construction planes / axes made by Datum features (their feature ids).
constexpr int kBaseXY = -1, kBaseXZ = -2, kBaseYZ = -3, kBaseX = -4, kBaseY = -5, kBaseZ = -6;

struct Datum
{
    int id = 0;
    bool plane = true;
    gp_Ax3 frame; // a plane: its position and normal; an axis: location and direction
    gp_Ax1 axis() const { return gp_Ax1(frame.Location(), frame.Direction()); }
};

// The base plane / axis with that id, if it is one.
std::optional<Datum> baseDatum(int id);

// An axis (or a direction) as picked: a straight body edge (its line) or a round one
// (its center axis), a sketch line, or a datum.
struct AxisRef
{
    enum class Kind { None, Edge, SketchLine, Datum };
    Kind kind = Kind::None;
    EdgePick edge;
    int sketch = -1; // SketchLine: the sketch's id and the line's
    int entity = -1;
    int datum = 0;
};

// A plane as picked: a flat body face (facing out of it) or a datum plane.
struct PlaneRef
{
    enum class Kind { None, Face, Datum };
    Kind kind = Kind::None;
    FacePick face;
    int datum = 0;
};

// A point of a sketch (a hole placed on it moves with it).
struct SketchPointRef
{
    int sketch = -1;
    int point = -1;
};

struct Feature
{
    enum class Kind
    {
        Sketch,  // a sketch was made (keeps its place in the timeline; builds nothing)
        Extrude, // regions and / or body faces, `value` far, with `operation`
        Fillet,  // edges, radius `value`
        Chamfer, // edges, size `value`
        Combine, // bodies, with `boolean`
        Delete,  // bodies
        Shell,   // faces to open, wall thickness `value`
        Revolve, // regions around `axis` by `value` degrees, with `operation`
        Sweep,   // regions along `path` (a chain of sketch curves), with `operation`
        Loft,    // through regions (in order), with `operation`
        Mirror,  // copies of bodies mirrored about `plane`, with `operation`
        Pattern, // copies of feature `source` (or of bodies, when 0) along / around `axis`
        Hole,    // holes into `faces[0]` at `holeAt` / `points`, shaped by `hole`
        Datum,   // a construction plane or axis (`datum`)
    };
    enum class PatternKind { Linear, Circular };
    enum class DatumKind { OffsetPlane, AnglePlane, ThreePointPlane, Axis, TwoPointAxis };

    Kind kind = Kind::Extrude;
    int id = 0; // stable, for patterns and datums to refer to
    int sketch = -1; // Sketch: its id
    double value = 0.0;
    BodyOperation operation = BodyOperation::Auto;
    BooleanKind boolean = BooleanKind::Union;
    std::vector<RegionPick> regions;
    std::vector<FacePick> faces;
    std::vector<EdgePick> edges;
    std::vector<gp_Pnt> bodies; // in pick order

    AxisRef axis;   // Revolve: around it; Pattern: along (linear) or around (circular) it; Datum
    PlaneRef plane; // Mirror: about it; Datum: the plane it starts from
    // Sweep: the path, the chain of connected curves through this sketch curve.
    int pathSketch = -1;
    int pathEntity = -1;
    // Pattern: `count` instances in all; `value` apart (linear), or spread over `value`
    // degrees (circular; a full turn at 360).
    PatternKind pattern = PatternKind::Linear;
    int count = 2;
    int source = 0;
    // Hole
    HoleSpec hole;
    std::vector<SketchPointRef> holeAt;
    std::vector<gp_Pnt> points; // Hole: where else; Datum: through these
    DatumKind datum = DatumKind::OffsetPlane; // `value`: offset (mm) / angle (degrees)
};

// What a replay knows besides the bodies: the datums so far, and each applied feature's
// tool solid (by feature id), for patterns.
struct ModelContext
{
    std::vector<Datum> datums;
    std::map<int, FeatureTool> tools;
    const Datum* datum(int id) const;
};

std::optional<gp_Ax1> resolveAxis(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies,
                                  const AxisRef& ref, const ModelContext& context);
std::optional<gp_Ax3> resolvePlane(const std::vector<TopoDS_Shape>& bodies, const PlaneRef& ref,
                                   const ModelContext& context);
// The connected chain of curves through `entity` of the sketch, as a wire in 3D.
std::optional<TopoDS_Wire> sketchPath(const Sketch& sketch, int entity);

// Recording picks.
std::optional<gp_Pnt2d> insidePoint(const Sketch& sketch, const TopoDS_Face& region);
FacePick facePick(const TopoDS_Face& face);
EdgePick edgePick(const TopoDS_Edge& edge);
std::optional<gp_Pnt> insidePoint(const TopoDS_Shape& solid);

// Finding them again in the current model.
std::optional<TopoDS_Face> findRegion(const SketchLookup& sketches, const RegionPick& pick);
// The best matching planar face, and the index of its body.
std::optional<std::pair<TopoDS_Face, int>> findFace(const std::vector<TopoDS_Shape>& bodies, const FacePick& pick);
std::optional<EdgeRef> findEdge(const std::vector<TopoDS_Shape>& bodies, const EdgePick& pick);
std::optional<int> findBody(const std::vector<TopoDS_Shape>& bodies, const gp_Pnt& inside);

// Applies one feature on top of `bodies` (a Datum feature returns its datum).
struct FeatureResult : ModelResult
{
    std::optional<Datum> datum;
};
FeatureResult applyFeature(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies,
                           const Feature& feature, const ModelContext& context = {});
// Records what an applied feature leaves for later ones (its datum, its tool).
void remember(ModelContext& context, const Feature& feature, const FeatureResult& result);
// Ids for features that have none (older projects), after the highest one.
void numberFeatures(std::vector<Feature>& features);

struct RebuildResult
{
    std::vector<TopoDS_Shape> bodies;
    ModelContext context;
    std::vector<std::string> errors; // per feature; empty when it applied (a failed one is skipped)
};

RebuildResult rebuild(const SketchLookup& sketches, const std::vector<Feature>& features);
