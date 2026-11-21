#pragma once

#include "model/SheetMetal.h"
#include "model/Solids.h"
#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <array>
#include <functional>
#include <map>
#include <memory>
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

// Flat faces are picked by their middle; round and curved ones (threads, emboss) by a
// point on them, with the normal there.
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

// The most instances a pattern makes, as the Pattern panel allows; project files are
// held to it too.
constexpr int kMaxPatternCount = 500;

struct Feature
{
    enum class Kind
    {
        Sketch,  // a sketch was made (keeps its place in the timeline; builds nothing); it follows `plane`
        Extrude, // regions and / or body faces, `value` far from `startOffset`, with `operation`
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
        Move,    // bodies moved (or copied, `copy`) by `matrix`
        Import,  // the solids of `shape` as new bodies, or all of it as a `reference`
        Thread,  // `thread` on the round face `faces[0]` (cosmetic, or a groove cut)
        Draft,   // `faces` tilted by `value` degrees about the neutral `plane`
        Rib,     // a wall `value` thick from the open path (`pathSketch`, `pathEntity`) to the body; `flip`: the other side
        Emboss,  // regions onto the face `faces[0]`, `value` high (`operation` Join) or deep (Cut)
        // Sheet metal (M18), bent by the rules `sheet` of the base flange / conversion:
        SheetBase,    // a new sheet: regions (flat, `flip`: the other side of the sketch), or the open path
                      // (`pathSketch`, `pathEntity`) bent at its corners, `value` deep
        Flange,       // from the edge `edges[0]`, `value` long, shaped by `flange`; `flip`: bent the other way
        Hem,          // the edge `edges[0]` folded back flat, `value` long, `flange.gap` apart
        Tab,          // regions on a side of a sheet, as thick as it
        Relief,       // a corner relief `value` across at `points[0]` (`flange.square`)
        SheetConvert, // the shelled body of `faces[0]` (a side of a wall, which stays) to sheet metal
    };
    enum class PatternKind { Linear, Circular };
    enum class DatumKind { OffsetPlane, AnglePlane, ThreePointPlane, Axis, TwoPointAxis };

    Kind kind = Kind::Extrude;
    int id = 0; // stable, for patterns and datums to refer to
    int sketch = -1; // Sketch: its id
    double value = 0.0;
    // Fillet: the radius at the end each edge was picked toward (`value` at the other);
    // 0: the same all along.
    double endValue = 0.0;
    // Fillet: how far from a corner where three or more of its edges meet the fillets give
    // way to one smooth patch; 0: the usual corner.
    double setback = 0.0;
    bool flip = false; // Rib: fills the other side of its line
    // Extrude: where it starts, this far from the profiles along their normal (negative:
    // the other way); 0: on the sketch plane / face.
    double startOffset = 0.0;
    BodyOperation operation = BodyOperation::Auto;
    BooleanKind boolean = BooleanKind::Union;
    std::vector<RegionPick> regions;
    std::vector<FacePick> faces;
    std::vector<EdgePick> edges;
    std::vector<gp_Pnt> bodies; // in pick order

    AxisRef axis;   // Revolve: around it; Pattern: along (linear) or around (circular) it; Datum
    PlaneRef plane; // Mirror: about it; Datum: the plane it starts from; Sketch: what it was drawn on
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
    // Thread
    ThreadSpec thread;
    std::vector<SketchPointRef> holeAt;
    std::vector<gp_Pnt> points; // Hole: where else; Datum: through these
    DatumKind datum = DatumKind::OffsetPlane; // `value`: offset (mm) / angle (degrees)
    // Move: a rigid motion, the 3 × 4 matrix row by row (rotation | translation).
    std::vector<double> matrix;
    bool copy = false;
    // Import: the geometry (BREP text, shared between copies of the history), where it
    // came from, and whether it is only to look at.
    std::shared_ptr<const std::string> shape;
    std::string label;
    bool reference = false;
    // Sheet metal
    SheetSpec sheet;
    FlangeSpec flange;
    // Values typed as expressions, by field (see featureField() in Parameters.h): they
    // follow the parameters. A value without one is a plain number.
    std::map<std::string, Expression> expressions;
};

std::vector<double> matrixOf(const gp_Trsf& trsf);
std::optional<gp_Trsf> trsfOf(const std::vector<double>& matrix); // empty unless a rigid motion

// How a body looks, kept apart from the history: found again after rebuilds by a point
// inside it (moved along when the body moves).
struct BodyStyle
{
    gp_Pnt inside;
    std::string name;   // empty: "Body n"
    bool hidden = false;
    std::optional<std::array<double, 3>> color; // sRGB 0..1; empty: the default
};

// The body (index) that has `p` inside it, if any.
std::optional<int> bodyContaining(const std::vector<TopoDS_Shape>& bodies, const gp_Pnt& p);
// For each body, the style that belongs to it (index into `styles`) or -1; a style goes
// to one body at most, earlier styles first.
std::vector<int> matchBodyStyles(const std::vector<TopoDS_Shape>& bodies, const std::vector<BodyStyle>& styles);

// A sheet metal body: the rules it is bent by (from the feature that made it), and its
// fixed face (the side that stays as it lies when it is unfolded), by which the body is
// found again.
struct SheetBody
{
    int feature = 0;
    SheetSpec spec;
    FacePick fixed;
};

// What a replay knows besides the bodies: the datums so far, and each applied feature's
// tool solid (by feature id), for patterns.
struct ModelContext
{
    std::vector<Datum> datums;
    std::map<int, FeatureTool> tools;
    std::vector<TopoDS_Shape> references; // imported to look at, not to build on
    std::map<int, std::vector<ThreadMark>> threads; // by feature id: threads and tapped holes
    // By sketch id: where sketches drawn on a body face or a datum plane sit now (they
    // follow it when earlier features move it).
    std::map<int, gp_Ax3> sketchPlanes;
    std::vector<SheetBody> sheets;
    const Datum* datum(int id) const;
    std::vector<ThreadMark> allThreads() const;
};

std::optional<gp_Ax1> resolveAxis(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies,
                                  const AxisRef& ref, const ModelContext& context);
std::optional<gp_Ax3> resolvePlane(const std::vector<TopoDS_Shape>& bodies, const PlaneRef& ref,
                                   const ModelContext& context);
// What a sketch on `plane` was drawn on: a flat body face or a datum plane it lies in,
// facing the same way. Empty for sketches on the base planes (they stay where they are).
std::optional<PlaneRef> planeUnder(const gp_Ax3& plane, const std::vector<TopoDS_Shape>& bodies,
                                   const ModelContext& context);
// The same position and axes (within rounding).
bool samePlane(const gp_Ax3& a, const gp_Ax3& b);
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
// Any face (flat or curved) through the picked point, facing the same way.
std::optional<std::pair<TopoDS_Face, int>> findSurface(const std::vector<TopoDS_Shape>& bodies, const FacePick& pick);
// A pick of a face at `point` on it (the normal there).
FacePick surfacePick(const TopoDS_Face& face, const gp_Pnt& point);

// Picking what a feature made, for patterns: the feature (id) whose cut or added solid
// shaped `face`, i.e. the face lies on that solid's surface. The latest such feature
// wins; features that made a new body don't count (their faces stand for the body).
std::optional<int> featureOfFace(const std::vector<Feature>& features, const ModelContext& context,
                                 const TopoDS_Face& face);
// The faces of `bodies` that lie on the surface of `tool` (to show what a feature made).
std::vector<TopoDS_Face> facesOnTool(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Shape& tool);

// Applies one feature on top of `bodies` (a Datum feature returns its datum).
struct FeatureResult : ModelResult
{
    std::optional<Datum> datum;
    std::optional<TopoDS_Shape> reference;
    std::vector<ThreadMark> threads;
    std::optional<gp_Ax3> sketchPlane; // Sketch: where it sits now
    std::optional<SheetBody> sheet;     // a sheet metal body it made
};

// The sheet metal bodies as they are now: which body each is, and its fixed face.
struct SheetFound
{
    SheetBody sheet;
    int body = -1;
    TopoDS_Face fixed;
};
std::vector<SheetFound> findSheets(const std::vector<TopoDS_Shape>& bodies, const ModelContext& context);
// The sheet the body (index) is, if it is one.
std::optional<SheetFound> sheetOf(const std::vector<TopoDS_Shape>& bodies, const ModelContext& context, int body);
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
