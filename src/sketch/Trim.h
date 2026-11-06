#pragma once

#include "sketch/DrawList.h"
#include "sketch/Sketch.h"

#include <optional>
#include <vector>

// The stretch of one curve between its nearest cuts (intersections with other
// curves, or sketch points lying on it) around a pointer position: what Trim removes.
// Parameters: line t in [0, 1] from start to end; arc angle in [0, sweep] from its
// start; circle absolute angle, with the piece running counter-clockwise from -> to.
struct TrimPiece
{
    EntityId entity = kNoId;
    double from = 0.0;
    double to = 0.0;
    bool whole = false; // nothing cuts the curve (or a circle only once): it goes entirely
};

// The piece under `cursor`, picking curves within `tolerance` (sketch units).
std::optional<TrimPiece> findTrimPiece(const Sketch& sketch, const gp_Pnt2d& cursor, double tolerance);

// Polyline of the piece, for highlighting.
std::vector<Segment2d> trimPieceSegments(const Sketch& sketch, const TrimPiece& piece);

// Removes the piece. What is left keeps the curve's id (a second leftover becomes a new
// curve, a circle becomes an arc); new ends get pinned onto the curve that cut them.
// Constraints that still make sense follow the leftovers, the rest are dropped.
bool trim(Sketch& sketch, const TrimPiece& piece);
