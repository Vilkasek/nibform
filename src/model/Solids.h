#pragma once

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
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

struct ModelResult
{
    std::vector<TopoDS_Shape> bodies;
    std::string error; // empty on success; nothing changes otherwise

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

// Combines the `selected` bodies (by index) into one: union, intersection, or the first
// minus the others.
ModelResult combine(const std::vector<TopoDS_Shape>& bodies, const std::vector<int>& selected, BooleanKind kind);
