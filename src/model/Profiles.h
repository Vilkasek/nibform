#pragma once

#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <set>
#include <vector>

// The sketch curve as a 3D edge on the sketch plane.
TopoDS_Edge toEdge(const Sketch& sketch, const SketchEntity& entity);

// Closed regions bounded by the sketch's curves, as planar faces on the sketch plane
// (facing along its normal). Crossing curves split regions; a loop inside another
// region becomes both a hole in it and a region of its own. Open curves are ignored.
std::vector<TopoDS_Face> findRegions(const Sketch& sketch);

// Copies the edges of `face` (lying in the sketch's plane) into the sketch as fixed
// construction geometry, to snap and dimension to.
void projectFaceEdges(Sketch& sketch, const TopoDS_Shape& face);

// Projects the edges of `shape` (an edge, a face's outline, a body) straight onto the
// sketch plane and adds them pinned in place. Lines stay lines; circles square to the
// sketch stay circles / arcs, slanted ones become ellipses (or lines, seen edge-on);
// anything else becomes a spline through points along it. Curves already in the sketch
// aren't added again. Returns the new curves.
std::set<EntityId> projectEdges(Sketch& sketch, const TopoDS_Shape& shape, bool construction);

// Index of the region containing `p` (a point on the sketch plane).
std::optional<std::size_t> regionAt(const std::vector<TopoDS_Face>& regions, const gp_Pnt& p);
