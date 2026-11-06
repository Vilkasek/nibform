#include "sketch/ConstraintCommands.h"

#include "sketch/Solver.h"

#include <cmath>
#include <vector>

namespace {

// Lines closer than this to parallel get a distance dimension instead of an angle.
constexpr double kParallelDegrees = 2.0;

bool isRound(const SketchEntity* e)
{
    return e && (e->kind == EntityKind::Circle || e->kind == EntityKind::Arc);
}

bool isLine(const SketchEntity* e)
{
    return e && e->kind == EntityKind::Line;
}

double radiusOf(const Sketch& s, const SketchEntity& e)
{
    return e.kind == EntityKind::Arc ? s.pos(e.c).Distance(s.pos(e.a)) : e.radius;
}

PointId sharedPoint(const SketchEntity& l1, const SketchEntity& l2)
{
    for (PointId p : {l1.a, l1.b}) {
        if (p == l2.a || p == l2.b)
            return p;
    }
    return kNoId;
}

// Which side of the line the rest of the sketch is on, so labels go the other way.
double outwardSide(const Sketch& s, const SketchEntity& line)
{
    gp_XY centroid(0.0, 0.0);
    for (const SketchPoint& p : s.points())
        centroid += p.pos.XY();
    if (!s.points().empty())
        centroid /= static_cast<double>(s.points().size());
    const gp_Vec2d d(s.pos(line.a), s.pos(line.b));
    const gp_Vec2d toCentroid(s.pos(line.a), gp_Pnt2d(centroid));
    return d.Crossed(toCentroid) > 0.0 ? -1.0 : 1.0;
}

} // namespace

CommandResult applyCommand(ConstraintCommand command, Sketch& sketch, const SketchSelection& selection,
                           double labelOffset)
{
    std::vector<const SketchEntity*> lines;
    std::vector<const SketchEntity*> rounds;
    for (EntityId id : selection.entities) {
        const SketchEntity* e = sketch.entity(id);
        if (isLine(e))
            lines.push_back(e);
        else if (isRound(e))
            rounds.push_back(e);
    }
    const std::vector<PointId> points(selection.points.begin(), selection.points.end());
    const std::size_t entityCount = lines.size() + rounds.size();

    std::vector<SketchConstraint> add;
    auto relation = [](ConstraintKind kind, EntityId e1, EntityId e2 = kNoId) {
        SketchConstraint c;
        c.kind = kind;
        c.e1 = e1;
        c.e2 = e2;
        return c;
    };

    CommandResult result;
    switch (command) {
    case ConstraintCommand::Horizontal:
    case ConstraintCommand::Vertical: {
        const ConstraintKind kind =
            command == ConstraintCommand::Horizontal ? ConstraintKind::Horizontal : ConstraintKind::Vertical;
        if (!lines.empty() && rounds.empty() && points.empty()) {
            for (const SketchEntity* l : lines) {
                SketchConstraint c = relation(kind, l->id);
                c.p1 = l->a;
                c.p2 = l->b;
                add.push_back(c);
            }
        } else if (entityCount == 0 && points.size() == 2) {
            SketchConstraint c = relation(kind, kNoId);
            c.p1 = points[0];
            c.p2 = points[1];
            add.push_back(c);
        }
        break;
    }
    case ConstraintCommand::Parallel:
    case ConstraintCommand::Perpendicular: {
        const bool parallel = command == ConstraintCommand::Parallel;
        const bool fits = rounds.empty() && points.empty()
                       && (parallel ? lines.size() >= 2 : lines.size() == 2);
        if (fits) {
            for (std::size_t i = 1; i < lines.size(); ++i)
                add.push_back(relation(parallel ? ConstraintKind::Parallel : ConstraintKind::Perpendicular,
                                       lines[0]->id, lines[i]->id));
        }
        break;
    }
    case ConstraintCommand::Equal: {
        if (!points.empty())
            break;
        const auto& group = lines.empty() ? rounds : lines;
        if (group.size() >= 2 && group.size() == entityCount) {
            for (std::size_t i = 1; i < group.size(); ++i)
                add.push_back(relation(ConstraintKind::Equal, group[0]->id, group[i]->id));
        }
        break;
    }
    case ConstraintCommand::Tangent: {
        if (!points.empty() || entityCount != 2 || rounds.empty())
            break;
        // Keep whichever side / touching mode the geometry is closest to right now.
        SketchConstraint c;
        if (lines.size() == 1) {
            const SketchEntity& l = *lines[0];
            const SketchEntity& r = *rounds[0];
            c = relation(ConstraintKind::Tangent, l.id, r.id);
            const gp_Vec2d d(sketch.pos(l.a), sketch.pos(l.b));
            c.value = d.Crossed(gp_Vec2d(sketch.pos(l.a), sketch.pos(r.c))) >= 0.0 ? 1.0 : -1.0;
        } else {
            const SketchEntity& a = *rounds[0];
            const SketchEntity& b = *rounds[1];
            c = relation(ConstraintKind::Tangent, a.id, b.id);
            const double d = sketch.pos(a.c).Distance(sketch.pos(b.c));
            const double ra = radiusOf(sketch, a), rb = radiusOf(sketch, b);
            c.value = std::abs(d - (ra + rb)) <= std::abs(d - std::abs(ra - rb)) ? 1.0 : -1.0;
        }
        add.push_back(c);
        break;
    }
    case ConstraintCommand::Concentric:
        if (points.empty() && lines.empty() && rounds.size() >= 2) {
            for (std::size_t i = 1; i < rounds.size(); ++i)
                add.push_back(relation(ConstraintKind::Concentric, rounds[0]->id, rounds[i]->id));
        }
        break;
    case ConstraintCommand::Coincident:
        if (entityCount == 0 && points.size() == 2) {
            result.applied = sketch.mergePoints(points[0], points[1]);
            return result;
        }
        if (entityCount == 1 && points.size() == 1) {
            const SketchEntity* e = lines.empty() ? rounds[0] : lines[0];
            if (points[0] == e->a || points[0] == e->b || points[0] == e->c)
                break;
            SketchConstraint c = relation(ConstraintKind::PointOnEntity, e->id);
            c.p1 = points[0];
            add.push_back(c);
        }
        break;
    case ConstraintCommand::Midpoint:
        if (lines.size() == 1 && rounds.empty() && points.size() == 1
            && points[0] != lines[0]->a && points[0] != lines[0]->b) {
            SketchConstraint c = relation(ConstraintKind::Midpoint, lines[0]->id);
            c.p1 = points[0];
            add.push_back(c);
        }
        break;
    case ConstraintCommand::Fix: {
        std::set<PointId> pins(points.begin(), points.end());
        for (const auto* group : {&lines, &rounds}) {
            for (const SketchEntity* e : *group) {
                for (PointId p : {e->a, e->b, e->c}) {
                    if (p != kNoId)
                        pins.insert(p);
                }
            }
        }
        for (PointId p : pins) {
            SketchConstraint c = relation(ConstraintKind::Fixed, kNoId);
            c.p1 = p;
            c.target = sketch.pos(p);
            add.push_back(c);
        }
        break;
    }
    case ConstraintCommand::Dimension: {
        SketchConstraint c;
        if (lines.size() == 1 && rounds.empty() && points.empty()) {
            const SketchEntity& l = *lines[0];
            c = relation(ConstraintKind::Distance, l.id);
            c.p1 = l.a;
            c.p2 = l.b;
            c.offset = outwardSide(sketch, l) * labelOffset;
        } else if (entityCount == 0 && points.size() == 2) {
            c = relation(ConstraintKind::Distance, kNoId);
            c.p1 = points[0];
            c.p2 = points[1];
            c.offset = labelOffset;
        } else if (rounds.size() == 1 && lines.empty() && points.empty()) {
            const bool circle = rounds[0]->kind == EntityKind::Circle;
            c = relation(circle ? ConstraintKind::Diameter : ConstraintKind::Radius, rounds[0]->id);
            c.offset = M_PI / 4.0; // leader direction
        } else if (lines.size() == 1 && rounds.empty() && points.size() == 1) {
            if (points[0] == lines[0]->a || points[0] == lines[0]->b)
                break;
            c = relation(ConstraintKind::PointLineDistance, lines[0]->id);
            c.p1 = points[0];
        } else if (lines.size() == 2 && rounds.empty() && points.empty()) {
            const gp_Vec2d d1(sketch.pos(lines[0]->a), sketch.pos(lines[0]->b));
            const gp_Vec2d d2(sketch.pos(lines[1]->a), sketch.pos(lines[1]->b));
            const double sine = std::abs(d1.Crossed(d2)) / (d1.Magnitude() * d2.Magnitude() + 1e-300);
            if (sine > std::sin(kParallelDegrees * M_PI / 180.0)) {
                c = relation(ConstraintKind::Angle, lines[0]->id, lines[1]->id);
                c.p1 = sharedPoint(*lines[0], *lines[1]);
                c.offset = 2.0 * labelOffset; // arc radius
            } else {
                // (Nearly) parallel lines: dimension the gap, like Fusion.
                c = relation(ConstraintKind::LineDistance, lines[0]->id, lines[1]->id);
                if (std::abs(measure(sketch, c)) < 1e-9)
                    break; // collinear: no gap to dimension
                // Make them truly parallel too, unless something already keeps them that way.
                const SketchConstraint parallel = relation(ConstraintKind::Parallel, lines[0]->id, lines[1]->id);
                Sketch probe = sketch;
                probe.addConstraint(parallel);
                if (solve(probe).dof < analyze(sketch).dof) {
                    sketch.addConstraint(parallel);
                    solve(sketch);
                }
            }
        } else {
            break;
        }
        c.value = measure(sketch, c);
        add.push_back(c);
        break;
    }
    }

    if (add.empty())
        return result;
    result.applied = true;
    for (const SketchConstraint& c : add) {
        const ConstraintId id = sketch.addConstraint(c);
        if (isDimension(c.kind))
            result.dimension = id;
    }
    return result;
}
