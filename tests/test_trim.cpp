// Trim: picking the piece between cuts and splitting curves.

#include "Check.h"

#include "sketch/Solver.h"
#include "sketch/Trim.h"

namespace {

SketchConstraint relation(ConstraintKind kind, PointId p1, PointId p2, EntityId e1)
{
    SketchConstraint c;
    c.kind = kind;
    c.p1 = p1;
    c.p2 = p2;
    c.e1 = e1;
    return c;
}

void testCrossTrimsToIntersection()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId h = s.addLine(a, b);
    const EntityId v = s.addLine(s.addPoint({4, -5}), s.addPoint({4, 5}));

    const auto piece = findTrimPiece(s, {8, 0.1}, 0.5);
    CHECK(piece && piece->entity == h && !piece->whole);
    CHECK(near(piece->from, 0.4) && near(piece->to, 1.0));
    CHECK(trim(s, *piece));

    CHECK(s.entities().size() == 2);
    const SketchEntity* line = s.entity(h);
    CHECK(line->a == a && near(s.pos(line->b), gp_Pnt2d(4, 0)));
    CHECK(!s.point(b)); // only the removed piece used it
    // The new end stays on the line that cut it.
    CHECK(s.constraints().size() == 1);
    CHECK(s.constraints()[0].kind == ConstraintKind::PointOnEntity && s.constraints()[0].e1 == v);
    CHECK(solve(s).ok);
}

void testMiddlePieceSplitsLine()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId h = s.addLine(a, b);
    s.addConstraint(relation(ConstraintKind::Horizontal, a, b, h));
    SketchConstraint length = relation(ConstraintKind::Distance, a, b, h);
    length.value = 10.0;
    s.addConstraint(length);
    s.addLine(s.addPoint({3, -5}), s.addPoint({3, 5}));
    s.addLine(s.addPoint({7, -5}), s.addPoint({7, 5}));

    const auto piece = findTrimPiece(s, {5, 0}, 0.5);
    CHECK(piece && piece->entity == h && near(piece->from, 0.3) && near(piece->to, 0.7));
    CHECK(trim(s, *piece));
    CHECK(s.entities().size() == 4);

    // Both leftovers stay horizontal; the length dimension no longer means anything.
    int horizontal = 0;
    for (const SketchConstraint& c : s.constraints()) {
        CHECK(c.kind != ConstraintKind::Distance);
        if (c.kind == ConstraintKind::Horizontal) {
            ++horizontal;
            const SketchEntity* e = s.entity(c.e1);
            CHECK(e && ((c.p1 == e->a && c.p2 == e->b) || (c.p1 == e->b && c.p2 == e->a)));
        }
    }
    CHECK(horizontal == 2);
    CHECK(near(s.pos(s.entity(h)->b), gp_Pnt2d(3, 0)));
    CHECK(solve(s).ok);
}

void testUncutCurvesGoEntirely()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId h = s.addLine(a, b);
    s.addConstraint(relation(ConstraintKind::Horizontal, a, b, h));
    const EntityId circle = s.addCircle(s.addPoint({0, 20}), 3.0);
    // Touched at one point only: a single cut can't make an arc, so the circle goes whole.
    s.addLine(s.addPoint({0, 23}), s.addPoint({0, 30}));

    auto piece = findTrimPiece(s, {5, 0}, 0.5);
    CHECK(piece && piece->whole);
    CHECK(trim(s, *piece));
    CHECK(!s.entity(h) && s.constraints().empty());

    piece = findTrimPiece(s, {3, 20}, 0.5);
    CHECK(piece && piece->entity == circle && piece->whole);
}

void testCircleBecomesArc()
{
    Sketch s;
    const PointId center = s.addPoint({0, 0});
    const EntityId circle = s.addCircle(center, 5.0);
    SketchConstraint dia = relation(ConstraintKind::Diameter, kNoId, kNoId, circle);
    dia.value = 10.0;
    const ConstraintId diaId = s.addConstraint(dia);
    s.addLine(s.addPoint({-10, 0}), s.addPoint({10, 0}));

    // Remove the lower half.
    const auto piece = findTrimPiece(s, {0, -5}, 0.5);
    CHECK(piece && !piece->whole && near(piece->from, M_PI) && near(piece->to, 0.0));
    CHECK(trim(s, *piece));
    const SketchEntity* arc = s.entity(circle);
    CHECK(arc && arc->kind == EntityKind::Arc && arc->c == center);
    CHECK(near(s.pos(arc->a), gp_Pnt2d(5, 0), 1e-9) && near(s.pos(arc->b), gp_Pnt2d(-5, 0), 1e-9));
    CHECK(isOnCcwArc(s.pos(center), s.pos(arc->a), s.pos(arc->b), {0, 5}));
    CHECK(s.constraint(diaId) != nullptr); // a diameter still means something on the arc
    CHECK(solve(s).ok);
}

void testArcSplitInTheMiddle()
{
    Sketch s;
    const PointId c = s.addPoint({0, 0});
    const EntityId arc = s.addArc(c, s.addPoint({10, 0}), s.addPoint({-10, 0})); // upper half
    s.addLine(s.addPoint({-2, -1}), s.addPoint({-2, 20}));
    s.addLine(s.addPoint({2, -1}), s.addPoint({2, 20}));

    const auto piece = findTrimPiece(s, {0, 10}, 0.5);
    CHECK(piece && piece->entity == arc && !piece->whole);
    CHECK(trim(s, *piece));
    int arcs = 0;
    for (const SketchEntity& e : s.entities()) {
        if (e.kind != EntityKind::Arc)
            continue;
        ++arcs;
        CHECK(e.c == c);
        CHECK(near(s.pos(e.a).Distance(s.pos(c)), 10.0, 1e-9));
        CHECK(near(s.pos(e.b).Distance(s.pos(c)), 10.0, 1e-9));
        CHECK(!isOnCcwArc(s.pos(c), s.pos(e.a), s.pos(e.b), {0, 10}));
    }
    CHECK(arcs == 2);
    CHECK(solve(s).ok);
}

void testTJunctionReusesThePoint()
{
    // A stem whose end is pinned onto the bar: trimming one side of the bar must
    // end the bar exactly at the stem's point, so they stay connected.
    Sketch s;
    const EntityId bar = s.addLine(s.addPoint({0, 0}), s.addPoint({10, 0}));
    const PointId foot = s.addPoint({6, 0});
    const EntityId stem = s.addLine(foot, s.addPoint({6, 8}));
    SketchConstraint on = relation(ConstraintKind::PointOnEntity, foot, kNoId, bar);
    s.addConstraint(on);

    const auto piece = findTrimPiece(s, {9, 0}, 0.5);
    CHECK(piece && piece->entity == bar);
    CHECK(trim(s, *piece));
    CHECK(s.entity(bar)->b == foot);
    CHECK(s.entity(stem)->a == foot);
    CHECK(s.constraints().empty()); // the point is now a shared end, not pinned
    CHECK(solve(s).ok);
}

} // namespace

int main()
{
    return runTests({
        {"cross trims to intersection", testCrossTrimsToIntersection},
        {"middle piece splits line", testMiddlePieceSplitsLine},
        {"uncut curves go entirely", testUncutCurvesGoEntirely},
        {"circle becomes arc", testCircleBecomesArc},
        {"arc split in the middle", testArcSplitInTheMiddle},
        {"T-junction reuses the point", testTJunctionReusesThePoint},
    });
}
