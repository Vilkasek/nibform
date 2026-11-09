#include "sketch/Sketch.h"

#include <ElSLib.hxx>
#include "sketch/Geometry.h"

#include <Geom2d_BSplineCurve.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

double ccwAngle(const gp_Pnt2d& center, const gp_Pnt2d& p)
{
    double a = std::atan2(p.Y() - center.Y(), p.X() - center.X());
    return a < 0.0 ? a + 2.0 * M_PI : a;
}

double segmentDistance(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& p)
{
    const double dx = b.X() - a.X();
    const double dy = b.Y() - a.Y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-24)
        return p.Distance(a);
    const double t = std::clamp(((p.X() - a.X()) * dx + (p.Y() - a.Y()) * dy) / len2, 0.0, 1.0);
    return p.Distance(gp_Pnt2d(a.X() + t * dx, a.Y() + t * dy));
}

double polylineDistance(const std::vector<gp_Pnt2d>& pts, const gp_Pnt2d& p)
{
    if (pts.size() == 1)
        return p.Distance(pts.front());
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < pts.size(); ++i)
        best = std::min(best, segmentDistance(pts[i - 1], pts[i], p));
    return best;
}

} // namespace

std::vector<PointId> SketchEntity::pointIds() const
{
    std::vector<PointId> out;
    for (PointId p : {a, b, c}) {
        if (p != kNoId)
            out.push_back(p);
    }
    for (PointId p : through) {
        if (std::find(out.begin(), out.end(), p) == out.end())
            out.push_back(p);
    }
    return out;
}

bool SketchEntity::usesPoint(PointId p) const
{
    return p != kNoId && (a == p || b == p || c == p || std::find(through.begin(), through.end(), p) != through.end());
}

gp_Pnt2d EllipseAxes::at(double t) const
{
    return center.Translated(u * std::cos(t) + v * std::sin(t));
}

EllipseAxes ellipseAxes(const Sketch& sketch, const SketchEntity& e)
{
    const gp_Pnt2d c = sketch.pos(e.c);
    return {c, gp_Vec2d(c, sketch.pos(e.a)), gp_Vec2d(c, sketch.pos(e.b))};
}

std::vector<gp_Pnt2d> sampleSpline(const std::vector<gp_Pnt2d>& points, bool closed, int perSpan)
{
    const Handle(Geom2d_BSplineCurve) curve = interpolateSpline(points, closed);
    if (curve.IsNull())
        return {};
    const double t0 = curve->FirstParameter(), t1 = curve->LastParameter();
    const int spans = curve->NbKnots() - 1;
    const int n = curve->Degree() == 1 ? spans : std::max(8, spans * perSpan);
    std::vector<gp_Pnt2d> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    if (curve->Degree() == 1) {
        for (int i = 1; i <= curve->NbPoles(); ++i)
            out.push_back(curve->Pole(i));
        if (curve->IsPeriodic())
            out.push_back(curve->Pole(1));
        return out;
    }
    for (int i = 0; i <= n; ++i)
        out.push_back(curve->Value(t0 + (t1 - t0) * i / n));
    return out;
}

Sketch::Sketch(const gp_Ax3& plane)
    : m_plane(plane)
{
}

Sketch::Sketch(const gp_Ax3& plane, Contents contents)
    : m_plane(plane)
    , m_points(std::move(contents.points))
    , m_entities(std::move(contents.entities))
    , m_constraints(std::move(contents.constraints))
    , m_nextId(contents.nextId)
    , m_image(std::move(contents.image))
{
}

gp_Pnt Sketch::to3d(const gp_Pnt2d& p) const
{
    return ElSLib::Value(p.X(), p.Y(), gp_Pln(m_plane));
}

gp_Pnt2d Sketch::to2d(const gp_Pnt& p) const
{
    double u = 0.0, v = 0.0;
    ElSLib::Parameters(gp_Pln(m_plane), p, u, v);
    return gp_Pnt2d(u, v);
}

PointId Sketch::addPoint(const gp_Pnt2d& pos)
{
    const PointId id = m_nextId++;
    m_points.push_back({id, pos});
    return id;
}

PointId Sketch::pointAt(const gp_Pnt2d& pos, double eps)
{
    if (auto existing = findPoint(pos, eps))
        return *existing;
    return addPoint(pos);
}

std::optional<PointId> Sketch::findPoint(const gp_Pnt2d& pos, double eps) const
{
    std::optional<PointId> best;
    double bestDist = eps;
    for (const SketchPoint& p : m_points) {
        const double d = p.pos.Distance(pos);
        if (d <= bestDist) {
            bestDist = d;
            best = p.id;
        }
    }
    return best;
}

EntityId Sketch::addLine(PointId start, PointId end)
{
    SketchEntity e;
    e.id = m_nextId++;
    e.kind = EntityKind::Line;
    e.a = start;
    e.b = end;
    m_entities.push_back(e);
    return e.id;
}

EntityId Sketch::addCircle(PointId center, double radius)
{
    SketchEntity e;
    e.id = m_nextId++;
    e.kind = EntityKind::Circle;
    e.c = center;
    e.radius = radius;
    m_entities.push_back(e);
    return e.id;
}

EntityId Sketch::addArc(PointId center, PointId start, PointId end)
{
    SketchEntity e;
    e.id = m_nextId++;
    e.kind = EntityKind::Arc;
    e.c = center;
    e.a = start;
    e.b = end;
    e.radius = pos(center).Distance(pos(start));
    m_entities.push_back(e);
    return e.id;
}

EntityId Sketch::addEllipse(PointId center, PointId axisA, PointId axisB)
{
    SketchEntity e;
    e.id = m_nextId++;
    e.kind = EntityKind::Ellipse;
    e.c = center;
    e.a = axisA;
    e.b = axisB;
    m_entities.push_back(e);
    return e.id;
}

EntityId Sketch::addSpline(std::vector<PointId> points, bool closed)
{
    SketchEntity e;
    e.id = m_nextId++;
    e.kind = EntityKind::Spline;
    e.closed = closed;
    if (!closed && !points.empty()) {
        e.a = points.front();
        e.b = points.back();
    }
    e.through = std::move(points);
    m_entities.push_back(e);
    return e.id;
}

bool Sketch::removeThroughPoint(EntityId spline, PointId point)
{
    SketchEntity* e = entity(spline);
    if (!e || e->kind != EntityKind::Spline || e->through.size() <= (e->closed ? 3u : 2u))
        return false;
    auto it = std::find(e->through.begin(), e->through.end(), point);
    if (it == e->through.end())
        return false;
    e->through.erase(it);
    if (!e->closed) {
        e->a = e->through.front();
        e->b = e->through.back();
    }
    removeEntities({}); // the point may be unused now
    return true;
}

const SketchConstraint* Sketch::findDuplicate(const SketchConstraint& c) const
{
    // Horizontal/vertical are about the two points, whether or not they came from a line.
    const bool pointsOnly = c.kind == ConstraintKind::Horizontal || c.kind == ConstraintKind::Vertical;
    for (const SketchConstraint& existing : m_constraints) {
        if (existing.kind != c.kind)
            continue;
        const bool same = pointsOnly
            ? (existing.p1 == c.p1 && existing.p2 == c.p2) || (existing.p1 == c.p2 && existing.p2 == c.p1)
            : sameRelation(existing, c);
        if (same)
            return &existing;
    }
    return nullptr;
}

ConstraintId Sketch::addConstraint(SketchConstraint c)
{
    if (const SketchConstraint* existing = findDuplicate(c))
        return existing->id;
    c.id = m_nextId++;
    m_constraints.push_back(c);
    return c.id;
}

void Sketch::removeConstraints(const std::set<ConstraintId>& ids)
{
    std::erase_if(m_constraints, [&](const SketchConstraint& c) { return ids.count(c.id) > 0; });
}

SketchConstraint* Sketch::constraint(ConstraintId id)
{
    auto it = std::find_if(m_constraints.begin(), m_constraints.end(),
                           [id](const SketchConstraint& c) { return c.id == id; });
    return it == m_constraints.end() ? nullptr : &*it;
}

const SketchConstraint* Sketch::constraint(ConstraintId id) const
{
    return const_cast<Sketch*>(this)->constraint(id);
}

void Sketch::removeEntities(const std::set<EntityId>& ids)
{
    std::erase_if(m_entities, [&](const SketchEntity& e) { return ids.count(e.id) > 0; });

    std::set<PointId> used;
    for (const SketchEntity& e : m_entities) {
        for (PointId p : e.pointIds())
            used.insert(p);
    }
    std::erase_if(m_points, [&](const SketchPoint& p) { return used.count(p.id) == 0; });

    std::erase_if(m_constraints, [&](const SketchConstraint& c) {
        for (EntityId e : {c.e1, c.e2}) {
            if (e != kNoId && !entity(e))
                return true;
        }
        for (PointId p : {c.p1, c.p2}) {
            if (p != kNoId && !point(p))
                return true;
        }
        return false;
    });
}

bool Sketch::mergePoints(PointId keep, PointId drop)
{
    if (keep == drop || !point(keep) || !point(drop))
        return false;
    auto remap = [&](PointId p) { return p == drop ? keep : p; };

    for (const SketchEntity& e : m_entities) {
        const PointId a = remap(e.a), b = remap(e.b), c = remap(e.c);
        const bool threePoints = e.kind == EntityKind::Arc || e.kind == EntityKind::Ellipse;
        bool collapses = (e.kind == EntityKind::Line && a == b) || (threePoints && (a == b || a == c || b == c));
        if (e.kind == EntityKind::Spline) {
            // Neighbouring through points can't become one (the ends of an open spline can).
            const std::size_t n = e.through.size();
            for (std::size_t i = 0; i + 1 < n; ++i) {
                if (remap(e.through[i]) == remap(e.through[i + 1]))
                    collapses = true;
            }
            if (e.closed && n > 1 && remap(e.through.front()) == remap(e.through.back()))
                collapses = true;
        }
        if (collapses)
            return false;
    }

    for (SketchEntity& e : m_entities) {
        e.a = remap(e.a);
        e.b = remap(e.b);
        e.c = remap(e.c);
        for (PointId& p : e.through)
            p = remap(p);
    }
    std::erase_if(m_points, [&](const SketchPoint& p) { return p.id == drop; });

    std::vector<SketchConstraint> old = std::move(m_constraints);
    m_constraints.clear();
    for (SketchConstraint c : old) {
        c.p1 = remap(c.p1);
        c.p2 = remap(c.p2);
        const bool degenerate = [&] {
            switch (c.kind) {
            case ConstraintKind::Horizontal:
            case ConstraintKind::Vertical:
            case ConstraintKind::Distance:
            case ConstraintKind::HorizontalDistance:
            case ConstraintKind::VerticalDistance:
            case ConstraintKind::Symmetric:
                return c.p1 == c.p2;
            case ConstraintKind::PointOnEntity:
            case ConstraintKind::PointLineDistance:
            case ConstraintKind::Midpoint: {
                const SketchEntity* e = entity(c.e1);
                return !e || c.p1 == e->a || c.p1 == e->b || c.p1 == e->c;
            }
            default:
                return false;
            }
        }();
        if (!degenerate && !findDuplicate(c))
            m_constraints.push_back(c);
    }
    return true;
}

void Sketch::clear()
{
    m_points.clear();
    m_entities.clear();
    m_constraints.clear();
    m_image.reset();
}

void Sketch::setPos(PointId id, const gp_Pnt2d& pos)
{
    for (SketchPoint& p : m_points) {
        if (p.id == id)
            p.pos = pos;
    }
}

void Sketch::setRadius(EntityId id, double radius)
{
    for (SketchEntity& e : m_entities) {
        if (e.id == id)
            e.radius = radius;
    }
}

void Sketch::setConstruction(EntityId id, bool construction)
{
    for (SketchEntity& e : m_entities) {
        if (e.id == id)
            e.construction = construction;
    }
}

std::vector<EntityId> Sketch::entitiesAt(PointId point) const
{
    std::vector<EntityId> out;
    for (const SketchEntity& e : m_entities) {
        if (e.usesPoint(point))
            out.push_back(e.id);
    }
    return out;
}

const SketchPoint* Sketch::point(PointId id) const
{
    auto it = std::find_if(m_points.begin(), m_points.end(), [id](const SketchPoint& p) { return p.id == id; });
    return it == m_points.end() ? nullptr : &*it;
}

const SketchEntity* Sketch::entity(EntityId id) const
{
    auto it = std::find_if(m_entities.begin(), m_entities.end(), [id](const SketchEntity& e) { return e.id == id; });
    return it == m_entities.end() ? nullptr : &*it;
}

SketchEntity* Sketch::entity(EntityId id)
{
    return const_cast<SketchEntity*>(std::as_const(*this).entity(id));
}

gp_Pnt2d Sketch::pos(PointId id) const
{
    const SketchPoint* p = point(id);
    return p ? p->pos : gp_Pnt2d();
}

double Sketch::distanceTo(const SketchEntity& e, const gp_Pnt2d& p) const
{
    switch (e.kind) {
    case EntityKind::Line:
        return segmentDistance(pos(e.a), pos(e.b), p);
    case EntityKind::Circle:
        return std::abs(p.Distance(pos(e.c)) - e.radius);
    case EntityKind::Arc: {
        const gp_Pnt2d center = pos(e.c);
        if (isOnCcwArc(center, pos(e.a), pos(e.b), p))
            return std::abs(p.Distance(center) - e.radius);
        return std::min(p.Distance(pos(e.a)), p.Distance(pos(e.b)));
    }
    case EntityKind::Ellipse:
    case EntityKind::Spline:
        return polylineDistance(polyline(e), p);
    }
    return std::numeric_limits<double>::max();
}

std::vector<gp_Pnt2d> Sketch::polyline(const SketchEntity& e) const
{
    // ~96 segments per full turn keeps curves smooth at any zoom we care about.
    constexpr int kTurn = 96;
    std::vector<gp_Pnt2d> out;
    auto arc = [&](const gp_Pnt2d& c, double r, double a0, double sweep) {
        const int n = std::max(4, static_cast<int>(std::ceil(std::abs(sweep) / (2.0 * M_PI) * kTurn)));
        for (int i = 0; i <= n; ++i) {
            const double t = a0 + sweep * i / n;
            out.emplace_back(c.X() + r * std::cos(t), c.Y() + r * std::sin(t));
        }
    };
    switch (e.kind) {
    case EntityKind::Line:
        out = {pos(e.a), pos(e.b)};
        break;
    case EntityKind::Circle:
        arc(pos(e.c), e.radius, 0.0, 2.0 * M_PI);
        break;
    case EntityKind::Arc: {
        const gp_Pnt2d c = pos(e.c);
        const double a0 = ccwAngle(c, pos(e.a));
        double sweep = ccwAngle(c, pos(e.b)) - a0;
        if (sweep <= 0.0)
            sweep += 2.0 * M_PI;
        arc(c, c.Distance(pos(e.a)), a0, sweep);
        break;
    }
    case EntityKind::Ellipse: {
        const EllipseAxes axes = ellipseAxes(*this, e);
        for (int i = 0; i <= kTurn; ++i)
            out.push_back(axes.at(2.0 * M_PI * i / kTurn));
        break;
    }
    case EntityKind::Spline: {
        std::vector<gp_Pnt2d> pts;
        for (PointId p : e.through)
            pts.push_back(pos(p));
        out = sampleSpline(pts, e.closed);
        break;
    }
    }
    return out;
}

std::optional<EntityId> Sketch::hitTest(const gp_Pnt2d& p, double tolerance) const
{
    std::optional<EntityId> best;
    double bestDist = tolerance;
    for (const SketchEntity& e : m_entities) {
        const double d = distanceTo(e, p);
        if (d <= bestDist) {
            bestDist = d;
            best = e.id;
        }
    }
    return best;
}

std::optional<gp_Pnt2d> circumcenter(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c)
{
    const double d = 2.0 * (a.X() * (b.Y() - c.Y()) + b.X() * (c.Y() - a.Y()) + c.X() * (a.Y() - b.Y()));
    const double scale = std::max({a.Distance(b), b.Distance(c), c.Distance(a)});
    if (scale < 1e-12 || std::abs(d) < 1e-9 * scale * scale)
        return std::nullopt;

    const double a2 = a.X() * a.X() + a.Y() * a.Y();
    const double b2 = b.X() * b.X() + b.Y() * b.Y();
    const double c2 = c.X() * c.X() + c.Y() * c.Y();
    return gp_Pnt2d((a2 * (b.Y() - c.Y()) + b2 * (c.Y() - a.Y()) + c2 * (a.Y() - b.Y())) / d,
                    (a2 * (c.X() - b.X()) + b2 * (a.X() - c.X()) + c2 * (b.X() - a.X())) / d);
}

bool isOnCcwArc(const gp_Pnt2d& center, const gp_Pnt2d& start, const gp_Pnt2d& end, const gp_Pnt2d& p)
{
    const double a0 = ccwAngle(center, start);
    double sweep = ccwAngle(center, end) - a0;
    if (sweep <= 0.0)
        sweep += 2.0 * M_PI;
    double t = ccwAngle(center, p) - a0;
    if (t < 0.0)
        t += 2.0 * M_PI;
    return t <= sweep;
}
