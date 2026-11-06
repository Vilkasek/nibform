// Sketch core: topology, hit testing, snapping, tessellation.

#include "Check.h"

#include "sketch/DrawList.h"
#include "sketch/Sketch.h"
#include "sketch/Snapper.h"

namespace {

void testSharedPoints()
{
    Sketch s;
    const PointId a = s.pointAt({0, 0});
    const PointId b = s.pointAt({10, 0});
    const PointId c = s.pointAt({10, 0}); // same location -> same point
    CHECK(b == c);
    s.addLine(a, b);
    s.addLine(b, s.pointAt({10, 5}));
    CHECK(s.points().size() == 3);
    CHECK(s.entities().size() == 2);
}

void testRemoveDropsOrphanPoints()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({10, 0});
    const PointId c = s.addPoint({10, 10});
    const EntityId l1 = s.addLine(a, b);
    s.addLine(b, c);
    s.removeEntities({l1});
    CHECK(s.entities().size() == 1);
    CHECK(s.point(a) == nullptr); // only l1 used it
    CHECK(s.point(b) != nullptr);
    CHECK(s.point(c) != nullptr);
}

void testHitTest()
{
    Sketch s;
    const EntityId line = s.addLine(s.addPoint({0, 0}), s.addPoint({10, 0}));
    const EntityId circle = s.addCircle(s.addPoint({50, 0}), 5.0);
    CHECK(s.hitTest({5, 0.4}, 0.5) == line);
    CHECK(!s.hitTest({5, 1.0}, 0.5));
    CHECK(s.hitTest({55.2, 0}, 0.5) == circle);
    CHECK(!s.hitTest({50, 0}, 0.5)); // circle center is not on the curve
}

void testCircumcenterAndArcSide()
{
    const auto c = circumcenter({1, 0}, {0, 1}, {-1, 0});
    CHECK(c && near(*c, gp_Pnt2d(0, 0)));
    CHECK(!circumcenter({0, 0}, {1, 1}, {2, 2})); // collinear

    // CCW from (1,0) to (-1,0) goes through (0,1), not (0,-1).
    CHECK(isOnCcwArc({0, 0}, {1, 0}, {-1, 0}, {0, 1}));
    CHECK(!isOnCcwArc({0, 0}, {1, 0}, {-1, 0}, {0, -1}));
}

void testArcHitRespectsSweep()
{
    Sketch s;
    const PointId c = s.addPoint({0, 0});
    const EntityId arc = s.addArc(c, s.addPoint({10, 0}), s.addPoint({-10, 0})); // upper half
    CHECK(s.hitTest({0, 10.1}, 0.5) == arc);
    CHECK(!s.hitTest({0, -10.1}, 0.5));
}

void testPlaneMapping()
{
    // Front plane: u = X, v = Z.
    Sketch front(gp_Ax3(gp::Origin(), -gp::DY(), gp::DX()));
    const gp_Pnt p = front.to3d({3, 4});
    CHECK(near(p.X(), 3) && near(p.Y(), 0) && near(p.Z(), 4));
    CHECK(near(front.to2d(p), gp_Pnt2d(3, 4)));
}

void testSnapPriorities()
{
    Sketch s;
    const PointId a = s.addPoint({10, 10});
    s.addLine(a, s.addPoint({30, 10}));
    Snapper snapper;

    auto r = snapper.snap(s, {10.3, 9.8}, 1.0, 0.5);
    CHECK(r.kind == SnapResult::Kind::Point && r.pointId == a);

    r = snapper.snap(s, {0.2, -0.3}, 1.0, 0.5);
    CHECK(r.kind == SnapResult::Kind::Origin && near(r.pos, gp_Pnt2d(0, 0)));

    r = snapper.snap(s, {20.4, 10.3}, 1.0, 0.5);
    CHECK(r.kind == SnapResult::Kind::Midpoint && near(r.pos, gp_Pnt2d(20, 10)));

    // Lines up vertically with (10,10) and horizontally with the anchor (50,40).
    r = snapper.snap(s, {10.3, 40.2}, 0.1, 0.5, {gp_Pnt2d(50, 40)});
    CHECK(r.kind == SnapResult::Kind::Aligned);
    CHECK(near(r.pos, gp_Pnt2d(10, 40)));
    CHECK(r.guides.size() == 2);

    // The anchor beats a closer unrelated point: (0.5, -50) is 0.5 from the origin's X
    // but 0.69 from the anchor's; the anchor must still win to give a true vertical.
    r = snapper.snap(s, {0.5, -50}, 0.1, 1.0, {gp_Pnt2d(-0.19, -25)});
    CHECK(r.kind == SnapResult::Kind::Aligned && near(r.pos.X(), -0.19));

    r = snapper.snap(s, {77, 66}, 1.0, 0.5);
    CHECK(!r.snapped() && near(r.pos, gp_Pnt2d(77, 66)));
}

void testTessellateClosesCircle()
{
    Sketch s;
    s.addCircle(s.addPoint({0, 0}), 2.0);
    const DrawList list = tessellate(s);
    CHECK(list.segments.size() >= 32);
    CHECK(near(list.segments.front().a, list.segments.back().b, 1e-9));
    for (const Segment2d& seg : list.segments)
        CHECK(near(seg.a.Distance(gp_Pnt2d(0, 0)), 2.0, 1e-9));
}

} // namespace

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"shared points", testSharedPoints},
        {"remove drops orphan points", testRemoveDropsOrphanPoints},
        {"hit test", testHitTest},
        {"circumcenter and arc side", testCircumcenterAndArcSide},
        {"arc hit respects sweep", testArcHitRespectsSweep},
        {"plane mapping", testPlaneMapping},
        {"snap priorities", testSnapPriorities},
        {"tessellate closes circle", testTessellateClosesCircle},
    };
    return runTests(tests);
}
