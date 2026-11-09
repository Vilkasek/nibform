#pragma once

#include "sketch/Sketch.h"

#include <gp_Vec2d.hxx>

#include <optional>
#include <set>
#include <utility>
#include <vector>

// Compound sketch operations: shapes built from several curves with the constraints
// that keep them what they are, corner rounding, mirroring and patterns.

// Regular polygon with one corner at `corner`, inscribed in a construction circle around
// `center`: every corner sits on the circle and the sides are equal, so it keeps its
// shape (4 degrees of freedom: where, how big, turned how far). Returns the sides.
std::vector<EntityId> addPolygon(Sketch& sketch, PointId center, PointId corner, int sides);

// Slot around the segment between two arc centers, `width` across: two lines tangent to
// two equal half-circle arcs, and a construction line between the centers.
// Returns the centerline (the two lines and two arcs follow it in the entity list).
EntityId addSlot(Sketch& sketch, PointId center1, PointId center2, double width);

// The two lines that meet at `corner`, when exactly two curves end there and both are
// lines that aren't in line with each other.
std::optional<std::pair<EntityId, EntityId>> cornerLines(const Sketch& sketch, PointId corner);

// The largest fillet radius / chamfer distance that still leaves both lines some length.
double maxFilletRadius(const Sketch& sketch, PointId corner);
double maxChamferDistance(const Sketch& sketch, PointId corner);

// Rounds the corner: the lines are cut back and joined by an arc tangent to both, with
// a radius dimension. Relations of the lines carry over; dimensions to the corner point
// itself go with it. False (and no change) when the corner can't take that radius.
// Returns the arc through `arc` when given.
bool filletCorner(Sketch& sketch, PointId corner, double radius, EntityId* arc = nullptr);

// Bevels the corner: both lines are cut back by `distance` and joined by a new line.
bool chamferCorner(Sketch& sketch, PointId corner, double distance, EntityId* bevel = nullptr);

// Mirrors `entities` about the line `axis`. Each copied point is held symmetric to its
// original (points on the axis are shared and kept on it), copied circles equal to their
// originals, so the copy follows every later change. Returns the copies.
std::vector<EntityId> mirrorEntities(Sketch& sketch, const std::set<EntityId>& entities, EntityId axis);

// `count` instances in all (the original and count - 1 copies), each `step` further.
// Copies keep the relations among the copied curves (not dimensions or pins), and copied
// circles / arcs are held equal to their originals. Returns the copies.
std::vector<EntityId> linearPattern(Sketch& sketch, const std::set<EntityId>& entities, const gp_Vec2d& step,
                                    int count);

// The curves with the points they use and the relations among just them (dimensions
// too), to copy elsewhere.
Sketch::Contents copyEntities(const Sketch& sketch, const std::set<EntityId>& entities);

// Adds a copied part to the sketch under new ids, shifted by `offset`; returns the new curves.
std::vector<EntityId> pasteContents(Sketch& sketch, const Sketch::Contents& part, const gp_Vec2d& offset = gp_Vec2d());

// `count` instances spread evenly over a full turn around `center` (as above; horizontal /
// vertical relations don't carry over, they don't survive the turn).
std::vector<EntityId> circularPattern(Sketch& sketch, const std::set<EntityId>& entities, const gp_Pnt2d& center,
                                      int count);
