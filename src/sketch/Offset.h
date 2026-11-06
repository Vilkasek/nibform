#pragma once

#include "sketch/Sketch.h"

#include <optional>
#include <vector>

// Curves joined end to end, in walking order. `reversed` walks a curve from its
// end to its start (arcs are stored counter-clockwise, lines start -> end).
struct ChainLink
{
    EntityId entity = kNoId;
    bool reversed = false;
};

struct Chain
{
    std::vector<ChainLink> links;
    bool closed = false;
};

// The chain through `seed`: neighbours are followed while exactly two curves meet
// at a point, so it stops at branches. A circle is a closed chain of its own.
std::optional<Chain> chainFrom(const Sketch& sketch, EntityId seed);

// Signed distance from `p` to the nearest curve of the chain: positive on the
// left of the walking direction. Used to pick the side and size of an offset.
double chainSide(const Sketch& sketch, const Chain& chain, const gp_Pnt2d& p);

// Adds a copy of the chain shifted `distance` to its left (negative: right), with
// corners re-intersected so the copy stays closed / connected. Lines get a parallel
// constraint to their originals, arcs share their originals' centers.
// Returns the new curves, or empty (and no change) when the offset collapses something.
std::vector<EntityId> offsetChain(Sketch& sketch, const Chain& chain, double distance);
