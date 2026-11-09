// Transform and inspect (M11): moving / copying bodies in the history, lining parts up,
// measuring, body styles found again after rebuilds, units.

#include "Check.h"

#include "model/Features.h"
#include "model/Measure.h"
#include "model/Units.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <map>

namespace {

constexpr double kPi = 3.14159265358979323846;

// The face of `body` whose middle is nearest `p` (and the edge / corner likewise).
TopoDS_Shape nearest(const TopoDS_Shape& body, TopAbs_ShapeEnum type, const gp_Pnt& p)
{
    TopoDS_Shape best;
    double bestDistance = 1e100;
    for (TopExp_Explorer it(body, type); it.More(); it.Next()) {
        const Measurement m = measure(it.Current(), BRepPrimAPI_MakeBox(p, 1e-3, 1e-3, 1e-3).Shape());
        if (m.distance && *m.distance < bestDistance) {
            bestDistance = *m.distance;
            best = it.Current();
        }
    }
    return best;
}

Feature extrudeAt(int sketch, gp_Pnt2d inside, double distance)
{
    Feature f;
    f.kind = Feature::Kind::Extrude;
    f.regions = {{sketch, inside}};
    f.value = distance;
    return f;
}

void square(Sketch& s, double x0, double y0, double x1, double y1)
{
    const PointId a = s.addPoint({x0, y0}), b = s.addPoint({x1, y0}), c = s.addPoint({x1, y1}), d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
}

void testMeasuringOneItem()
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const Measurement top = measure(nearest(box, TopAbs_FACE, gp_Pnt(5, 10, 30)));
    CHECK(top.area && near(*top.area, 200.0, 1e-6));
    const Measurement edge = measure(nearest(box, TopAbs_EDGE, gp_Pnt(5, 0, 0)));
    CHECK(edge.length && near(*edge.length, 10.0, 1e-6));
    const Measurement corner = measure(nearest(box, TopAbs_VERTEX, gp_Pnt(10, 20, 30)));
    CHECK(corner.position && near(corner.position->Distance(gp_Pnt(10, 20, 30)), 0.0, 1e-9));

    const TopoDS_Shape rod = BRepPrimAPI_MakeCylinder(4.0, 10.0).Shape();
    const Measurement rim = measure(nearest(rod, TopAbs_EDGE, gp_Pnt(4, 0, 10)));
    CHECK(rim.radius && near(*rim.radius, 4.0, 1e-9));
    CHECK(rim.length && near(*rim.length, 8.0 * kPi, 1e-6));
    const Measurement side = measure(nearest(rod, TopAbs_FACE, gp_Pnt(4, 0, 5)));
    CHECK(side.radius && near(*side.radius, 4.0, 1e-9));
}

void testMeasuringTwoItems()
{
    // Two boxes 5 apart along X; one turned 30° about Z.
    const TopoDS_Shape a = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    const TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(15, 0, 0), 10, 10, 10).Shape();
    const Measurement gap = measure(nearest(a, TopAbs_FACE, gp_Pnt(10, 5, 5)), nearest(b, TopAbs_FACE, gp_Pnt(15, 5, 5)));
    CHECK(gap.distance && near(*gap.distance, 5.0, 1e-9));
    CHECK(gap.angle && near(*gap.angle, 0.0, 1e-6));

    gp_Trsf turn;
    turn.SetRotation(gp_Ax1(gp::Origin(), gp::DZ()), 30.0 * kPi / 180.0);
    const TopoDS_Shape c = BRepBuilderAPI_Transform(a, turn, true).Shape();
    const Measurement angled = measure(nearest(a, TopAbs_FACE, gp_Pnt(10, 5, 5)), nearest(c, TopAbs_FACE, gp_Pnt(8.66, 5, 5)));
    CHECK(angled.angle && near(*angled.angle, 30.0, 1e-6));

    // Hole centers 20 apart.
    const TopoDS_Shape p = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 2.0, 5.0).Shape();
    const TopoDS_Shape q = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(12, 16, 0), gp::DZ()), 3.0, 5.0).Shape();
    const Measurement centers = measure(nearest(p, TopAbs_EDGE, gp_Pnt(2, 0, 5)), nearest(q, TopAbs_EDGE, gp_Pnt(15, 16, 5)));
    CHECK(centers.centers && near(*centers.centers, 20.0, 1e-9));
}

void testMassProperties()
{
    const MassProperties m = massProperties(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10, 20, 30).Shape());
    CHECK(near(m.volume, 6000.0, 1e-6));
    CHECK(near(m.area, 2 * (200 + 300 + 600), 1e-6));
    CHECK(near(m.center.Distance(gp_Pnt(5, 10, 15)), 0.0, 1e-9));
    CHECK(near(m.size.X(), 10.0, 1e-6) && near(m.size.Y(), 20.0, 1e-6) && near(m.size.Z(), 30.0, 1e-6));
    // A box's principal moments: V (b² + c²) / 12 and so on.
    const double ix = 6000.0 * (400 + 900) / 12.0;
    CHECK(near(std::max({m.inertia.X(), m.inertia.Y(), m.inertia.Z()}), ix, 1e-3));
}

void testAlignment()
{
    const TopoDS_Shape a = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    const TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(30, 5, 2), 10, 10, 10).Shape();
    // Corner onto corner.
    const auto corner = alignment(nearest(a, TopAbs_VERTEX, gp_Pnt(10, 10, 10)), nearest(b, TopAbs_VERTEX, gp_Pnt(30, 5, 2)));
    CHECK(corner.has_value());
    if (corner)
        CHECK(near(gp_Pnt(10, 10, 10).Transformed(*corner).Distance(gp_Pnt(30, 5, 2)), 0.0, 1e-9));

    // Face flush against face: a's right face (x = 10) onto b's left face (x = 30).
    const auto flush = alignment(nearest(a, TopAbs_FACE, gp_Pnt(10, 5, 5)), nearest(b, TopAbs_FACE, gp_Pnt(30, 10, 7)));
    CHECK(flush.has_value());
    if (flush) {
        const TopoDS_Shape moved = BRepBuilderAPI_Transform(a, *flush, true).Shape();
        const Measurement m = measure(moved, b);
        CHECK(m.distance && near(*m.distance, 0.0, 1e-6)); // touching
        CHECK(near(gp_Pnt(10, 5, 5).Transformed(*flush).X(), 30.0, 1e-9));
    }

    // Face onto a face looking the same way: turned half around to face it.
    const auto turned = alignment(nearest(a, TopAbs_FACE, gp_Pnt(5, 5, 10)), nearest(b, TopAbs_FACE, gp_Pnt(35, 10, 12)));
    CHECK(turned.has_value());
    if (turned) {
        const gp_Dir up = gp::DZ().Transformed(*turned);
        CHECK(near(up.Z(), -1.0, 1e-9));
        CHECK(near(gp_Pnt(5, 5, 10).Transformed(*turned).Z(), 12.0, 1e-9));
    }

    // Two straight edges don't say how.
    CHECK(!alignment(nearest(a, TopAbs_EDGE, gp_Pnt(5, 0, 0)), nearest(b, TopAbs_EDGE, gp_Pnt(35, 5, 2))));
}

void testMoveAndCopyInTheHistory()
{
    std::map<int, Sketch> sketches;
    sketches.emplace(1, Sketch(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())));
    square(sketches.at(1), 0, 0, 10, 10);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };

    gp_Trsf t;
    t.SetRotation(gp_Ax1(gp::Origin(), gp::DZ()), kPi / 2.0);
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(50, 0, 0));
    Feature move;
    move.kind = Feature::Kind::Move;
    move.bodies = {gp_Pnt(5, 5, 5)};
    move.matrix = matrixOf(shift * t);
    std::vector<Feature> history = {extrudeAt(1, {5, 5}, 10.0), move};
    numberFeatures(history);
    RebuildResult moved = rebuild(lookup, history);
    CHECK(moved.errors.at(1).empty());
    CHECK(moved.bodies.size() == 1);
    CHECK(bodyContaining(moved.bodies, gp_Pnt(45, 5, 5)).has_value()); // turned a quarter, then 50 along X
    CHECK(!bodyContaining(moved.bodies, gp_Pnt(5, 5, 5)).has_value());

    history[1].copy = true;
    RebuildResult copied = rebuild(lookup, history);
    CHECK(copied.bodies.size() == 2);
    CHECK(bodyContaining(copied.bodies, gp_Pnt(5, 5, 5)).has_value());

    // A matrix that scales isn't a move.
    history[1].matrix[0] = 2.0;
    CHECK(!rebuild(lookup, history).errors.at(1).empty());
    CHECK(!trsfOf({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0})); // a mirror
    CHECK(trsfOf(matrixOf(gp_Trsf())).has_value());
}

void testBodyStylesFollowTheirBodies()
{
    const std::vector<TopoDS_Shape> bodies = {BRepPrimAPI_MakeBox(10, 10, 10).Shape(),
                                              BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 10, 10, 10).Shape()};
    BodyStyle right;
    right.inside = gp_Pnt(25, 5, 5);
    right.name = "bracket";
    BodyStyle lost;
    lost.inside = gp_Pnt(100, 0, 0);
    BodyStyle twin = right; // a second style for the same body: the first one wins
    twin.name = "twin";
    const std::vector<int> owner = matchBodyStyles(bodies, {lost, right, twin});
    CHECK(owner.size() == 2);
    CHECK(owner[0] == -1);
    CHECK(owner[1] == 1);
}

void testUnits()
{
    CHECK(lengthUnit() == LengthUnit::Millimetre);
    CHECK(near(toDisplayLength(25.4), 25.4));
    setLengthUnit(LengthUnit::Inch);
    CHECK(near(toDisplayLength(25.4), 1.0, 1e-12));
    CHECK(near(fromDisplayLength(0.5), 12.7, 1e-12));
    CHECK(std::string(lengthSymbol()) == "in");
    CHECK(lengthDecimals() == 3);
    setLengthUnit(LengthUnit::Millimetre);
    CHECK(std::string(lengthSymbol()) == "mm");
}

} // namespace

int main()
{
    return runTests({
        {"measuring one item", testMeasuringOneItem},
        {"measuring two items", testMeasuringTwoItems},
        {"mass properties", testMassProperties},
        {"alignment", testAlignment},
        {"move and copy in the history", testMoveAndCopyInTheHistory},
        {"body styles follow their bodies", testBodyStylesFollowTheirBodies},
        {"units", testUnits},
    });
}
