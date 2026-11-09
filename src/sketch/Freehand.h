#pragma once

#include "sketch/Sketch.h"

#include <gp_Pnt2d.hxx>

#include <set>
#include <vector>

// Freehand strokes: what a rough pen stroke was meant to be, and the clean geometry
// that replaces it.

struct StrokeShape
{
    enum class Kind { None, Line, Polyline, Rectangle, Circle, Arc, Scribble };

    Kind kind = Kind::None;
    // Line: the two ends. Polyline: its corners in order (`closed`: the last joins the
    // first, which isn't repeated). Rectangle: its four corners in order.
    // Arc: start and end, counter-clockwise from start to end.
    std::vector<gp_Pnt2d> points;
    bool closed = false;
    gp_Pnt2d center; // Circle, Arc
    double radius = 0.0;
};

// What `stroke` (pen positions in sketch units, in drawing order) looks like:
//  - a line, an open polyline (corners) or a closed polygon; four corners at right
//    angles make a rectangle (axis-aligned when it is nearly so);
//  - a circle when it closes and stays round, an arc when it is a round part of one;
//  - a scribble: back and forth over the same spot (to delete what it covers).
// `tolerance` is how far off a stroke can wander and still count as straight / round
// for small shapes (bigger ones get proportionally more slack). None when unclear.
StrokeShape recognizeStroke(const std::vector<gp_Pnt2d>& stroke, double tolerance);

// Adds the shape to the sketch: ends and corners within `snap` of existing points join
// them, sides that are nearly horizontal / vertical become exactly so (with the
// relation), a rotated rectangle keeps its right angles. Returns the new curves.
std::vector<EntityId> addStrokeShape(Sketch& sketch, const StrokeShape& shape, double snap);

// Curves that `stroke` passes within `tolerance` of.
std::set<EntityId> entitiesCrossed(const Sketch& sketch, const std::vector<gp_Pnt2d>& stroke, double tolerance);
