#pragma once

#include "sketch/DrawList.h"
#include "sketch/Sketch.h"

#include <vector>

struct SnapResult
{
    enum class Kind { None, Point, Midpoint, Origin, Aligned };

    Kind kind = Kind::None;
    gp_Pnt2d pos;
    PointId pointId = kNoId;       // set when snapped onto an existing sketch point
    EntityId entity = kNoId;       // the line whose midpoint was snapped to
    std::vector<Segment2d> guides; // inference lines to show (alignment)

    bool snapped() const { return kind != Kind::None; }
};

// Pulls a raw cursor position onto meaningful sketch locations, in priority order:
// existing points, the origin, line midpoints, then horizontal/vertical alignment
// with the tool's anchors, existing points and the origin.
class Snapper
{
public:
    // Tolerances in sketch units (the editor converts them from screen pixels).
    SnapResult snap(const Sketch& sketch, const gp_Pnt2d& raw, double pointTolerance,
                    double alignTolerance, const std::vector<gp_Pnt2d>& anchors = {}) const;
};
