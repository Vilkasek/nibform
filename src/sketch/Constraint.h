#pragma once

#include <gp_Pnt2d.hxx>

using PointId = int;
using EntityId = int;
using ConstraintId = int;
constexpr int kNoId = -1;

enum class ConstraintKind
{
    // Geometric relations
    Horizontal,    // p1, p2 share Y (e1: the line they came from, if any)
    Vertical,      // p1, p2 share X (e1 as above)
    Parallel,      // lines e1, e2
    Perpendicular, // lines e1, e2
    Equal,         // lines e1, e2 (length) or circles/arcs e1, e2 (radius)
    Tangent,       // e1 line or circle/arc, e2 circle/arc; value = side (+1 / -1)
    Concentric,    // circles/arcs e1, e2
    PointOnEntity, // point p1 on line / circle / arc e1
    Midpoint,      // point p1 at the middle of line e1
    Fixed,         // point p1 pinned at `target`; with a circle e1, its radius held at `value` too

    // Dimensions (driving, `value` is the target)
    Distance, // between p1 and p2 (e1: the line, when it dimensions a line)
    Radius,   // circle / arc e1
    Diameter, // circle / arc e1
    Angle,    // lines e1, e2, rays leaving `p1` when they share it; signed radians
    PointLineDistance, // point p1 to the infinite line e1; signed (left of e1 is positive)
    LineDistance,      // middle of line e2 to the infinite line e1 (parallel lines); signed
    HorizontalDistance, // p2.x - p1.x, signed (e1: the line, when it dimensions a line)
    VerticalDistance,   // p2.y - p1.y, signed (e1 as above)

    // Relations added later (stored by name, so the order here is free)
    Symmetric, // p1 and p2 mirror each other about line e1
};

struct SketchConstraint
{
    ConstraintId id = kNoId;
    ConstraintKind kind = ConstraintKind::Horizontal;
    PointId p1 = kNoId;
    PointId p2 = kNoId;
    EntityId e1 = kNoId;
    EntityId e2 = kNoId;
    double value = 0.0;
    gp_Pnt2d target;     // Fixed only
    double offset = 0.0; // dimensions: label placement, in sketch units
};

inline bool isDimension(ConstraintKind kind)
{
    return kind == ConstraintKind::Distance || kind == ConstraintKind::Radius
        || kind == ConstraintKind::Diameter || kind == ConstraintKind::Angle
        || kind == ConstraintKind::PointLineDistance || kind == ConstraintKind::LineDistance
        || kind == ConstraintKind::HorizontalDistance || kind == ConstraintKind::VerticalDistance;
}

// Same relation between the same objects (values are ignored).
inline bool sameRelation(const SketchConstraint& a, const SketchConstraint& b)
{
    if (a.kind != b.kind)
        return false;
    const bool samePoints = (a.p1 == b.p1 && a.p2 == b.p2) || (a.p1 == b.p2 && a.p2 == b.p1);
    const bool sameEntities = (a.e1 == b.e1 && a.e2 == b.e2) || (a.e1 == b.e2 && a.e2 == b.e1);
    return samePoints && sameEntities;
}
