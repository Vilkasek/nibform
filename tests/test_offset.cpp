// Offset: chains of connected curves and their shifted copies.

#include "Check.h"

#include "sketch/Offset.h"
#include "sketch/Solver.h"

namespace {

// Counter-clockwise rectangle (0,0)-(w,h); returns its first line.
EntityId rectangle(Sketch& s, double w, double h)
{
    const PointId p0 = s.addPoint({0, 0}), p1 = s.addPoint({w, 0});
    const PointId p2 = s.addPoint({w, h}), p3 = s.addPoint({0, h});
    const EntityId first = s.addLine(p0, p1);
    s.addLine(p1, p2);
    s.addLine(p2, p3);
    s.addLine(p3, p0);
    return first;
}

bool hasPoint(const Sketch& s, const std::vector<EntityId>& curves, const gp_Pnt2d& p)
{
    for (EntityId id : curves) {
        const SketchEntity* e = s.entity(id);
        if (near(s.pos(e->a), p, 1e-9) || near(s.pos(e->b), p, 1e-9))
            return true;
    }
    return false;
}

void testRectangleGrowsOutward()
{
    Sketch s;
    const EntityId first = rectangle(s, 10, 6);
    const auto chain = chainFrom(s, first);
    CHECK(chain && chain->closed && chain->links.size() == 4);

    // Inside a counter-clockwise loop is its left side.
    CHECK(chainSide(s, *chain, {5, 1}) > 0.0);
    CHECK(near(chainSide(s, *chain, {5, -2}), -2.0));

    const auto created = offsetChain(s, *chain, -2.0);
    CHECK(created.size() == 4);
    for (const gp_Pnt2d& corner : {gp_Pnt2d(-2, -2), gp_Pnt2d(12, -2), gp_Pnt2d(12, 8), gp_Pnt2d(-2, 8)})
        CHECK(hasPoint(s, created, corner));
    CHECK(s.points().size() == 8); // the copy is closed: four shared corners
    // Every new line is tied parallel to the line it came from.
    int parallels = 0;
    for (const SketchConstraint& c : s.constraints()) {
        if (c.kind != ConstraintKind::Parallel)
            continue;
        ++parallels;
        CHECK(c.e1 >= first && c.e1 < first + 8 && s.entity(c.e1) && s.entity(c.e2));
    }
    CHECK(parallels == 4);
    const SolveResult r = solve(s);
    CHECK(r.ok && r.dof == 8 + 4); // 8 for the free original, 8 - 4 parallels for the copy
}

void testInwardTooFarIsRefused()
{
    Sketch s;
    const auto chain = chainFrom(s, rectangle(s, 10, 6));
    const std::size_t before = s.entities().size();
    CHECK(offsetChain(s, *chain, 3.5).empty()); // half the height is 3
    CHECK(s.entities().size() == before);
    CHECK(offsetChain(s, *chain, 2.0).size() == 4);
}

void testOpenChainWithFillet()
{
    // Line east, quarter arc turning north, line north.
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({10, 0});
    const PointId c = s.addPoint({15, 5}), d = s.addPoint({15, 15});
    const PointId center = s.addPoint({10, 5});
    const EntityId first = s.addLine(a, b);
    const EntityId arc = s.addArc(center, b, c);
    s.addLine(d, c); // stored backwards: the chain has to walk it reversed

    const auto chain = chainFrom(s, arc);
    CHECK(chain && !chain->closed && chain->links.size() == 3);
    CHECK(chain->links.front().entity == first);
    CHECK(chain->links.back().reversed);

    const auto created = offsetChain(s, *chain, 1.0); // left = toward the arc's center
    CHECK(created.size() == 3);
    const SketchEntity* newArc = s.entity(created[1]);
    CHECK(newArc->kind == EntityKind::Arc && newArc->c == center);
    CHECK(near(newArc->radius, 4.0, 1e-9));
    CHECK(near(s.pos(newArc->a), gp_Pnt2d(10, 1), 1e-9) && near(s.pos(newArc->b), gp_Pnt2d(14, 5), 1e-9));
    CHECK(near(s.pos(s.entity(created[0])->a), gp_Pnt2d(0, 1), 1e-9));
    CHECK(hasPoint(s, {created[2]}, {14, 15}));
    CHECK(solve(s).ok);
}

void testCircleChangesRadius()
{
    Sketch s;
    const PointId center = s.addPoint({3, 3});
    const EntityId circle = s.addCircle(center, 5.0);
    const auto chain = chainFrom(s, circle);
    CHECK(chain && chain->closed);
    CHECK(chainSide(s, *chain, {3, 10}) < 0.0); // outside
    const auto created = offsetChain(s, *chain, -1.5);
    CHECK(created.size() == 1 && s.entity(created[0])->c == center);
    CHECK(near(s.entity(created[0])->radius, 6.5));
    CHECK(offsetChain(s, *chain, 5.0).empty());
}

void testChainStopsAtBranches()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({10, 0}), c = s.addPoint({20, 0});
    const EntityId left = s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(b, s.addPoint({10, 10})); // a third curve at b: a branch
    const auto chain = chainFrom(s, left);
    CHECK(chain && chain->links.size() == 1 && !chain->closed);
}

} // namespace

int main()
{
    return runTests({
        {"rectangle grows outward", testRectangleGrowsOutward},
        {"inward too far is refused", testInwardTooFarIsRefused},
        {"open chain with fillet", testOpenChainWithFillet},
        {"circle changes radius", testCircleChangesRadius},
        {"chain stops at branches", testChainStopsAtBranches},
    });
}
