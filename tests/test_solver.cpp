// Constraints, the solver and the selection -> constraint rules.

#include "Check.h"

#include "sketch/ConstraintCommands.h"
#include "sketch/Solver.h"

namespace {

SketchConstraint make(ConstraintKind kind, PointId p1, PointId p2, EntityId e1 = kNoId, EntityId e2 = kNoId,
                      double value = 0.0)
{
    SketchConstraint c;
    c.kind = kind;
    c.p1 = p1;
    c.p2 = p2;
    c.e1 = e1;
    c.e2 = e2;
    c.value = value;
    return c;
}

SketchConstraint fixed(Sketch& s, PointId p)
{
    SketchConstraint c = make(ConstraintKind::Fixed, p, kNoId);
    c.target = s.pos(p);
    return c;
}

// Unit square-ish quad, corners p[0..3] counter-clockwise, lines l[0..3] = p[i] -> p[i+1].
struct Quad
{
    Sketch s;
    PointId p[4];
    EntityId l[4];
};

Quad quad(double w = 10.0, double h = 6.0, double skew = 0.0)
{
    Quad q;
    q.p[0] = q.s.addPoint({0, 0});
    q.p[1] = q.s.addPoint({w, skew});
    q.p[2] = q.s.addPoint({w + skew, h});
    q.p[3] = q.s.addPoint({skew, h + skew});
    for (int i = 0; i < 4; ++i)
        q.l[i] = q.s.addLine(q.p[i], q.p[(i + 1) % 4]);
    return q;
}

void testHorizontalMovesLeastGeometry()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 2});
    const EntityId l = s.addLine(a, b);
    s.addConstraint(make(ConstraintKind::Horizontal, a, b, l));
    const SolveResult r = solve(s);
    CHECK(r.ok);
    CHECK(near(s.pos(a).Y(), s.pos(b).Y()));
    // Minimum-norm correction splits the difference instead of moving one end.
    CHECK(near(s.pos(a).Y(), 1.0, 1e-6));
    CHECK(near(s.pos(a).X(), 0.0, 1e-6) && near(s.pos(b).X(), 10.0, 1e-6));
    CHECK(r.dof == 3);
}

void testFullyDefinedRectangle()
{
    Quad q = quad(10.0, 6.0, 0.7);
    Sketch& s = q.s;
    s.addConstraint(make(ConstraintKind::Horizontal, q.p[0], q.p[1], q.l[0]));
    s.addConstraint(make(ConstraintKind::Vertical, q.p[1], q.p[2], q.l[1]));
    s.addConstraint(make(ConstraintKind::Horizontal, q.p[2], q.p[3], q.l[2]));
    s.addConstraint(make(ConstraintKind::Vertical, q.p[3], q.p[0], q.l[3]));
    s.addConstraint(fixed(s, q.p[0]));

    SolveResult r = solve(s);
    CHECK(r.ok && r.dof == 2);
    CHECK(r.definedPoints.count(q.p[0]) == 1);
    CHECK(r.definedEntities.empty());

    s.addConstraint(make(ConstraintKind::Distance, q.p[0], q.p[1], q.l[0], kNoId, 40.0));
    s.addConstraint(make(ConstraintKind::Distance, q.p[1], q.p[2], q.l[1], kNoId, 25.0));
    r = solve(s);
    CHECK(r.ok && r.dof == 0);
    CHECK(r.definedEntities.size() == 4);
    CHECK(near(s.pos(q.p[2]), gp_Pnt2d(40, 25), 1e-7));
}

void testConflictLeavesSketchUntouched()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId l = s.addLine(a, b);
    s.addConstraint(fixed(s, a));
    s.addConstraint(fixed(s, b));
    s.addConstraint(make(ConstraintKind::Distance, a, b, l, kNoId, 20.0));
    CHECK(!solve(s).ok);
    CHECK(near(s.pos(b), gp_Pnt2d(10, 0)));
}

void testRedundancyShowsInDof()
{
    Quad q = quad();
    Sketch& s = q.s;
    s.addConstraint(make(ConstraintKind::Horizontal, q.p[0], q.p[1], q.l[0]));
    s.addConstraint(make(ConstraintKind::Horizontal, q.p[2], q.p[3], q.l[2]));
    const SolveResult before = solve(s);
    // Parallel on two lines that are already both horizontal adds an equation but no rank.
    s.addConstraint(make(ConstraintKind::Parallel, kNoId, kNoId, q.l[0], q.l[2]));
    const SolveResult after = solve(s);
    CHECK(after.ok);
    CHECK(after.equations == before.equations + 1);
    CHECK(after.dof == before.dof);
}

void testTangentLineCircle()
{
    Sketch s;
    const PointId a = s.addPoint({-10, 3});
    const PointId b = s.addPoint({10, 3});
    const EntityId l = s.addLine(a, b);
    const PointId c = s.addPoint({0, 0});
    const EntityId circle = s.addCircle(c, 5.0);
    s.addConstraint(fixed(s, a));
    s.addConstraint(fixed(s, b));
    s.addConstraint(fixed(s, c));

    Sketch probe = s;
    SketchSelection sel;
    sel.entities = {l, circle};
    CHECK(applyCommand(ConstraintCommand::Tangent, probe, sel, 1.0).applied);
    CHECK(solve(probe).ok);
    CHECK(near(probe.entity(circle)->radius, 3.0, 1e-7)); // only the radius may move
}

void testTangentCircles()
{
    Sketch s;
    const EntityId c1 = s.addCircle(s.addPoint({0, 0}), 4.0);
    const EntityId c2 = s.addCircle(s.addPoint({10, 0}), 4.0);
    SketchSelection sel;
    sel.entities = {c1, c2};
    CHECK(applyCommand(ConstraintCommand::Tangent, s, sel, 1.0).applied);
    CHECK(solve(s).ok);
    const SketchEntity* e1 = s.entity(c1);
    const SketchEntity* e2 = s.entity(c2);
    CHECK(near(s.pos(e1->c).Distance(s.pos(e2->c)), e1->radius + e2->radius, 1e-7));
}

void testAngleAtSharedCorner()
{
    Sketch s;
    const PointId o = s.addPoint({0, 0});
    const EntityId l1 = s.addLine(s.addPoint({10, 0}), o); // points *into* the corner
    const EntityId l2 = s.addLine(o, s.addPoint({7, 7}));
    s.addConstraint(fixed(s, o));
    s.addConstraint(make(ConstraintKind::Horizontal, o, s.entity(l1)->a, l1));

    SketchSelection sel;
    sel.entities = {l1, l2};
    const CommandResult cr = applyCommand(ConstraintCommand::Dimension, s, sel, 1.0);
    CHECK(cr.applied && cr.dimension != kNoId);
    SketchConstraint* dim = s.constraint(cr.dimension);
    CHECK(dim && dim->kind == ConstraintKind::Angle && dim->p1 == o);
    CHECK(near(dim->value, M_PI / 4.0, 1e-9)); // measured between rays leaving the corner

    dim->value = M_PI / 3.0;
    CHECK(solve(s).ok);
    const gp_Pnt2d tip = s.pos(s.entity(l2)->b);
    CHECK(near(std::atan2(tip.Y(), tip.X()), M_PI / 3.0, 1e-7));
}

void testArcKeepsItsEndsOnTheCircle()
{
    Sketch s;
    const PointId c = s.addPoint({0, 0});
    const PointId a = s.addPoint({10, 0});
    const PointId b = s.addPoint({0, 10});
    const EntityId arc = s.addArc(c, a, b);
    SketchSelection sel;
    sel.entities = {arc};
    const CommandResult cr = applyCommand(ConstraintCommand::Dimension, s, sel, 1.0);
    CHECK(cr.applied && s.constraint(cr.dimension)->kind == ConstraintKind::Radius);
    s.constraint(cr.dimension)->value = 4.0;
    CHECK(solve(s).ok);
    CHECK(near(s.pos(a).Distance(s.pos(c)), 4.0, 1e-7));
    CHECK(near(s.pos(b).Distance(s.pos(c)), 4.0, 1e-7));
    CHECK(near(s.entity(arc)->radius, 4.0, 1e-7));
}

void testDragFollowsOrSlides()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId l = s.addLine(a, b);
    s.addConstraint(make(ConstraintKind::Horizontal, a, b, l));

    // Free end follows the pointer exactly; the other end keeps the line horizontal.
    CHECK(solve(s, {{b, gp_Pnt2d(12, 5)}}).ok);
    CHECK(near(s.pos(b), gp_Pnt2d(12, 5), 1e-7));
    CHECK(near(s.pos(a).Y(), 5.0, 1e-7));

    // With the other end pinned, the point can only slide along the horizontal.
    s.addConstraint(fixed(s, a));
    CHECK(solve(s, {{b, gp_Pnt2d(15, 9)}}).ok);
    CHECK(near(s.pos(a), gp_Pnt2d(0, 5), 1e-7));
    CHECK(near(s.pos(b).Y(), 5.0, 1e-7));
}

void testMidpointAndPointOn()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const EntityId l = s.addLine(a, b);
    const PointId c = s.addPoint({3, 4});
    const EntityId other = s.addLine(c, s.addPoint({3, 10}));
    const EntityId circle = s.addCircle(s.addPoint({20, 0}), 5.0);

    SketchSelection sel;
    sel.points = {c};
    sel.entities = {l};
    CHECK(applyCommand(ConstraintCommand::Midpoint, s, sel, 1.0).applied);
    CHECK(solve(s).ok);
    CHECK(near(s.pos(c), gp_Pnt2d((s.pos(a).X() + s.pos(b).X()) / 2, (s.pos(a).Y() + s.pos(b).Y()) / 2), 1e-7));

    const PointId tip = s.entity(other)->b;
    sel.points = {tip};
    sel.entities = {circle};
    CHECK(applyCommand(ConstraintCommand::Coincident, s, sel, 1.0).applied);
    CHECK(solve(s).ok);
    const SketchEntity* ce = s.entity(circle);
    CHECK(near(s.pos(tip).Distance(s.pos(ce->c)), ce->radius, 1e-7));
}

void testCoincidentMergesPoints()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const PointId c = s.addPoint({10.5, 0.3});
    const PointId d = s.addPoint({10, 10});
    const EntityId l1 = s.addLine(a, b);
    const EntityId l2 = s.addLine(c, d);
    s.addConstraint(make(ConstraintKind::Vertical, c, d, l2));

    SketchSelection sel;
    sel.points = {b, c};
    CHECK(applyCommand(ConstraintCommand::Coincident, s, sel, 1.0).applied);
    CHECK(s.points().size() == 3);
    CHECK(s.entity(l2)->a == b);
    CHECK(s.constraints().size() == 1 && s.constraints()[0].p1 == b);
    CHECK(solve(s).ok);
    CHECK(near(s.pos(b).X(), s.pos(d).X(), 1e-9));

    // Merging a line's own endpoints would collapse it.
    sel.points = {a, b};
    CHECK(!applyCommand(ConstraintCommand::Coincident, s, sel, 1.0).applied);
    (void)l1;
}

void testRemovingGeometryDropsItsConstraints()
{
    Quad q = quad();
    Sketch& s = q.s;
    s.addConstraint(make(ConstraintKind::Horizontal, q.p[0], q.p[1], q.l[0]));
    s.addConstraint(make(ConstraintKind::Parallel, kNoId, kNoId, q.l[0], q.l[2]));
    s.addConstraint(make(ConstraintKind::Vertical, q.p[1], q.p[2], q.l[1]));
    s.removeEntities({q.l[0]});
    // Everything that referred to the line goes with it, even though its points stay.
    CHECK(s.constraints().size() == 1);
    CHECK(s.constraints()[0].kind == ConstraintKind::Vertical);
}

void testDuplicatesAreNotAdded()
{
    Quad q = quad();
    Sketch& s = q.s;
    const ConstraintId first = s.addConstraint(make(ConstraintKind::Horizontal, q.p[0], q.p[1], q.l[0]));
    // Same pair of points, reversed and without the line reference: same relation.
    CHECK(s.addConstraint(make(ConstraintKind::Horizontal, q.p[1], q.p[0])) == first);
    CHECK(s.constraints().size() == 1);
}

void testCommandsRejectWrongSelections()
{
    Quad q = quad();
    Sketch& s = q.s;
    const EntityId circle = s.addCircle(s.addPoint({50, 50}), 3.0);
    SketchSelection lineAndCircle;
    lineAndCircle.entities = {q.l[0], circle};
    SketchSelection oneLine;
    oneLine.entities = {q.l[0]};

    CHECK(!applyCommand(ConstraintCommand::Parallel, s, oneLine, 1.0).applied);
    CHECK(!applyCommand(ConstraintCommand::Parallel, s, lineAndCircle, 1.0).applied);
    CHECK(!applyCommand(ConstraintCommand::Equal, s, lineAndCircle, 1.0).applied);
    CHECK(!applyCommand(ConstraintCommand::Concentric, s, lineAndCircle, 1.0).applied);
    CHECK(!applyCommand(ConstraintCommand::Horizontal, s, lineAndCircle, 1.0).applied);
    CHECK(s.constraints().empty());
    CHECK(applyCommand(ConstraintCommand::Fix, s, lineAndCircle, 1.0).applied);
    CHECK(s.constraints().size() == 3); // both line ends and the circle center
}

void testDistanceBetweenParallelLines()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({10, 0});
    const PointId c = s.addPoint({2, 6}), d = s.addPoint({12, 6});
    const EntityId bottom = s.addLine(a, b);
    const EntityId top = s.addLine(c, d);
    s.addConstraint(make(ConstraintKind::Horizontal, a, b, bottom));
    s.addConstraint(make(ConstraintKind::Horizontal, c, d, top));
    s.addConstraint(fixed(s, a));

    SketchSelection sel;
    sel.entities = {bottom, top};
    const CommandResult r = applyCommand(ConstraintCommand::Dimension, s, sel, 1.0);
    CHECK(r.applied && s.constraint(r.dimension)->kind == ConstraintKind::LineDistance);
    CHECK(near(s.constraint(r.dimension)->value, 6.0));
    // Both already horizontal: no parallel constraint on top of that.
    for (const SketchConstraint& c2 : s.constraints())
        CHECK(c2.kind != ConstraintKind::Parallel);

    s.constraint(r.dimension)->value = 9.0;
    CHECK(solve(s).ok);
    CHECK(near(s.pos(c).Y(), 9.0, 1e-7) && near(s.pos(d).Y(), 9.0, 1e-7));
}

void testNearlyParallelLinesBecomeParallel()
{
    Sketch s;
    const EntityId l1 = s.addLine(s.addPoint({0, 0}), s.addPoint({10, 0}));
    const EntityId l2 = s.addLine(s.addPoint({0, 5}), s.addPoint({10, 5.2})); // ~1.1 degrees off
    SketchSelection sel;
    sel.entities = {l1, l2};
    const CommandResult r = applyCommand(ConstraintCommand::Dimension, s, sel, 1.0);
    CHECK(r.applied && s.constraint(r.dimension)->kind == ConstraintKind::LineDistance);
    bool parallel = false;
    for (const SketchConstraint& c : s.constraints())
        parallel |= c.kind == ConstraintKind::Parallel;
    CHECK(parallel);
    CHECK(solve(s).ok);

    // Clearly not parallel: still an angle.
    Sketch t;
    const EntityId m1 = t.addLine(t.addPoint({0, 0}), t.addPoint({10, 0}));
    const EntityId m2 = t.addLine(t.addPoint({0, 5}), t.addPoint({10, 8}));
    sel.entities = {m1, m2};
    const CommandResult angle = applyCommand(ConstraintCommand::Dimension, t, sel, 1.0);
    CHECK(angle.applied && t.constraint(angle.dimension)->kind == ConstraintKind::Angle);
}

void testPointToLineDistance()
{
    Sketch s;
    const EntityId line = s.addLine(s.addPoint({0, 0}), s.addPoint({10, 10}));
    const PointId p = s.addPoint({10, 0}); // below-right: on the line's right side
    s.addLine(p, s.addPoint({20, 0}));
    SketchSelection sel;
    sel.points = {p};
    sel.entities = {line};
    const CommandResult r = applyCommand(ConstraintCommand::Dimension, s, sel, 1.0);
    CHECK(r.applied && s.constraint(r.dimension)->kind == ConstraintKind::PointLineDistance);
    CHECK(near(s.constraint(r.dimension)->value, -std::sqrt(50.0), 1e-9));
    s.constraint(r.dimension)->value = -10.0;
    CHECK(solve(s).ok);
    CHECK(near(signedDistanceToLine(s, *s.entity(line), s.pos(p)), -10.0, 1e-7));
}

} // namespace

int main()
{
    return runTests({
        {"horizontal moves least geometry", testHorizontalMovesLeastGeometry},
        {"fully defined rectangle", testFullyDefinedRectangle},
        {"conflict leaves sketch untouched", testConflictLeavesSketchUntouched},
        {"redundancy shows in dof", testRedundancyShowsInDof},
        {"tangent line-circle", testTangentLineCircle},
        {"tangent circles", testTangentCircles},
        {"angle at shared corner", testAngleAtSharedCorner},
        {"arc keeps its ends on the circle", testArcKeepsItsEndsOnTheCircle},
        {"drag follows or slides", testDragFollowsOrSlides},
        {"midpoint and point-on", testMidpointAndPointOn},
        {"coincident merges points", testCoincidentMergesPoints},
        {"removing geometry drops its constraints", testRemovingGeometryDropsItsConstraints},
        {"duplicates are not added", testDuplicatesAreNotAdded},
        {"commands reject wrong selections", testCommandsRejectWrongSelections},
        {"distance between parallel lines", testDistanceBetweenParallelLines},
        {"nearly parallel lines become parallel", testNearlyParallelLinesBecomeParallel},
        {"point to line distance", testPointToLineDistance},
    });
}
