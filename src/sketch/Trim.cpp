#include "sketch/Trim.h"

#include "sketch/Geometry.h"

#include <gp_Vec2d.hxx>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr double kEps = 1e-6; // sketch units: cuts closer than this to an end or each other merge
constexpr double kTwoPi = 2.0 * M_PI;

// A place where the curve gets split: an existing sketch point, or a crossing with another curve.
struct Cut
{
    double t = 0.0;
    PointId point = kNoId;
    EntityId by = kNoId;
};

double normAngle(double a)
{
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}

double angleOf(const gp_Pnt2d& center, const gp_Pnt2d& p)
{
    return std::atan2(p.Y() - center.Y(), p.X() - center.X());
}

bool isRound(const SketchEntity& e)
{
    return e.kind != EntityKind::Line;
}

double radiusOf(const Sketch& s, const SketchEntity& e)
{
    return e.kind == EntityKind::Arc ? s.pos(e.c).Distance(s.pos(e.a)) : e.radius;
}

double arcStart(const Sketch& s, const SketchEntity& e)
{
    return angleOf(s.pos(e.c), s.pos(e.a));
}

// Parameter range: 1 for lines, the sweep for arcs, a full turn for circles.
double paramEnd(const Sketch& s, const SketchEntity& e)
{
    switch (e.kind) {
    case EntityKind::Line:
        return 1.0;
    case EntityKind::Arc: {
        const double sweep = normAngle(angleOf(s.pos(e.c), s.pos(e.b)) - arcStart(s, e));
        return sweep <= 0.0 ? kTwoPi : sweep;
    }
    case EntityKind::Circle:
    case EntityKind::Ellipse: // (only lines, circles and arcs are ever split)
    case EntityKind::Spline:
        break;
    }
    return kTwoPi;
}

// Arc length per unit of parameter, to compare parameters against kEps.
double paramScale(const Sketch& s, const SketchEntity& e)
{
    return e.kind == EntityKind::Line ? s.pos(e.a).Distance(s.pos(e.b)) : radiusOf(s, e);
}

double paramOf(const Sketch& s, const SketchEntity& e, const gp_Pnt2d& p)
{
    switch (e.kind) {
    case EntityKind::Line: {
        const gp_Vec2d d(s.pos(e.a), s.pos(e.b));
        const double len2 = d.SquareMagnitude();
        return len2 > 0.0 ? gp_Vec2d(s.pos(e.a), p).Dot(d) / len2 : 0.0;
    }
    case EntityKind::Circle:
        return normAngle(angleOf(s.pos(e.c), p));
    case EntityKind::Arc:
        return normAngle(angleOf(s.pos(e.c), p) - arcStart(s, e));
    case EntityKind::Ellipse:
    case EntityKind::Spline:
        break;
    }
    return 0.0;
}

gp_Pnt2d pointAt(const Sketch& s, const SketchEntity& e, double t)
{
    if (e.kind == EntityKind::Line) {
        const gp_Pnt2d a = s.pos(e.a);
        return a.Translated(gp_Vec2d(a, s.pos(e.b)) * t);
    }
    const double angle = (e.kind == EntityKind::Arc ? arcStart(s, e) : 0.0) + t;
    const gp_Pnt2d c = s.pos(e.c);
    const double r = radiusOf(s, e);
    return gp_Pnt2d(c.X() + r * std::cos(angle), c.Y() + r * std::sin(angle));
}

// Whether `p` (already on the curve's line or circle) is within its extent.
bool withinExtent(const Sketch& s, const SketchEntity& e, const gp_Pnt2d& p)
{
    // Ellipse and spline crossings come from their bounded curves already.
    if (e.kind == EntityKind::Circle || !e.isClassic())
        return true;
    const double t = paramOf(s, e, p);
    const double end = paramEnd(s, e);
    const double tol = kEps / std::max(paramScale(s, e), kEps);
    if (e.kind == EntityKind::Line)
        return t >= -tol && t <= end + tol;
    return t <= end + tol || t >= kTwoPi - tol;
}

// Crossings of the infinite lines / full circles carrying two curves.
std::vector<gp_Pnt2d> carrierIntersections(const Sketch& s, const SketchEntity& e, const SketchEntity& o)
{
    if (!e.isClassic() || !o.isClassic())
        return intersectCurves(s, e, o);
    auto lineOf = [&](const SketchEntity& l) { return std::pair(s.pos(l.a), gp_Vec2d(s.pos(l.a), s.pos(l.b))); };
    if (!isRound(e) && !isRound(o)) {
        const auto [p1, d1] = lineOf(e);
        const auto [p2, d2] = lineOf(o);
        if (auto p = intersectLines(p1, d1, p2, d2))
            return {*p};
        return {}; // parallel or overlapping: no single crossing
    }
    if (isRound(e) != isRound(o)) {
        const SketchEntity& round = isRound(e) ? e : o;
        const auto [p, d] = lineOf(isRound(e) ? o : e);
        return intersectLineCircle(p, d, s.pos(round.c), radiusOf(s, round));
    }
    return intersectCircles(s.pos(e.c), radiusOf(s, e), s.pos(o.c), radiusOf(s, o));
}

// Interior cuts of `e`, sorted by parameter. Its own ends are never cuts.
std::vector<Cut> cutsOn(const Sketch& s, const SketchEntity& e)
{
    std::vector<Cut> cuts;
    const double end = paramEnd(s, e);
    const double scale = std::max(paramScale(s, e), kEps);

    auto add = [&](const gp_Pnt2d& p, PointId point, EntityId by) {
        const double t = paramOf(s, e, p);
        if (e.kind != EntityKind::Circle && (t * scale < kEps || (end - t) * scale < kEps))
            return;
        for (Cut& c : cuts) {
            if (pointAt(s, e, c.t).Distance(p) < kEps) {
                if (point != kNoId)
                    c.point = point;
                return;
            }
        }
        cuts.push_back({t, point, by});
    };

    // Sketch points first, so a crossing at an existing point reuses it.
    for (const SketchPoint& p : s.points()) {
        if (!e.usesPoint(p.id) && s.distanceTo(e, p.pos) < kEps)
            add(p.pos, p.id, kNoId);
    }
    for (const SketchEntity& o : s.entities()) {
        if (o.id == e.id)
            continue;
        for (const gp_Pnt2d& p : carrierIntersections(s, e, o)) {
            if (withinExtent(s, e, p) && withinExtent(s, o, p))
                add(p, kNoId, o.id);
        }
    }
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.t < b.t; });
    return cuts;
}

} // namespace

std::optional<TrimPiece> findTrimPiece(const Sketch& sketch, const gp_Pnt2d& cursor, double tolerance)
{
    const auto id = sketch.hitTest(cursor, tolerance);
    if (!id)
        return std::nullopt;
    const SketchEntity& e = *sketch.entity(*id);
    if (!e.isClassic()) {
        // Ellipses and splines aren't split: trimming one removes it whole.
        TrimPiece piece;
        piece.entity = e.id;
        piece.whole = true;
        return piece;
    }
    const std::vector<Cut> cuts = cutsOn(sketch, e);
    const double end = paramEnd(sketch, e);

    TrimPiece piece;
    piece.entity = e.id;
    double t = paramOf(sketch, e, cursor);

    if (e.kind == EntityKind::Circle) {
        // A single cut can't turn a circle into an arc: the whole circle goes.
        if (cuts.size() < 2) {
            piece.whole = true;
            piece.to = kTwoPi;
            return piece;
        }
        piece.from = cuts.back().t; // wrap around by default
        piece.to = cuts.front().t;
        for (const Cut& c : cuts) {
            if (c.t < t)
                piece.from = c.t;
        }
        for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) {
            if (it->t > t)
                piece.to = it->t;
        }
        return piece;
    }

    // Past the end of an arc (picked near one of its ends): snap to the nearer end.
    if (e.kind == EntityKind::Arc && t > end)
        t = t - end < kTwoPi - t ? end : 0.0;
    t = std::clamp(t, 0.0, end);

    piece.whole = cuts.empty();
    piece.from = 0.0;
    piece.to = end;
    for (const Cut& c : cuts) {
        if (c.t < t)
            piece.from = c.t;
        else if (c.t > t && piece.to == end)
            piece.to = c.t;
    }
    return piece;
}

std::vector<Segment2d> trimPieceSegments(const Sketch& sketch, const TrimPiece& piece)
{
    std::vector<Segment2d> out;
    const SketchEntity* e = sketch.entity(piece.entity);
    if (!e)
        return out;
    if (!e->isClassic()) {
        const std::vector<gp_Pnt2d> pts = sketch.polyline(*e);
        for (std::size_t i = 1; i < pts.size(); ++i)
            out.push_back({pts[i - 1], pts[i]});
        return out;
    }
    if (e->kind == EntityKind::Line) {
        out.push_back({pointAt(sketch, *e, piece.from), pointAt(sketch, *e, piece.to)});
        return out;
    }
    const double start = (e->kind == EntityKind::Arc ? arcStart(sketch, *e) : 0.0) + piece.from;
    double sweep = piece.to - piece.from;
    if (e->kind == EntityKind::Circle)
        sweep = piece.whole ? kTwoPi : normAngle(sweep);
    appendArc(out, sketch.pos(e->c), radiusOf(sketch, *e), start, sweep);
    return out;
}

bool trim(Sketch& s, const TrimPiece& piece)
{
    const SketchEntity* found = s.entity(piece.entity);
    if (!found)
        return false;
    if (piece.whole) {
        s.removeEntities({piece.entity});
        return true;
    }

    const SketchEntity e = *found; // the curve as it was, for all parameter lookups
    const std::vector<Cut> cuts = cutsOn(s, e);
    const double end = paramEnd(s, e);

    // Where points pinned onto the curve sit along it, measured before anything moves.
    std::map<ConstraintId, double> pinnedAt;
    for (const SketchConstraint& c : s.constraints()) {
        if (c.kind == ConstraintKind::PointOnEntity && c.e1 == e.id)
            pinnedAt[c.id] = paramOf(s, e, s.pos(c.p1));
    }

    // The point for a cut: the sketch point already there, or a new one pinned onto
    // the curve that crossed this one, so the trimmed end stays on it.
    auto pointFor = [&](double t) {
        const gp_Pnt2d pos = pointAt(s, e, t);
        const auto cut = std::find_if(cuts.begin(), cuts.end(),
                                      [&](const Cut& c) { return pointAt(s, e, c.t).Distance(pos) < kEps; });
        if (cut != cuts.end() && cut->point != kNoId)
            return cut->point;
        if (auto existing = s.findPoint(pos, kEps))
            return *existing;
        const PointId id = s.addPoint(pos);
        const SketchEntity* by = cut != cuts.end() ? s.entity(cut->by) : nullptr;
        if (by && by->isClassic()) {
            SketchConstraint on;
            on.kind = ConstraintKind::PointOnEntity;
            on.p1 = id;
            on.e1 = cut->by;
            s.addConstraint(on);
        }
        return id;
    };
    auto endPoint = [&](double t) {
        if (e.kind != EntityKind::Circle && t <= 0.0)
            return e.a;
        if (e.kind != EntityKind::Circle && t >= end)
            return e.b;
        return pointFor(t);
    };

    // What is left, as parameter intervals (a circle leaves one arc running past its seam).
    struct Interval
    {
        double from, to;
        bool contains(double t) const { return t > from && t < to; }
    };
    std::vector<Interval> left;
    if (e.kind == EntityKind::Circle) {
        left.push_back({piece.to, piece.from <= piece.to ? piece.from + kTwoPi : piece.from});
    } else {
        if (piece.from > 0.0)
            left.push_back({0.0, piece.from});
        if (piece.to < end)
            left.push_back({piece.to, end});
    }

    // The first leftover keeps the curve's id, a second one becomes a new curve.
    const Interval& kept = left.front();
    const PointId keptA = endPoint(kept.from);
    const PointId keptB = endPoint(kept.to);
    EntityId second = kNoId;
    PointId secondA = kNoId, secondB = kNoId;
    if (left.size() > 1) {
        secondA = endPoint(left[1].from);
        secondB = endPoint(left[1].to);
        second = e.kind == EntityKind::Line ? s.addLine(secondA, secondB) : s.addArc(e.c, secondA, secondB);
    }
    SketchEntity& keptEntity = *s.entity(e.id);
    keptEntity.kind = e.kind == EntityKind::Line ? EntityKind::Line : EntityKind::Arc;
    keptEntity.a = keptA;
    keptEntity.b = keptB;
    if (keptEntity.kind == EntityKind::Arc)
        keptEntity.radius = s.pos(e.c).Distance(s.pos(keptA));

    // Re-home constraints on the trimmed curve.
    std::set<ConstraintId> drop;
    std::vector<SketchConstraint> copies;
    for (const SketchConstraint& c : s.constraints()) {
        if (c.e1 != e.id && c.e2 != e.id)
            continue;
        switch (c.kind) {
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical: {
            SketchConstraint& own = *s.constraint(c.id);
            own.p1 = keptA;
            own.p2 = keptB;
            if (second != kNoId) {
                SketchConstraint copy = c;
                copy.e1 = second;
                copy.p1 = secondA;
                copy.p2 = secondB;
                copies.push_back(copy);
            }
            break;
        }
        case ConstraintKind::Distance:
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance:
        case ConstraintKind::Midpoint:
            drop.insert(c.id); // the length they talk about is gone
            break;
        case ConstraintKind::Equal:
            if (e.kind == EntityKind::Line)
                drop.insert(c.id);
            break;
        case ConstraintKind::PointOnEntity: {
            double t = pinnedAt[c.id];
            if (e.kind == EntityKind::Circle && t < kept.from)
                t += kTwoPi;
            if (kept.contains(t))
                break;
            if (second != kNoId && left[1].contains(t))
                s.constraint(c.id)->e1 = second;
            else
                drop.insert(c.id);
            break;
        }
        case ConstraintKind::Angle:
            // Measured from a corner that may no longer be on this piece.
            if (c.p1 != kNoId && c.p1 != keptA && c.p1 != keptB)
                drop.insert(c.id);
            break;
        default:
            break;
        }
    }
    s.removeConstraints(drop);
    for (const SketchConstraint& c : copies)
        s.addConstraint(c);

    // Points only the removed piece used, and constraints on them, go too.
    s.removeEntities({});
    return true;
}
