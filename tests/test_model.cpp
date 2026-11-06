// Regions found in sketches, extrusion and booleans on bodies.

#include "Check.h"

#include "model/Profiles.h"
#include "model/Solids.h"
#include "sketch/Solver.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>

namespace {

double areaOf(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

void rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    const PointId a = s.addPoint({x0, y0}), b = s.addPoint({x1, y0});
    const PointId c = s.addPoint({x1, y1}), d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
}

std::vector<Profile> regionsOf(const Sketch& s)
{
    std::vector<Profile> out;
    for (const TopoDS_Face& f : findRegions(s))
        out.push_back({f, s.plane().Direction(), -1});
    return out;
}

double totalVolume(const std::vector<TopoDS_Shape>& bodies)
{
    double v = 0.0;
    for (const TopoDS_Shape& b : bodies)
        v += volumeOf(b);
    return v;
}

// The planar face of `body` whose outward normal is `n`, furthest along it.
TopoDS_Face faceFacing(const TopoDS_Shape& body, const gp_Dir& n)
{
    TopoDS_Face best;
    double bestHeight = -1e300;
    for (TopExp_Explorer it(body, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face f = TopoDS::Face(it.Current());
        const auto normal = planarNormal(f);
        if (!normal || !normal->IsEqual(n, 1e-9))
            continue;
        GProp_GProps props;
        BRepGProp::SurfaceProperties(f, props);
        const double h = gp_Vec(props.CentreOfMass().XYZ()).Dot(gp_Vec(n));
        if (h > bestHeight) {
            bestHeight = h;
            best = f;
        }
    }
    return best;
}

void testRegionsSplitHolesAndIslands()
{
    Sketch s;
    rectangle(s, 0, 0, 10, 6);
    s.addLine(s.addPoint({-3, 3}), s.addPoint({13, 3}));  // overshoots both sides
    s.addLine(s.addPoint({2, 1}), s.addPoint({4, 1}));    // dangling inside
    s.addCircle(s.addPoint({7, 4.5}), 0.8);              // island in the upper half

    const auto regions = findRegions(s);
    CHECK(regions.size() == 3);
    std::vector<double> areas;
    for (const TopoDS_Face& f : regions)
        areas.push_back(areaOf(f));
    std::sort(areas.begin(), areas.end());
    const double disc = M_PI * 0.8 * 0.8;
    CHECK(near(areas[0], disc, 1e-6));
    CHECK(near(areas[1], 30.0 - disc, 1e-6)); // upper half, with the disc as a hole
    CHECK(near(areas[2], 30.0, 1e-6));        // lower half; the stray line is no wall

    const auto inLower = regionAt(regions, gp_Pnt(5, 1.5, 0));
    const auto inDisc = regionAt(regions, gp_Pnt(7, 4.5, 0));
    CHECK(inLower && near(areaOf(regions[*inLower]), 30.0, 1e-6));
    CHECK(inDisc && near(areaOf(regions[*inDisc]), disc, 1e-6));
    CHECK(!regionAt(regions, gp_Pnt(20, 20, 0)));
}

void testOpenCurvesMakeNoRegion()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({10, 0}), c = s.addPoint({10, 10});
    s.addLine(a, b);
    s.addLine(b, c);
    CHECK(findRegions(s).empty());
}

void testArcBoundedRegion()
{
    // Half disc on another plane: arc over the top, closed by its diameter.
    Sketch s(gp_Ax3(gp::Origin(), -gp::DY(), gp::DX())); // front plane
    const PointId c = s.addPoint({0, 0}), a = s.addPoint({5, 0}), b = s.addPoint({-5, 0});
    s.addArc(c, a, b);
    s.addLine(b, a);
    const auto regions = findRegions(s);
    CHECK(regions.size() == 1);
    CHECK(near(areaOf(regions[0]), M_PI * 25.0 / 2.0, 1e-6));
}

void testNewBodyAndAutoCutJoin()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    ModelResult box = extrude({}, regionsOf(base), 5.0, BodyOperation::Auto);
    CHECK(box.ok() && box.bodies.size() == 1);
    CHECK(near(volumeOf(box.bodies[0]), 300.0, 1e-6));

    Sketch hole;
    hole.addCircle(hole.addPoint({5, 3}), 1.0);
    const double cylinder = M_PI * 5.0;

    // Fully inside the box: Auto cuts (a through hole here).
    ModelResult cut = extrude(box.bodies, regionsOf(hole), 5.0, BodyOperation::Auto);
    CHECK(cut.ok() && cut.bodies.size() == 1);
    CHECK(near(volumeOf(cut.bodies[0]), 300.0 - cylinder, 1e-6));

    // Sticking out of the top: Auto joins.
    ModelResult boss = extrude(box.bodies, regionsOf(hole), 8.0, BodyOperation::Auto);
    CHECK(boss.ok() && boss.bodies.size() == 1);
    CHECK(near(volumeOf(boss.bodies[0]), 300.0 + M_PI * 3.0, 1e-6));

    // Asked for a new body: the overlap is left alone.
    ModelResult separate = extrude(box.bodies, regionsOf(hole), 8.0, BodyOperation::NewBody);
    CHECK(separate.ok() && separate.bodies.size() == 2);

    // Away from everything: a new body even in Auto.
    Sketch far;
    rectangle(far, 50, 50, 52, 52);
    ModelResult second = extrude(box.bodies, regionsOf(far), -3.0, BodyOperation::Auto);
    CHECK(second.ok() && second.bodies.size() == 2);
    CHECK(near(totalVolume(second.bodies), 300.0 + 12.0, 1e-6));
}

void testPushPullBodyFace()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    const TopoDS_Shape box = extrude({}, regionsOf(base), 5.0, BodyOperation::Auto).bodies.at(0);
    const TopoDS_Face top = faceFacing(box, gp::DZ());
    CHECK(!top.IsNull());

    ModelResult pulled = extrude({box}, {{top, gp::DZ(), 0}}, 2.0, BodyOperation::Auto);
    CHECK(pulled.ok() && pulled.bodies.size() == 1);
    CHECK(near(volumeOf(pulled.bodies[0]), 420.0, 1e-6));
    // Coplanar faces are merged back: still a plain box with six faces.
    int faces = 0;
    for (TopExp_Explorer it(pulled.bodies[0], TopAbs_FACE); it.More(); it.Next())
        ++faces;
    CHECK(faces == 6);

    ModelResult pushed = extrude({box}, {{top, gp::DZ(), 0}}, -2.0, BodyOperation::Auto);
    CHECK(pushed.ok() && near(volumeOf(pushed.bodies.at(0)), 180.0, 1e-6));

    // Pushed all the way through: nothing is left.
    ModelResult gone = extrude({box}, {{top, gp::DZ(), 0}}, -6.0, BodyOperation::Auto);
    CHECK(gone.ok() && gone.bodies.empty());
}

void testSketchOnFacePulledOutJoins()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    const TopoDS_Shape box = extrude({}, regionsOf(base), 5.0, BodyOperation::Auto).bodies.at(0);

    // A sketch lying on the top face only touches the box; pulled up, it still joins.
    Sketch onTop(gp_Ax3(gp_Pnt(0, 0, 5), gp::DZ(), gp::DX()));
    rectangle(onTop, 2, 2, 4, 4);
    ModelResult up = extrude({box}, regionsOf(onTop), 3.0, BodyOperation::Auto);
    CHECK(up.ok() && up.bodies.size() == 1);
    CHECK(near(volumeOf(up.bodies[0]), 300.0 + 12.0, 1e-6));

    // Pushed down into it, it cuts a pocket.
    ModelResult down = extrude({box}, regionsOf(onTop), -2.0, BodyOperation::Auto);
    CHECK(down.ok() && down.bodies.size() == 1);
    CHECK(near(volumeOf(down.bodies[0]), 300.0 - 8.0, 1e-6));
}

// The box's edge along X at y = 0, z = 5 (top front edge).
EdgeRef topFrontEdge(const TopoDS_Shape& box)
{
    for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
        GProp_GProps props;
        BRepGProp::LinearProperties(it.Current(), props);
        const gp_Pnt c = props.CentreOfMass();
        if (near(c.Y(), 0.0, 1e-9) && near(c.Z(), 5.0, 1e-9) && near(props.Mass(), 10.0, 1e-9))
            return {0, TopoDS::Edge(it.Current())};
    }
    return {};
}

void testFilletAndChamfer()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    const TopoDS_Shape box = extrude({}, regionsOf(base), 5.0, BodyOperation::Auto).bodies.at(0);
    const EdgeRef edge = topFrontEdge(box);
    CHECK(!edge.edge.IsNull());

    // A round of radius 1 along a 10 mm edge removes (1 - pi/4) * 1² * 10.
    ModelResult round = filletEdges({box}, {edge}, 1.0);
    CHECK(round.ok() && round.bodies.size() == 1);
    CHECK(near(volumeOf(round.bodies[0]), 300.0 - (1.0 - M_PI / 4.0) * 10.0, 1e-6));

    // A 1 mm chamfer removes a right triangle of 0.5 mm² along it.
    ModelResult bevel = chamferEdges({box}, {edge}, 1.0);
    CHECK(bevel.ok() && near(volumeOf(bevel.bodies.at(0)), 295.0, 1e-6));

    // The handle sits on the edge's middle, pointing into the box between both faces.
    const auto handle = edgeHandle(box, edge.edge);
    CHECK(handle && handle->first.Distance(gp_Pnt(5, 0, 5)) < 1e-9);
    CHECK(handle && handle->second.IsEqual(gp_Dir(0, 1, -1), 1e-9));

    // Bigger than the box allows: refused, bodies unchanged.
    ModelResult tooBig = filletEdges({box}, {edge}, 7.0);
    CHECK(!tooBig.ok());
    CHECK(!filletEdges({box}, {}, 1.0).ok());
}

void testShell()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    const TopoDS_Shape box = extrude({}, regionsOf(base), 5.0, BodyOperation::Auto).bodies.at(0);
    const TopoDS_Face top = faceFacing(box, gp::DZ());

    // Open at the top, 1 mm walls: the inside is 8 x 4 x 4.
    ModelResult hollow = shellBodies({box}, {{0, top}}, 1.0);
    CHECK(hollow.ok() && hollow.bodies.size() == 1);
    CHECK(near(volumeOf(hollow.bodies[0]), 300.0 - 128.0, 1e-6));

    CHECK(!shellBodies({box}, {{0, top}}, 3.5).ok()); // thicker than the box is wide
    CHECK(!shellBodies({box}, {}, 1.0).ok());

    // A boss standing on the opened face (OCCT's own maker gives up on that one).
    Sketch bossSketch(gp_Ax3(gp_Pnt(0, 0, 5), gp::DZ(), gp::DX()));
    bossSketch.addCircle(bossSketch.addPoint({5, 3}), 1.5);
    const TopoDS_Shape withBoss = extrude({box}, regionsOf(bossSketch), 3.0, BodyOperation::Auto).bodies.at(0);
    const TopoDS_Face openTop = faceFacing(withBoss, gp::DZ()); // highest +Z face: the boss top
    TopoDS_Face plateTop;
    for (TopExp_Explorer it(withBoss, TopAbs_FACE); it.More(); it.Next()) {
        const TopoDS_Face f = TopoDS::Face(it.Current());
        GProp_GProps props;
        BRepGProp::SurfaceProperties(f, props);
        if (planarNormal(f) && planarNormal(f)->IsEqual(gp::DZ(), 1e-9) && near(props.CentreOfMass().Z(), 5.0, 1e-9))
            plateTop = f;
    }
    CHECK(!plateTop.IsNull() && !openTop.IsNull());
    ModelResult hollowBoss = shellBodies({withBoss}, {{0, plateTop}}, 0.5);
    CHECK(hollowBoss.ok() && hollowBoss.bodies.size() == 1);
    if (hollowBoss.ok()) {
        const TopoDS_Shape& shell = hollowBoss.bodies[0];
        CHECK(volumeOf(shell) < volumeOf(withBoss) && volumeOf(shell) > 0.0);
        // Inside the cavity there is no material; inside a wall there is.
        BRepClass3d_SolidClassifier inside(shell, gp_Pnt(2, 3, 2.5), 1e-7);
        CHECK(inside.State() == TopAbs_OUT);
        BRepClass3d_SolidClassifier wall(shell, gp_Pnt(0.25, 3, 2.5), 1e-7);
        CHECK(wall.State() == TopAbs_IN);
        // The opened face is gone: above the cavity, at the old top, it's open.
        BRepClass3d_SolidClassifier opening(shell, gp_Pnt(2, 3, 4.9), 1e-7);
        CHECK(opening.State() == TopAbs_OUT);
    }
}

void testFaceEdgesBecomeFixedReference()
{
    Sketch base;
    rectangle(base, 0, 0, 10, 6);
    base.addCircle(base.addPoint({30, 3}), 2.0);
    const std::vector<TopoDS_Shape> bodies = extrude({}, regionsOf(base), 5.0, BodyOperation::NewBody).bodies;
    CHECK(bodies.size() == 2);

    for (const TopoDS_Shape& body : bodies) {
        const TopoDS_Face top = faceFacing(body, gp::DZ());
        Sketch onTop(gp_Ax3(gp_Pnt(0, 0, 5), gp::DZ(), gp::DX()));
        projectFaceEdges(onTop, top);
        const bool box = onTop.entities().size() == 4;
        CHECK(box || (onTop.entities().size() == 1 && onTop.entities()[0].kind == EntityKind::Circle));
        for (const SketchEntity& e : onTop.entities())
            CHECK(e.construction);
        // Corners shared between neighbouring edges, each pinned in place; circles by
        // center and radius: nothing left to move.
        CHECK(onTop.points().size() == (box ? 4u : 1u));
        CHECK(onTop.constraints().size() == onTop.points().size());
        CHECK(solve(onTop).dof == 0);
        // Reference only: nothing to pull until real curves are drawn.
        CHECK(findRegions(onTop).empty());
        if (box) {
            onTop.addCircle(onTop.addPoint({5, 3}), 1.0);
            CHECK(findRegions(onTop).size() == 1);
        }
    }
}

void testCombineBodies()
{
    Sketch a, b;
    rectangle(a, 0, 0, 10, 10);
    rectangle(b, 5, 5, 15, 15);
    std::vector<TopoDS_Shape> bodies = extrude({}, regionsOf(a), 10.0, BodyOperation::NewBody).bodies;
    const auto more = extrude({}, regionsOf(b), 10.0, BodyOperation::NewBody).bodies;
    bodies.insert(bodies.end(), more.begin(), more.end());
    CHECK(bodies.size() == 2);

    ModelResult u = combine(bodies, {0, 1}, BooleanKind::Union);
    CHECK(u.ok() && u.bodies.size() == 1 && near(volumeOf(u.bodies[0]), 1750.0, 1e-6));
    ModelResult d = combine(bodies, {0, 1}, BooleanKind::Subtract);
    CHECK(d.ok() && d.bodies.size() == 1 && near(volumeOf(d.bodies[0]), 750.0, 1e-6));
    ModelResult i = combine(bodies, {1, 0}, BooleanKind::Intersect);
    CHECK(i.ok() && i.bodies.size() == 1 && near(volumeOf(i.bodies[0]), 250.0, 1e-6));
    CHECK(!combine(bodies, {0}, BooleanKind::Union).ok());
}

} // namespace

int main()
{
    return runTests({
        {"regions split, holes and islands", testRegionsSplitHolesAndIslands},
        {"open curves make no region", testOpenCurvesMakeNoRegion},
        {"arc bounded region", testArcBoundedRegion},
        {"new body, auto cut and join", testNewBodyAndAutoCutJoin},
        {"push / pull a body face", testPushPullBodyFace},
        {"sketch on a face pulled out joins", testSketchOnFacePulledOutJoins},
        {"fillet and chamfer", testFilletAndChamfer},
        {"shell", testShell},
        {"face edges become fixed reference", testFaceEdgesBecomeFixedReference},
        {"combine bodies", testCombineBodies},
    });
}
