#include "sketch/SketchOps.h"

#include "sketch/Solver.h"

#include <gp_Trsf2d.hxx>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

constexpr double kEps = 1e-6;           // sketch units
constexpr double kMinLeftover = 1e-3;   // what a cut-back line keeps at least

SketchConstraint relation(ConstraintKind kind, EntityId e1 = kNoId, EntityId e2 = kNoId)
{
    SketchConstraint c;
    c.kind = kind;
    c.e1 = e1;
    c.e2 = e2;
    return c;
}

// +1 when `p` is left of the line a -> b, else -1 (the side Tangent keeps).
double sideOf(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& p)
{
    return gp_Vec2d(a, b).Crossed(gp_Vec2d(a, p)) >= 0.0 ? 1.0 : -1.0;
}

void addTangent(Sketch& s, EntityId line, EntityId round)
{
    const SketchEntity& l = *s.entity(line);
    SketchConstraint c = relation(ConstraintKind::Tangent, line, round);
    c.value = sideOf(s.pos(l.a), s.pos(l.b), s.pos(s.entity(round)->c));
    s.addConstraint(c);
}

// The end of `line` other than `corner`.
PointId farEnd(const SketchEntity& line, PointId corner)
{
    return line.a == corner ? line.b : line.a;
}

struct Corner
{
    EntityId l1 = kNoId, l2 = kNoId;
    gp_Pnt2d p;      // the corner
    gp_Vec2d u1, u2; // unit, from the corner along each line
    double len1 = 0.0, len2 = 0.0;
    double half = 0.0; // half the angle between the lines
};

std::optional<Corner> corner(const Sketch& s, PointId point)
{
    const auto lines = cornerLines(s, point);
    if (!lines)
        return std::nullopt;
    Corner c;
    c.l1 = lines->first;
    c.l2 = lines->second;
    c.p = s.pos(point);
    const gp_Vec2d d1(c.p, s.pos(farEnd(*s.entity(c.l1), point)));
    const gp_Vec2d d2(c.p, s.pos(farEnd(*s.entity(c.l2), point)));
    c.len1 = d1.Magnitude();
    c.len2 = d2.Magnitude();
    if (c.len1 < kEps || c.len2 < kEps)
        return std::nullopt;
    c.u1 = d1 / c.len1;
    c.u2 = d2 / c.len2;
    c.half = std::acos(std::clamp(c.u1.Dot(c.u2), -1.0, 1.0)) / 2.0;
    return c;
}

// Moves the lines' shared end `corner` to `t1` on the first and `t2` on the second, and
// sorts out the constraints that talked about the corner.
void detachCorner(Sketch& s, PointId cornerId, const Corner& c, PointId t1, PointId t2)
{
    for (auto [line, to] : {std::pair(c.l1, t1), std::pair(c.l2, t2)}) {
        SketchEntity& e = *s.entity(line);
        (e.a == cornerId ? e.a : e.b) = to;
    }

    std::set<ConstraintId> drop;
    std::vector<EntityId> partners; // lines that were equal to the cut-back ones
    for (const SketchConstraint& k : s.constraints()) {
        const bool onLine = k.e1 == c.l1 || k.e1 == c.l2;
        const bool atCorner = k.p1 == cornerId || k.p2 == cornerId;
        SketchConstraint& own = *s.constraint(k.id);
        switch (k.kind) {
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical:
            // The line's own relation follows its new end; a pair with the corner goes.
            if (onLine && k.e2 == kNoId) {
                const PointId to = k.e1 == c.l1 ? t1 : t2;
                if (own.p1 == cornerId)
                    own.p1 = to;
                if (own.p2 == cornerId)
                    own.p2 = to;
            } else if (atCorner) {
                drop.insert(k.id);
            }
            break;
        case ConstraintKind::Angle:
            if (k.p1 == cornerId) {
                // Same lines, same directions: measured between them as they run now.
                own.p1 = kNoId;
                own.value = measure(s, own);
            }
            break;
        case ConstraintKind::Distance:
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance:
        case ConstraintKind::Midpoint:
            // The length of the cut-back line isn't what was dimensioned.
            if (onLine || atCorner)
                drop.insert(k.id);
            break;
        case ConstraintKind::Equal:
            // Equal lengths: one of them is shorter now. What was equal through it stays
            // equal among itself (a polygon's other sides).
            if (onLine || k.e2 == c.l1 || k.e2 == c.l2) {
                drop.insert(k.id);
                for (EntityId e : {k.e1, k.e2}) {
                    if (e != c.l1 && e != c.l2 && std::find(partners.begin(), partners.end(), e) == partners.end())
                        partners.push_back(e);
                }
            }
            break;
        default:
            if (atCorner)
                drop.insert(k.id);
            break;
        }
    }
    s.removeConstraints(drop);
    for (std::size_t i = 1; i < partners.size(); ++i)
        s.addConstraint(relation(ConstraintKind::Equal, partners[0], partners[i]));
    s.removeEntities({}); // the corner point itself
}

// Copies `entities` through `map` (a point transform). Points the map leaves in place are
// shared with the originals; `mirrored` swaps arc ends to keep arcs counter-clockwise.
// Fills `pointMap` (original -> copy) and returns original -> copy entity ids.
std::map<EntityId, EntityId> copyEntities(Sketch& s, const std::set<EntityId>& entities,
                                          const std::function<gp_Pnt2d(const gp_Pnt2d&)>& map, bool mirrored,
                                          std::map<PointId, PointId>& pointMap)
{
    std::map<EntityId, EntityId> out;
    std::vector<SketchEntity> originals;
    for (EntityId id : entities) {
        if (const SketchEntity* e = s.entity(id))
            originals.push_back(*e);
    }
    auto copyPoint = [&](PointId p) {
        if (p == kNoId)
            return kNoId;
        if (auto it = pointMap.find(p); it != pointMap.end())
            return it->second;
        const gp_Pnt2d from = s.pos(p);
        const gp_Pnt2d to = map(from);
        const PointId q = to.Distance(from) < kEps ? p : s.pointAt(to, kEps);
        pointMap[p] = q;
        return q;
    };
    for (const SketchEntity& e : originals) {
        EntityId copy = kNoId;
        switch (e.kind) {
        case EntityKind::Line:
            copy = s.addLine(copyPoint(e.a), copyPoint(e.b));
            break;
        case EntityKind::Circle:
            copy = s.addCircle(copyPoint(e.c), e.radius);
            break;
        case EntityKind::Arc: {
            const PointId c = copyPoint(e.c), a = copyPoint(e.a), b = copyPoint(e.b);
            copy = mirrored ? s.addArc(c, b, a) : s.addArc(c, a, b);
            break;
        }
        case EntityKind::Ellipse:
            copy = s.addEllipse(copyPoint(e.c), copyPoint(e.a), copyPoint(e.b));
            break;
        case EntityKind::Spline: {
            std::vector<PointId> through;
            for (PointId p : e.through)
                through.push_back(copyPoint(p));
            copy = s.addSpline(through, e.closed);
            break;
        }
        }
        s.setConstruction(copy, e.construction);
        out[e.id] = copy;
    }
    return out;
}

// Pattern copies keep the relations among the copied curves; `turned` drops the ones a
// rotation breaks. Dimensions and pins stay with the original only.
void copyRelations(Sketch& s, const std::map<EntityId, EntityId>& entities, const std::map<PointId, PointId>& points,
                   bool turned)
{
    auto mapEntity = [&](EntityId e) -> std::optional<EntityId> {
        if (e == kNoId)
            return kNoId;
        auto it = entities.find(e);
        return it == entities.end() ? std::nullopt : std::optional(it->second);
    };
    auto mapPoint = [&](PointId p) -> std::optional<PointId> {
        if (p == kNoId)
            return kNoId;
        auto it = points.find(p);
        return it == points.end() ? std::nullopt : std::optional(it->second);
    };
    const std::vector<SketchConstraint> existing = s.constraints();
    for (SketchConstraint c : existing) {
        if (isDimension(c.kind) || c.kind == ConstraintKind::Fixed)
            continue;
        if (turned && (c.kind == ConstraintKind::Horizontal || c.kind == ConstraintKind::Vertical))
            continue;
        const auto e1 = mapEntity(c.e1), e2 = mapEntity(c.e2);
        const auto p1 = mapPoint(c.p1), p2 = mapPoint(c.p2);
        if (!e1 || !e2 || !p1 || !p2)
            continue; // reaches outside what was copied
        if (*e1 == c.e1 && *e2 == c.e2 && *p1 == c.p1 && *p2 == c.p2)
            continue; // nothing of it moved
        c.e1 = *e1;
        c.e2 = *e2;
        c.p1 = *p1;
        c.p2 = *p2;
        s.addConstraint(c);
    }
}

void equalRounds(Sketch& s, const std::map<EntityId, EntityId>& copies)
{
    for (const auto& [original, copy] : copies) {
        const SketchEntity* e = s.entity(original);
        if (e && (e->kind == EntityKind::Circle || e->kind == EntityKind::Arc))
            s.addConstraint(relation(ConstraintKind::Equal, original, copy));
    }
}

std::vector<EntityId> values(const std::map<EntityId, EntityId>& m)
{
    std::vector<EntityId> out;
    for (const auto& [from, to] : m)
        out.push_back(to);
    return out;
}

std::vector<EntityId> pattern(Sketch& s, const std::set<EntityId>& entities, int count, bool turned,
                              const std::function<gp_Trsf2d(int)>& transform)
{
    std::vector<EntityId> out;
    for (int k = 1; k < count; ++k) {
        const gp_Trsf2d t = transform(k);
        std::map<PointId, PointId> points;
        const auto copies = copyEntities(
            s, entities, [&](const gp_Pnt2d& p) { return p.Transformed(t); }, false, points);
        copyRelations(s, copies, points, turned);
        equalRounds(s, copies);
        const std::vector<EntityId> made = values(copies);
        out.insert(out.end(), made.begin(), made.end());
    }
    return out;
}

} // namespace

std::vector<EntityId> addPolygon(Sketch& s, PointId center, PointId first, int sides)
{
    std::vector<EntityId> out;
    const gp_Pnt2d c = s.pos(center);
    const gp_Vec2d r0(c, s.pos(first));
    const double radius = r0.Magnitude();
    if (sides < 3 || radius < kEps || center == first)
        return out;

    std::vector<PointId> corners{first};
    for (int k = 1; k < sides; ++k)
        corners.push_back(s.pointAt(c.Translated(r0.Rotated(2.0 * M_PI * k / sides))));
    const EntityId circle = s.addCircle(center, radius);
    s.setConstruction(circle, true);
    for (int k = 0; k < sides; ++k)
        out.push_back(s.addLine(corners[k], corners[(k + 1) % sides]));

    for (PointId p : corners) {
        SketchConstraint on = relation(ConstraintKind::PointOnEntity, circle);
        on.p1 = p;
        s.addConstraint(on);
    }
    for (int k = 1; k < sides; ++k)
        s.addConstraint(relation(ConstraintKind::Equal, out[0], out[k]));
    return out;
}

EntityId addSlot(Sketch& s, PointId center1, PointId center2, double width)
{
    const gp_Pnt2d c1 = s.pos(center1), c2 = s.pos(center2);
    const gp_Vec2d along(c1, c2);
    if (along.Magnitude() < kEps || width < kEps || center1 == center2)
        return kNoId;
    const gp_Vec2d d = along.Normalized();
    const gp_Vec2d n = gp_Vec2d(-d.Y(), d.X()) * (width / 2.0);

    const PointId p1 = s.pointAt(c1.Translated(n));
    const PointId p2 = s.pointAt(c2.Translated(n));
    const PointId p3 = s.pointAt(c2.Translated(-n));
    const PointId p4 = s.pointAt(c1.Translated(-n));
    const EntityId centerline = s.addLine(center1, center2);
    s.setConstruction(centerline, true);
    const EntityId top = s.addLine(p1, p2);
    const EntityId bottom = s.addLine(p3, p4);
    const EntityId end2 = s.addArc(center2, p3, p2); // counter-clockwise round the far end
    const EntityId end1 = s.addArc(center1, p1, p4);
    for (EntityId line : {top, bottom}) {
        addTangent(s, line, end1);
        addTangent(s, line, end2);
    }
    s.addConstraint(relation(ConstraintKind::Equal, end1, end2));
    return centerline;
}

std::optional<std::pair<EntityId, EntityId>> cornerLines(const Sketch& s, PointId point)
{
    const std::vector<EntityId> at = s.entitiesAt(point);
    if (at.size() != 2)
        return std::nullopt;
    const SketchEntity* a = s.entity(at[0]);
    const SketchEntity* b = s.entity(at[1]);
    if (!a || !b || a->kind != EntityKind::Line || b->kind != EntityKind::Line)
        return std::nullopt;
    if ((a->a != point && a->b != point) || (b->a != point && b->b != point))
        return std::nullopt;
    const gp_Vec2d d1(s.pos(point), s.pos(farEnd(*a, point)));
    const gp_Vec2d d2(s.pos(point), s.pos(farEnd(*b, point)));
    const double m = d1.Magnitude() * d2.Magnitude();
    if (m < kEps * kEps || std::abs(d1.Crossed(d2)) / m < 1e-6)
        return std::nullopt; // in line: no corner to round
    return std::pair(a->id, b->id);
}

double maxFilletRadius(const Sketch& s, PointId point)
{
    const auto c = corner(s, point);
    if (!c)
        return 0.0;
    return std::max(0.0, std::min(c->len1, c->len2) - kMinLeftover) * std::tan(c->half);
}

double maxChamferDistance(const Sketch& s, PointId point)
{
    const auto c = corner(s, point);
    return c ? std::max(0.0, std::min(c->len1, c->len2) - kMinLeftover) : 0.0;
}

bool filletCorner(Sketch& s, PointId point, double radius, EntityId* arcOut)
{
    const auto c = corner(s, point);
    if (!c || radius <= kEps || radius > maxFilletRadius(s, point) + 1e-12)
        return false;
    const double back = radius / std::tan(c->half);
    const gp_Vec2d bisector = (c->u1 + c->u2).Normalized();
    const gp_Pnt2d center = c->p.Translated(bisector * (radius / std::sin(c->half)));
    const gp_Pnt2d a = c->p.Translated(c->u1 * back);
    const gp_Pnt2d b = c->p.Translated(c->u2 * back);

    const PointId t1 = s.addPoint(a);
    const PointId t2 = s.addPoint(b);
    detachCorner(s, point, *c, t1, t2); // (drops unused points: the center comes after)
    const PointId cp = s.addPoint(center);

    // The arc bulges toward where the corner was.
    const gp_Pnt2d bulge = center.Translated(gp_Vec2d(center, c->p).Normalized() * radius);
    const bool ccw = isOnCcwArc(center, a, b, bulge);
    const EntityId arc = ccw ? s.addArc(cp, t1, t2) : s.addArc(cp, t2, t1);
    addTangent(s, c->l1, arc);
    addTangent(s, c->l2, arc);
    SketchConstraint dimension = relation(ConstraintKind::Radius, arc);
    dimension.value = radius;
    dimension.offset = M_PI / 4.0;
    s.addConstraint(dimension);
    if (arcOut)
        *arcOut = arc;
    return true;
}

bool chamferCorner(Sketch& s, PointId point, double distance, EntityId* bevelOut)
{
    const auto c = corner(s, point);
    if (!c || distance <= kEps || distance > maxChamferDistance(s, point) + 1e-12)
        return false;
    const PointId t1 = s.addPoint(c->p.Translated(c->u1 * distance));
    const PointId t2 = s.addPoint(c->p.Translated(c->u2 * distance));
    detachCorner(s, point, *c, t1, t2);
    const EntityId bevel = s.addLine(t1, t2);
    if (bevelOut)
        *bevelOut = bevel;
    return true;
}

std::vector<EntityId> mirrorEntities(Sketch& s, const std::set<EntityId>& entities, EntityId axis)
{
    const SketchEntity* line = s.entity(axis);
    if (!line || line->kind != EntityKind::Line)
        return {};
    const gp_Pnt2d a = s.pos(line->a), b = s.pos(line->b);
    if (a.Distance(b) < kEps)
        return {};
    gp_Trsf2d reflect;
    reflect.SetMirror(gp_Ax2d(a, gp_Dir2d(gp_Vec2d(a, b))));

    std::set<EntityId> picked = entities;
    picked.erase(axis);
    std::map<PointId, PointId> points;
    const auto copies = copyEntities(
        s, picked, [&](const gp_Pnt2d& p) { return p.Transformed(reflect); }, true, points);

    const SketchEntity axisLine = *s.entity(axis);
    for (const auto& [original, copy] : points) {
        if (original == copy) {
            // On the axis: shared, and kept there.
            if (!axisLine.usesPoint(original)) {
                SketchConstraint on = relation(ConstraintKind::PointOnEntity, axis);
                on.p1 = original;
                s.addConstraint(on);
            }
            continue;
        }
        SketchConstraint sym = relation(ConstraintKind::Symmetric, axis);
        sym.p1 = original;
        sym.p2 = copy;
        s.addConstraint(sym);
    }
    for (const auto& [original, copy] : copies) {
        if (s.entity(original)->kind == EntityKind::Circle)
            s.addConstraint(relation(ConstraintKind::Equal, original, copy));
    }
    return values(copies);
}

std::vector<EntityId> linearPattern(Sketch& s, const std::set<EntityId>& entities, const gp_Vec2d& step, int count)
{
    if (step.Magnitude() < kEps)
        return {};
    return pattern(s, entities, count, false, [&](int k) {
        gp_Trsf2d t;
        t.SetTranslation(step * k);
        return t;
    });
}

std::vector<EntityId> circularPattern(Sketch& s, const std::set<EntityId>& entities, const gp_Pnt2d& center,
                                      int count)
{
    return pattern(s, entities, count, true, [&](int k) {
        gp_Trsf2d t;
        t.SetRotation(center, 2.0 * M_PI * k / count);
        return t;
    });
}
