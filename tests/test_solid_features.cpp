// Solid features (M10): revolve, sweep, loft, mirror, patterns, holes, construction
// planes and axes, all replayed from the history.

#include "Check.h"

#include "model/Features.h"
#include "model/Profiles.h"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <map>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Rect
{
    PointId a, b, c, d;
    EntityId ab, bc, cd, da;
};

Rect rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    Rect r{s.addPoint({x0, y0}), s.addPoint({x1, y0}), s.addPoint({x1, y1}), s.addPoint({x0, y1}), 0, 0, 0, 0};
    r.ab = s.addLine(r.a, r.b);
    r.bc = s.addLine(r.b, r.c);
    r.cd = s.addLine(r.c, r.d);
    r.da = s.addLine(r.d, r.a);
    return r;
}

double totalVolume(const std::vector<TopoDS_Shape>& bodies)
{
    double v = 0.0;
    for (const TopoDS_Shape& b : bodies)
        v += volumeOf(b);
    return v;
}

bool allValid(const std::vector<TopoDS_Shape>& bodies)
{
    for (const TopoDS_Shape& b : bodies) {
        if (!BRepCheck_Analyzer(b).IsValid())
            return false;
    }
    return true;
}

struct Model
{
    std::map<int, Sketch> sketches;
    std::vector<Feature> history;
    SketchLookup lookup() const
    {
        return [this](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    }
    RebuildResult build()
    {
        numberFeatures(history);
        return rebuild(lookup(), history);
    }
    Feature& add(Feature f)
    {
        history.push_back(f);
        numberFeatures(history);
        return history.back();
    }
};

Feature extrudeAt(int sketch, gp_Pnt2d inside, double distance)
{
    Feature f;
    f.kind = Feature::Kind::Extrude;
    f.regions = {{sketch, inside}};
    f.value = distance;
    return f;
}

AxisRef datumAxis(int id)
{
    AxisRef a;
    a.kind = AxisRef::Kind::Datum;
    a.datum = id;
    return a;
}

PlaneRef datumPlane(int id)
{
    PlaneRef p;
    p.kind = PlaneRef::Kind::Datum;
    p.datum = id;
    return p;
}

// A 40 × 40 × 10 plate centered on the origin, from a sketch on the ground.
void plate(Model& m)
{
    m.sketches.emplace(1, Sketch(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())));
    rectangle(m.sketches.at(1), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
}

void testRevolveAFlange()
{
    // A ring cross-section on the front plane (x 10..30, z 0..5), around the Z axis.
    Model m;
    m.sketches.emplace(1, Sketch(baseDatum(kBaseXZ)->frame));
    rectangle(m.sketches.at(1), 10, 0, 30, 5);
    Feature f;
    f.kind = Feature::Kind::Revolve;
    f.regions = {{1, {20, 2.5}}};
    f.axis = datumAxis(kBaseZ);
    f.value = 360.0;
    m.add(f);
    RebuildResult r = m.build();
    CHECK(r.errors.at(0).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), kPi * (30 * 30 - 10 * 10) * 5, 1e-3));

    // Half a turn, then editing the angle back to a quarter.
    m.history[0].value = 90.0;
    CHECK(near(totalVolume(m.build().bodies), kPi * (30 * 30 - 10 * 10) * 5 / 4.0, 1e-3));

    // Around one of the sketch's own lines (its inner edge): a ring 20 wide, from radius 0.
    Model n;
    n.sketches.emplace(1, Sketch(baseDatum(kBaseXZ)->frame));
    const Rect rect = rectangle(n.sketches.at(1), 10, 0, 30, 5);
    f.axis = {};
    f.axis.kind = AxisRef::Kind::SketchLine;
    f.axis.sketch = 1;
    f.axis.entity = rect.da; // x = 10, upright
    f.value = 360.0;
    n.add(f);
    RebuildResult around = n.build();
    CHECK(around.errors.at(0).empty());
    CHECK(near(totalVolume(around.bodies), kPi * 20 * 20 * 5, 1e-3));

    // An axis through the profile can't work.
    n.history[0].axis.entity = kNoId;
    n.history[0].axis = datumAxis(kBaseZ);
    n.sketches.at(1).setPos(rect.a, {-10, 0});
    n.sketches.at(1).setPos(rect.d, {-10, 5});
    RebuildResult crossing = n.build();
    CHECK(!crossing.errors.at(0).empty());
    CHECK(crossing.bodies.empty());
}

void testSweepAHandle()
{
    // A round profile (r 2) on the right plane at the origin, along an L with a rounded bend.
    Model m;
    m.sketches.emplace(1, Sketch(baseDatum(kBaseYZ)->frame));
    Sketch& profile = m.sketches.at(1);
    profile.addCircle(profile.addPoint({0, 0}), 2.0);
    m.sketches.emplace(2, Sketch(baseDatum(kBaseXY)->frame));
    Sketch& path = m.sketches.at(2);
    const PointId p0 = path.addPoint({0, 0}), p1 = path.addPoint({20, 0});
    const PointId c = path.addPoint({20, 10}), p2 = path.addPoint({30, 10}), p3 = path.addPoint({30, 30});
    const EntityId first = path.addLine(p0, p1);
    path.addArc(c, p1, p2);
    path.addLine(p2, p3);

    Feature f;
    f.kind = Feature::Kind::Sweep;
    f.regions = {{1, {0, 0}}};
    f.pathSketch = 2;
    f.pathEntity = first;
    m.add(f);
    RebuildResult r = m.build();
    CHECK(r.errors.at(0).empty());
    CHECK(r.bodies.size() == 1);
    const double length = 20.0 + 5.0 * kPi + 20.0;
    CHECK(near(totalVolume(r.bodies), kPi * 4.0 * length, 0.5));
    CHECK(allValid(r.bodies));

    // A longer last leg: the handle follows.
    path.setPos(p3, {30, 40});
    CHECK(near(totalVolume(m.build().bodies), kPi * 4.0 * (length + 10.0), 0.5));
}

void testLoft()
{
    // A 20 × 20 square on the ground to a circle r 5 twenty up.
    Model m;
    m.sketches.emplace(1, Sketch(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())));
    rectangle(m.sketches.at(1), -10, -10, 10, 10);
    m.sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(0, 0, 20), gp::DZ(), gp::DX())));
    m.sketches.at(2).addCircle(m.sketches.at(2).addPoint({0, 0}), 5.0);
    Feature f;
    f.kind = Feature::Kind::Loft;
    f.regions = {{1, {0, 0}}, {2, {0, 0}}};
    m.add(f);
    RebuildResult r = m.build();
    CHECK(r.errors.at(0).empty());
    CHECK(r.bodies.size() == 1);
    const double v = totalVolume(r.bodies);
    CHECK(v > kPi * 25 * 20 && v < 400 * 20); // between the circle's and the square's prism

    m.history[0].regions.pop_back();
    CHECK(!m.build().errors.at(0).empty()); // one profile isn't a loft
}

Feature holeAt(const gp_Pnt& p, double diameter)
{
    Feature f;
    f.kind = Feature::Kind::Hole;
    f.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
    f.points = {p};
    f.hole.diameter = diameter;
    return f;
}

void testHoles()
{
    Model m;
    plate(m);
    const double plateVolume = 40.0 * 40.0 * 10.0;
    m.add(holeAt(gp_Pnt(10, 0, 10), 6.0)); // through
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(near(totalVolume(r.bodies), plateVolume - kPi * 9.0 * 10.0, 1e-3));

    // Blind, 4 deep.
    m.history[1].hole.depth = 4.0;
    CHECK(near(totalVolume(m.build().bodies), plateVolume - kPi * 9.0 * 4.0, 1e-3));

    // Counterbored: 10 across, 3 deep, over the through hole.
    m.history[1].hole.depth = 0.0;
    m.history[1].hole.kind = HoleKind::Counterbore;
    m.history[1].hole.headDiameter = 10.0;
    m.history[1].hole.headDepth = 3.0;
    CHECK(near(totalVolume(m.build().bodies), plateVolume - kPi * 9.0 * 7.0 - kPi * 25.0 * 3.0, 1e-3));

    // Countersunk 90°: a cone from r 5 down to r 3 (2 deep) over the hole.
    m.history[1].hole.kind = HoleKind::Countersink;
    const double cone = kPi * 2.0 / 3.0 * (25.0 + 15.0 + 9.0);
    CHECK(near(totalVolume(m.build().bodies), plateVolume - kPi * 9.0 * 8.0 - cone, 1e-2));

    // Off the plate: nothing to drill.
    m.history[1].points = {gp_Pnt(60, 0, 10)};
    CHECK(!m.build().errors.at(1).empty());
}

void testHoleOnASketchPointFollowsIt()
{
    Model m;
    plate(m);
    m.sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX())));
    const PointId spot = m.sketches.at(2).addPoint({5, 5});
    Feature f = holeAt(gp_Pnt(), 4.0);
    f.points.clear();
    f.holeAt = {{2, spot}};
    f.hole.depth = 5.0;
    m.add(f);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    const double drilled = 40.0 * 40.0 * 10.0 - kPi * 4.0 * 5.0;
    CHECK(near(totalVolume(r.bodies), drilled, 1e-3));
    // Moved to the plate's edge: half the hole is in the air.
    m.sketches.at(2).setPos(spot, {20, 0});
    CHECK(near(totalVolume(m.build().bodies), 40.0 * 40.0 * 10.0 - kPi * 4.0 * 5.0 / 2.0, 1e-3));
}

void testBoltCircle()
{
    // A hole, then a circular pattern of it around the Z axis: six holes, editable count.
    Model m;
    plate(m);
    const int hole = m.add(holeAt(gp_Pnt(15, 0, 10), 4.0)).id;
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.pattern = Feature::PatternKind::Circular;
    pattern.source = hole;
    pattern.axis = datumAxis(kBaseZ);
    pattern.count = 6;
    pattern.value = 360.0;
    m.add(pattern);
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    const double oneHole = kPi * 4.0 * 10.0;
    CHECK(near(totalVolume(r.bodies), 16000.0 - 6.0 * oneHole, 1e-2));

    m.history[2].count = 4;
    CHECK(near(totalVolume(m.build().bodies), 16000.0 - 4.0 * oneHole, 1e-2));

    // A bigger hole changes all of them.
    m.history[1].hole.diameter = 6.0;
    CHECK(near(totalVolume(m.build().bodies), 16000.0 - 4.0 * kPi * 9.0 * 10.0, 1e-2));

    // Over half a turn: copies 60° apart; those running off the plate are skipped.
    m.history[2].value = 180.0;
    m.history[2].count = 4;
    CHECK(m.build().errors.at(2).empty());
}

void testLinearPatternOfAFeatureAndOfBodies()
{
    // A boss on the plate, repeated twice more 12 apart along X.
    Model m;
    plate(m);
    m.sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX())));
    m.sketches.at(2).addCircle(m.sketches.at(2).addPoint({-12, 0}), 3.0);
    const int boss = m.add(extrudeAt(2, {-12, 0}, 5.0)).id;
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.source = boss;
    pattern.axis = datumAxis(kBaseX);
    pattern.count = 3;
    pattern.value = 12.0;
    m.add(pattern);
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(r.bodies.size() == 1); // joined
    CHECK(near(totalVolume(r.bodies), 16000.0 + 3.0 * kPi * 9.0 * 5.0, 1e-2));

    // Bodies: a separate block, three of them.
    Model b;
    b.sketches.emplace(1, Sketch(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())));
    rectangle(b.sketches.at(1), 0, 0, 10, 10);
    b.add(extrudeAt(1, {5, 5}, 10.0));
    Feature bodies;
    bodies.kind = Feature::Kind::Pattern;
    bodies.bodies = {gp_Pnt(5, 5, 5)};
    bodies.axis = datumAxis(kBaseY);
    bodies.count = 3;
    bodies.value = 15.0;
    b.add(bodies);
    RebuildResult three = b.build();
    CHECK(three.errors.at(1).empty());
    CHECK(three.bodies.size() == 3);
    CHECK(near(totalVolume(three.bodies), 3000.0, 1e-3));
}

// The face of the model nearest `p` (its middle).
TopoDS_Face faceNear(const std::vector<TopoDS_Shape>& bodies, const gp_Pnt& p)
{
    TopoDS_Face best;
    double bestDistance = 1e300;
    for (const TopoDS_Shape& b : bodies) {
        for (TopExp_Explorer it(b, TopAbs_FACE); it.More(); it.Next()) {
            const TopoDS_Face face = TopoDS::Face(it.Current());
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            const double d = props.CentreOfMass().Distance(p);
            if (d < bestDistance) {
                bestDistance = d;
                best = face;
            }
        }
    }
    return best;
}

void testPickingWhatAFeatureMade()
{
    // A square pocket cut from a sketch on the plate's top, and a round boss joined on it.
    Model m;
    plate(m);
    m.sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX())));
    rectangle(m.sketches.at(2), 10, -2, 14, 2);
    m.sketches.at(2).addCircle(m.sketches.at(2).addPoint({-12, 0}), 3.0);
    Feature pocket = extrudeAt(2, {12, 0}, -4.0);
    pocket.operation = BodyOperation::Cut;
    const int pocketId = m.add(pocket).id;
    Feature boss = extrudeAt(2, {-12, 0}, 5.0);
    boss.operation = BodyOperation::Join;
    const int bossId = m.add(boss).id;
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty() && r.errors.at(2).empty());

    // The pocket's floor and walls, the boss' side and top; the plate's faces are no feature's.
    CHECK(featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(12, 0, 6))) == pocketId);
    CHECK(featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(14, 0, 8))) == pocketId);
    CHECK(featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(-12, 0, 15))) == bossId);
    CHECK(featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(-12, 0, 12.5))) == bossId);
    CHECK(!featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(0, -20, 5))));
    CHECK(!featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(0, 0, 0))));
    // The pocket shows as its five faces.
    CHECK(facesOnTool(r.bodies, r.context.tools.at(pocketId).shape).size() == 5);

    // Picked so, a circular pattern of the pocket: four of them around Z.
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.pattern = Feature::PatternKind::Circular;
    pattern.source = *featureOfFace(m.history, r.context, faceNear(r.bodies, gp_Pnt(12, 0, 6)));
    pattern.axis = datumAxis(kBaseZ);
    pattern.count = 4;
    pattern.value = 360.0;
    m.add(pattern);
    RebuildResult four = m.build();
    CHECK(four.errors.at(3).empty());
    CHECK(allValid(four.bodies));
    CHECK(near(totalVolume(four.bodies), 16000.0 + kPi * 9.0 * 5.0 - 4.0 * 4.0 * 4.0 * 4.0, 1e-2));
}

void testMirror()
{
    // A block touching the right plane joins its mirror image; one apart stays apart.
    Model m;
    m.sketches.emplace(1, Sketch(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())));
    rectangle(m.sketches.at(1), 0, 0, 10, 10);
    rectangle(m.sketches.at(1), 20, 0, 30, 10);
    m.add(extrudeAt(1, {5, 5}, 4.0));
    m.add(extrudeAt(1, {25, 5}, 4.0));
    Feature mirror;
    mirror.kind = Feature::Kind::Mirror;
    mirror.bodies = {gp_Pnt(5, 5, 2), gp_Pnt(25, 5, 2)};
    mirror.plane = datumPlane(kBaseYZ);
    m.add(mirror);
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(r.bodies.size() == 3); // the joined pair, the far block and its image
    CHECK(near(totalVolume(r.bodies), 4 * 400.0, 1e-3));
}

void testConstructionPlanesAndAxes()
{
    Model m;
    plate(m);
    // 15 above the ground.
    Feature offset;
    offset.kind = Feature::Kind::Datum;
    offset.datum = Feature::DatumKind::OffsetPlane;
    offset.plane = datumPlane(kBaseXY);
    offset.value = 15.0;
    const int above = m.add(offset).id;
    // Turned 90° about the X axis from the ground: the front plane (up to the sense).
    Feature angled;
    angled.kind = Feature::Kind::Datum;
    angled.datum = Feature::DatumKind::AnglePlane;
    angled.plane = datumPlane(kBaseXY);
    angled.axis = datumAxis(kBaseX);
    angled.value = 90.0;
    const int turned = m.add(angled).id;
    // Through three corners of the plate's top.
    Feature three;
    three.kind = Feature::Kind::Datum;
    three.datum = Feature::DatumKind::ThreePointPlane;
    three.points = {gp_Pnt(-20, -20, 10), gp_Pnt(20, -20, 10), gp_Pnt(20, 20, 10)};
    const int top = m.add(three).id;
    // Along the plate's top front edge.
    Feature edge;
    edge.kind = Feature::Kind::Datum;
    edge.datum = Feature::DatumKind::Axis;
    edge.axis.kind = AxisRef::Kind::Edge;
    edge.axis.edge = {gp_Pnt(0, -20, 10), gp::DX()};
    const int along = m.add(edge).id;

    RebuildResult r = m.build();
    for (std::size_t i = 0; i < r.errors.size(); ++i)
        CHECK(r.errors[i].empty());
    const Datum* d = r.context.datum(above);
    CHECK(d && d->plane && near(d->frame.Location().Z(), 15.0, 1e-9));
    d = r.context.datum(turned);
    CHECK(d && std::abs(d->frame.Direction().Dot(gp::DY())) > 0.999);
    d = r.context.datum(top);
    CHECK(d && d->plane && near(d->frame.Direction().Z(), 1.0, 1e-9) && near(d->frame.Location().Z(), 10.0, 1e-9));
    d = r.context.datum(along);
    CHECK(d && !d->plane && std::abs(d->frame.Direction().X()) > 0.999 && near(d->frame.Location().Y(), -20.0, 1e-9));

    // A plane made from the plate's top face moves with it.
    Feature onFace;
    onFace.kind = Feature::Kind::Datum;
    onFace.datum = Feature::DatumKind::OffsetPlane;
    onFace.plane.kind = PlaneRef::Kind::Face;
    onFace.plane.face = {gp_Pnt(0, 0, 10), gp::DZ()};
    onFace.value = 5.0;
    const int faceDatum = m.add(onFace).id;
    m.history[0].value = 20.0; // thicker plate
    RebuildResult thicker = m.build();
    d = thicker.context.datum(faceDatum);
    CHECK(d && near(d->frame.Location().Z(), 25.0, 1e-6));

    // Datums serve as references: mirror the plate about the plane 15 up.
    Feature mirror;
    mirror.kind = Feature::Kind::Mirror;
    mirror.bodies = {gp_Pnt(0, 0, 5)};
    mirror.plane = datumPlane(above);
    mirror.operation = BodyOperation::NewBody;
    m.add(mirror);
    m.history[0].value = 10.0;
    RebuildResult mirrored = m.build();
    CHECK(mirrored.errors.back().empty());
    CHECK(mirrored.bodies.size() == 2);
}

void testOldHistoriesGetIds()
{
    std::vector<Feature> features(3);
    features[1].id = 7;
    numberFeatures(features);
    CHECK(features[0].id == 8);
    CHECK(features[1].id == 7);
    CHECK(features[2].id == 9);
}

} // namespace

int main()
{
    return runTests({
        {"revolve a flange", testRevolveAFlange},
        {"sweep a handle", testSweepAHandle},
        {"loft", testLoft},
        {"holes", testHoles},
        {"hole on a sketch point follows it", testHoleOnASketchPointFollowsIt},
        {"bolt circle", testBoltCircle},
        {"linear pattern of a feature and of bodies", testLinearPatternOfAFeatureAndOfBodies},
        {"picking what a feature made", testPickingWhatAFeatureMade},
        {"mirror", testMirror},
        {"construction planes and axes", testConstructionPlanesAndAxes},
        {"old histories get ids", testOldHistoriesGetIds},
    });
}
