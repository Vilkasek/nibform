#pragma once

#include "model/Solids.h"
#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <functional>
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
    };

    Kind kind = Kind::Extrude;
    int sketch = -1; // Sketch: its id
    double value = 0.0;
    BodyOperation operation = BodyOperation::Auto;
    BooleanKind boolean = BooleanKind::Union;
    std::vector<RegionPick> regions;
    std::vector<FacePick> faces;
    std::vector<EdgePick> edges;
    std::vector<gp_Pnt> bodies; // in pick order
};

using SketchLookup = std::function<const Sketch*(int id)>;

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

// Applies one feature on top of `bodies`.
ModelResult applyFeature(const SketchLookup& sketches, const std::vector<TopoDS_Shape>& bodies, const Feature& feature);

struct RebuildResult
{
    std::vector<TopoDS_Shape> bodies;
    std::vector<std::string> errors; // per feature; empty when it applied (a failed one is skipped)
};

RebuildResult rebuild(const SketchLookup& sketches, const std::vector<Feature>& features);
