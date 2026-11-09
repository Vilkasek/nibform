#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// What an extrusion does to the bodies it meets.
enum class BodyOperation
{
    Auto,    // pulled out of a body face: join; pushed into one: cut. From a sketch:
             // cut when it starts inside a body (e.g. a sketch on a face pushed into
             // it, even if it comes out the other side), join when it overlaps or
             // touches one, else a new body
    NewBody, // always a separate body
    Join,    // fuse with the bodies it touches (a new body when it touches none)
    Cut,     // subtract from the bodies it touches
};

enum class BooleanKind { Union, Subtract, Intersect };

enum class HoleKind { Simple, Counterbore, Countersink };

// A hole's shape: a cylinder `diameter` wide, `depth` deep (through everything when 0),
// under a wider counterbore (`headDiameter`, `headDepth` deep) or a 90° countersink
// (`headDiameter` across at the surface).
struct HoleSpec
{
    HoleKind kind = HoleKind::Simple;
    double diameter = 5.0;
    double depth = 0.0;
    double headDiameter = 9.0;
    double headDepth = 3.0;
};

// A planar face to push or pull: a sketch region, or a face of an existing body.
struct Profile
{
    TopoDS_Face face;
    gp_Dir normal;  // the direction a positive distance goes; outward for body faces
    int body = -1;  // index of the body the face belongs to, -1 for sketch regions
};

// An edge of body `body`, as picked for a fillet or chamfer.
struct EdgeRef
{
    int body = -1;
    TopoDS_Edge edge;
};

// The solid a feature added or took away, and how: patterns repeat it.
struct FeatureTool
{
    TopoDS_Shape shape;
    BodyOperation operation = BodyOperation::NewBody; // never Auto
};

struct ModelResult
{
    std::vector<TopoDS_Shape> bodies;
    std::string error; // empty on success; nothing changes otherwise
    std::optional<FeatureTool> tool; // set by features that combine one solid with the bodies

    bool ok() const { return error.empty(); }
};

// Outward normal of a planar face (taking its orientation into account).
std::optional<gp_Dir> planarNormal(const TopoDS_Face& face);

double volumeOf(const TopoDS_Shape& shape);

// Whether extruding sketch profiles by `distance` starts inside material: then Auto cuts.
bool startsInside(const std::vector<TopoDS_Shape>& bodies, const std::vector<Profile>& profiles, double distance);

// Extrudes the profiles by `distance` along their normals (negative goes the other way)
// and applies the result to `bodies`.
ModelResult extrude(const std::vector<TopoDS_Shape>& bodies, const std::vector<Profile>& profiles,
                    double distance, BodyOperation operation);

// Where a fillet / chamfer handle sits on an edge of `body`: the edge's middle, and the
// direction into the material halfway between its two faces (dragging along it grows the
// round). Empty for edges without two neighbouring faces.
std::optional<std::pair<gp_Pnt, gp_Dir>> edgeHandle(const TopoDS_Shape& body, const TopoDS_Edge& edge);

// Rounds the edges with `radius`, or cuts them flat `distance` back on both faces.
ModelResult filletEdges(const std::vector<TopoDS_Shape>& bodies, const std::vector<EdgeRef>& edges, double radius);
ModelResult chamferEdges(const std::vector<TopoDS_Shape>& bodies, const std::vector<EdgeRef>& edges,
                         double distance);

// Hollows bodies out, leaving walls `thickness` thick inside their surface; the given
// faces (of each body, at least one) are removed to open the shell there.
ModelResult shellBodies(const std::vector<TopoDS_Shape>& bodies, const std::vector<std::pair<int, TopoDS_Face>>& open,
                        double thickness);

// Combines `tool` with the bodies: joined to those it touches, cut from them, or a new
// body. Auto: a new body when it touches nothing, a cut when it lies (almost) wholly in
// material, a join otherwise. The result's `tool` says what was done.
ModelResult applyTool(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Shape& tool, BodyOperation operation);

// The solids the profiles sweep out (one solid, fused, or empty with `error` set).
// Revolve: around `axis` by `degrees` (a full turn at 360).
std::optional<TopoDS_Shape> revolveProfiles(const std::vector<TopoDS_Face>& profiles, const gp_Ax1& axis,
                                            double degrees, std::string& error);
// Sweep: along the connected `path`, the profiles carried with it.
std::optional<TopoDS_Shape> sweepProfiles(const std::vector<TopoDS_Face>& profiles, const TopoDS_Wire& path,
                                          std::string& error);
// Loft: through the outlines of the profiles, in order (2 or more).
std::optional<TopoDS_Shape> loftProfiles(const std::vector<TopoDS_Face>& profiles, std::string& error);

// What drills a hole at `at`, going `into` the material; `through` is how far a hole
// through everything reaches.
TopoDS_Shape holeCutter(const gp_Pnt& at, const gp_Dir& into, const HoleSpec& spec, double through);

// Combines the `selected` bodies (by index) into one: union, intersection, or the first
// minus the others.
ModelResult combine(const std::vector<TopoDS_Shape>& bodies, const std::vector<int>& selected, BooleanKind kind);
