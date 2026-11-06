#include "sketch/Offset.h"

#include "sketch/Geometry.h"

#include <gp_Vec2d.hxx>

#include <cmath>
#include <limits>
#include <set>

namespace {

constexpr double kEps = 1e-9;

PointId walkStart(const SketchEntity& e, bool reversed)
{
    return reversed ? e.b : e.a;
}

PointId walkEnd(const SketchEntity& e, bool reversed)
{
    return reversed ? e.a : e.b;
}

// One curve of the chain, with what its offset copy needs. Held by value: adding
// the copy to the sketch may reallocate its entity list.
struct Piece
{
    EntityId id = kNoId;
    EntityKind kind = EntityKind::Line;
    PointId centerId = kNoId;
    bool reversed = false;
    gp_Pnt2d start, end; // in walking order
    // Lines
    gp_Vec2d dir, normal; // unit; normal points left of the walk
    // Arcs: walking counter-clockwise (s = +1) the left side is toward the center
    gp_Pnt2d center;
    double radius = 0.0;
    double s = 1.0;

    bool isLine() const { return kind == EntityKind::Line; }

    // Where `p` (one of its ends) moves to when the curve is offset by `d`.
    gp_Pnt2d shifted(const gp_Pnt2d& p, double d) const
    {
        if (isLine())
            return p.Translated(normal * d);
        return p.Translated(gp_Vec2d(p, center).Normalized() * (s * d));
    }
    double offsetRadius(double d) const { return radius - s * d; }
};

Piece makePiece(const Sketch& sketch, const ChainLink& link)
{
    const SketchEntity& e = *sketch.entity(link.entity);
    Piece p;
    p.id = e.id;
    p.kind = e.kind;
    p.centerId = e.c;
    p.reversed = link.reversed;
    p.start = sketch.pos(walkStart(e, link.reversed));
    p.end = sketch.pos(walkEnd(e, link.reversed));
    if (p.isLine()) {
        p.dir = gp_Vec2d(p.start, p.end).Normalized();
        p.normal = gp_Vec2d(-p.dir.Y(), p.dir.X());
    } else {
        p.center = sketch.pos(e.c);
        p.radius = e.kind == EntityKind::Arc ? p.center.Distance(sketch.pos(e.a)) : e.radius;
        p.s = link.reversed ? -1.0 : 1.0;
    }
    return p;
}

// Where two consecutive offset curves meet: their crossing nearest to where their
// shifted shared end would be, or that point itself when they still touch there.
gp_Pnt2d junction(const Piece& prev, const Piece& next, const gp_Pnt2d& shared, double d)
{
    const gp_Pnt2d qa = prev.shifted(shared, d);
    const gp_Pnt2d qb = next.shifted(shared, d);
    const gp_Pnt2d guess((qa.XY() + qb.XY()) / 2.0);
    if (qa.Distance(qb) < 1e-9)
        return guess; // tangent join: the offsets stay tangent

    std::vector<gp_Pnt2d> candidates;
    if (prev.isLine() && next.isLine()) {
        if (auto p = intersectLines(qa, prev.dir, qb, next.dir))
            candidates.push_back(*p);
    } else if (prev.isLine() != next.isLine()) {
        const Piece& line = prev.isLine() ? prev : next;
        const Piece& round = prev.isLine() ? next : prev;
        candidates = intersectLineCircle(line.shifted(shared, d), line.dir, round.center, round.offsetRadius(d));
    } else {
        candidates = intersectCircles(prev.center, prev.offsetRadius(d), next.center, next.offsetRadius(d));
    }

    gp_Pnt2d best = guess;
    double bestDistance = std::numeric_limits<double>::max();
    for (const gp_Pnt2d& c : candidates) {
        if (c.Distance(guess) < bestDistance) {
            bestDistance = c.Distance(guess);
            best = c;
        }
    }
    return best;
}

} // namespace

std::optional<Chain> chainFrom(const Sketch& sketch, EntityId seed)
{
    const SketchEntity* first = sketch.entity(seed);
    if (!first)
        return std::nullopt;
    if (first->kind == EntityKind::Circle)
        return Chain{{{seed, false}}, true};

    // The single other curve ending at `at`, if exactly two curves meet there.
    auto neighbour = [&](PointId at, EntityId from) -> std::optional<ChainLink> {
        std::optional<ChainLink> found;
        int count = 0;
        for (const SketchEntity& e : sketch.entities()) {
            if (e.kind == EntityKind::Circle || (e.a != at && e.b != at))
                continue;
            ++count;
            if (e.id != from)
                found = ChainLink{e.id, e.b == at}; // walking away from `at`
        }
        return count == 2 ? found : std::nullopt;
    };

    Chain chain;
    chain.links.push_back({seed, false});
    std::set<EntityId> used{seed};

    PointId at = first->b;
    EntityId last = seed;
    while (auto link = neighbour(at, last)) {
        if (link->entity == seed) {
            chain.closed = true;
            break;
        }
        if (!used.insert(link->entity).second)
            break;
        chain.links.push_back(*link);
        at = walkEnd(*sketch.entity(link->entity), link->reversed);
        last = link->entity;
    }
    if (chain.closed)
        return chain;

    // Then backwards from the seed's start, walking toward it.
    at = first->a;
    last = seed;
    while (auto link = neighbour(at, last)) {
        if (!used.insert(link->entity).second)
            break;
        chain.links.insert(chain.links.begin(), ChainLink{link->entity, !link->reversed});
        at = walkEnd(*sketch.entity(link->entity), link->reversed);
        last = link->entity;
    }
    return chain;
}

double chainSide(const Sketch& sketch, const Chain& chain, const gp_Pnt2d& p)
{
    double best = std::numeric_limits<double>::max();
    double side = 0.0;
    for (const ChainLink& link : chain.links) {
        const Piece piece = makePiece(sketch, link);
        const double distance = sketch.distanceTo(*sketch.entity(piece.id), p);
        if (distance >= best)
            continue;
        best = distance;
        side = piece.isLine() ? piece.dir.Crossed(gp_Vec2d(piece.start, p))
                              : piece.s * (piece.radius - piece.center.Distance(p));
    }
    return side < 0.0 ? -best : best;
}

std::vector<EntityId> offsetChain(Sketch& sketch, const Chain& chain, double distance)
{
    if (chain.links.empty() || std::abs(distance) < kEps)
        return {};
    std::vector<Piece> pieces;
    for (const ChainLink& link : chain.links) {
        if (!sketch.entity(link.entity))
            return {};
        pieces.push_back(makePiece(sketch, link));
    }
    for (const Piece& p : pieces) {
        if (!p.isLine() && p.offsetRadius(distance) <= kEps)
            return {}; // an arc shrinks to nothing
    }

    // A lone circle just changes radius around the same center.
    if (pieces.size() == 1 && pieces[0].kind == EntityKind::Circle)
        return {sketch.addCircle(pieces[0].centerId, pieces[0].offsetRadius(distance))};

    // Junction k sits where piece k starts (an open chain also gets one after its last piece).
    const std::size_t n = pieces.size();
    std::vector<gp_Pnt2d> joints;
    for (std::size_t k = 0; k < n; ++k) {
        if (k == 0 && !chain.closed)
            joints.push_back(pieces[0].shifted(pieces[0].start, distance));
        else
            joints.push_back(junction(pieces[(k + n - 1) % n], pieces[k], pieces[k].start, distance));
    }
    if (!chain.closed)
        joints.push_back(pieces[n - 1].shifted(pieces[n - 1].end, distance));

    // Too large an inward offset flips lines around; refuse rather than build a knot.
    auto jointAfter = [&](std::size_t k) { return joints[chain.closed ? (k + 1) % n : k + 1]; };
    for (std::size_t k = 0; k < n; ++k) {
        const gp_Vec2d span(joints[k], jointAfter(k));
        if (span.Magnitude() < 1e-7)
            return {};
        if (pieces[k].isLine() && span.Dot(pieces[k].dir) <= 0.0)
            return {};
    }

    std::vector<PointId> ids;
    for (const gp_Pnt2d& p : joints)
        ids.push_back(sketch.addPoint(p));
    auto idAfter = [&](std::size_t k) { return ids[chain.closed ? (k + 1) % n : k + 1]; };

    std::vector<EntityId> created;
    for (std::size_t k = 0; k < n; ++k) {
        const Piece& p = pieces[k];
        if (p.isLine()) {
            const EntityId line = sketch.addLine(ids[k], idAfter(k));
            SketchConstraint parallel;
            parallel.kind = ConstraintKind::Parallel;
            parallel.e1 = p.id;
            parallel.e2 = line;
            sketch.addConstraint(parallel);
            created.push_back(line);
        } else {
            // Same center point as the original, so they stay concentric.
            created.push_back(p.reversed ? sketch.addArc(p.centerId, idAfter(k), ids[k])
                                         : sketch.addArc(p.centerId, ids[k], idAfter(k)));
        }
    }
    return created;
}
