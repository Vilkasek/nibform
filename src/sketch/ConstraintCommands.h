#pragma once

#include "sketch/Sketch.h"

#include <set>

struct SketchSelection
{
    std::set<PointId> points;
    std::set<EntityId> entities;
    std::set<ConstraintId> constraints;

    bool empty() const { return points.empty() && entities.empty() && constraints.empty(); }
    std::size_t size() const { return points.size() + entities.size() + constraints.size(); }
    bool operator==(const SketchSelection&) const = default;
};

// What the constraint buttons do with the current selection.
enum class ConstraintCommand
{
    Horizontal,    // lines, or two points
    Vertical,      // lines, or two points
    Parallel,      // 2+ lines
    Perpendicular, // 2 lines
    Equal,         // 2+ lines, or 2+ circles/arcs
    Tangent,       // line + circle/arc, or two circles/arcs
    Concentric,    // 2+ circles/arcs
    Coincident,    // two points (merged), or a point and a curve
    Midpoint,      // a point and a line
    Fix,           // points, or every point of the selected curves
    Dimension,     // line / two points (distance), circle (diameter), arc (radius), two lines (angle)
    HorizontalDimension, // a line or two points: the distance along X
    VerticalDimension,   // a line or two points: the distance along Y
    Symmetric,     // two points and a line (the points mirror each other about it)
};

struct CommandResult
{
    bool applied = false;
    ConstraintId dimension = kNoId; // the dimension that was added, if any
};

// Adds the constraints `command` means for `selection`. When it does not fit the
// selection nothing changes and `applied` is false. `labelOffset` is how far
// (in sketch units) a new dimension's label sits from its geometry.
CommandResult applyCommand(ConstraintCommand command, Sketch& sketch, const SketchSelection& selection,
                           double labelOffset);
