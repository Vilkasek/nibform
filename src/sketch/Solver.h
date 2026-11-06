#pragma once

#include "sketch/Sketch.h"

#include <gp_Vec2d.hxx>

#include <map>
#include <set>

struct SolveResult
{
    bool ok = false; // every constraint holds
    int variables = 0;
    int equations = 0;
    int dof = 0; // degrees of freedom left: variables - rank of the constraint Jacobian
    std::set<PointId> definedPoints;     // cannot move without breaking a constraint
    std::set<EntityId> definedEntities; // all of their points (and radius) are defined
};

// Moves points and radii so that every constraint holds, changing the sketch as
// little as possible. `drag` asks for points to follow the pointer: they land on
// their targets when the constraints allow it, otherwise as close as they can get.
// On failure the sketch is left untouched. `analyze` also fills the DOF fields.
SolveResult solve(Sketch& sketch, const std::map<PointId, gp_Pnt2d>& drag = {}, bool analyze = true);

// Degrees of freedom and fully defined geometry, without moving anything.
SolveResult analyze(const Sketch& sketch);

// Distance from `p` to the infinite line through `line`; positive on its left.
double signedDistanceToLine(const Sketch& sketch, const SketchEntity& line, const gp_Pnt2d& p);

// Current value of a dimension: millimetres (signed for distances to a line), or signed radians for Angle.
double measure(const Sketch& sketch, const SketchConstraint& c);

// Direction of `line` leaving `pivot` (or start -> end when the pivot is not one of its points).
gp_Vec2d rayDirection(const Sketch& sketch, const SketchEntity& line, PointId pivot);
