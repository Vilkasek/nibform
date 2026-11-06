#pragma once

#include "sketch/Sketch.h"

#include <gp_Pnt2d.hxx>

#include <set>
#include <vector>

struct Segment2d
{
    gp_Pnt2d a;
    gp_Pnt2d b;
};

// Flat 2D geometry ready to be drawn on a sketch plane. Items in the "accent"
// lists are drawn in the highlight color (selection, hover, snap markers).
struct DrawList
{
    std::vector<Segment2d> segments;
    std::vector<Segment2d> accentSegments;
    std::vector<gp_Pnt2d> points;
    std::vector<gp_Pnt2d> accentPoints;

    bool isEmpty() const
    {
        return segments.empty() && accentSegments.empty() && points.empty() && accentPoints.empty();
    }
};

// Appends a polyline approximation of the CCW arc from `startAngle` sweeping `sweep` radians.
void appendArc(std::vector<Segment2d>& out, const gp_Pnt2d& center, double radius,
               double startAngle, double sweep);

// Tessellates the sketch; entities in `accent` go to the accent lists.
DrawList tessellate(const Sketch& sketch, const std::set<EntityId>& accent = {});
