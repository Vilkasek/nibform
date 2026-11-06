#include "sketch/Sketch.h"

#include <ElSLib.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

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

} // namespace

Sketch::Sketch(const gp_Ax3& plane)
    : m_plane(plane)
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

void Sketch::removeEntities(const std::set<EntityId>& ids)
{
    std::erase_if(m_entities, [&](const SketchEntity& e) { return ids.count(e.id) > 0; });

    std::set<PointId> used;
    for (const SketchEntity& e : m_entities) {
        for (PointId p : {e.a, e.b, e.c}) {
            if (p != kNoId)
                used.insert(p);
        }
    }
    std::erase_if(m_points, [&](const SketchPoint& p) { return used.count(p.id) == 0; });
}

void Sketch::clear()
{
    m_points.clear();
    m_entities.clear();
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
    }
    return std::numeric_limits<double>::max();
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
