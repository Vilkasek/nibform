#pragma once

#include "model/Solids.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#include <utility>
#include <vector>

// The finishing features (M16): draft, ribs, embossed / engraved profiles and fillets
// whose radius changes along the edge.

// Tilts the faces (each with its body's index) by `degrees` about where they cross the
// `neutral` plane, so the part comes out of a mold along `pull` (the plane's normal):
// positive angles narrow it toward `pull`.
ModelResult draftFaces(const std::vector<TopoDS_Shape>& bodies, const std::vector<std::pair<int, TopoDS_Face>>& faces,
                       const gp_Pln& neutral, double degrees);

// A wall `thickness` thick (centered on the sketch plane, whose normal is `normal`) that
// fills from the open `path` across to the body, its ends carried on to the walls
// around: what is enclosed on one side of the path (the other with `flip`) joins the body.
ModelResult ribFrom(const std::vector<TopoDS_Shape>& bodies, const TopoDS_Wire& path, const gp_Dir& normal,
                    double thickness, bool flip);

// The profiles (faces on a plane) projected along `along` onto `target`, a face of body
// `body` (flat or curved) where it was picked at `near`, raised `depth` above it (emboss)
// or sunk into it (engrave).
ModelResult embossProfiles(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Face& target,
                           const gp_Pnt& near, const std::vector<TopoDS_Face>& profiles, const gp_Dir& along,
                           double depth, bool engrave);

// Rounds each edge with a radius going from `first` at one end to `second` at the other
// (`flipped`: the other way round).
struct VaryingEdge
{
    EdgeRef edge;
    bool flipped = false;
};
ModelResult filletVarying(const std::vector<TopoDS_Shape>& bodies, const std::vector<VaryingEdge>& edges, double first,
                          double second);

// Sets back the corners of a fillet: where three or more of `edges` (of the bodies before
// rounding) meet, everything of the rounded body within `setback` of the corner gives way
// to one smooth patch, joined tangent to the fillets and faces around it.
ModelResult setbackCorners(const std::vector<TopoDS_Shape>& before, const ModelResult& rounded,
                           const std::vector<EdgeRef>& edges, double setback);
