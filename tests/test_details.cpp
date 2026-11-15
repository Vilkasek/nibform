// The finishing features (M16): threads (cosmetic and modelled, on cylinders and in
// tapped holes), draft, ribs, emboss / engrave and fillets of varying radius, all
// replayed from the history.

#include "Check.h"

#include "io/ProjectFile.h"
#include "model/Drawing.h"
#include "model/Features.h"
#include "model/Parameters.h"
#include "model/Profiles.h"
#include "model/TextShapes.h"
#include "sketch/DrawList.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <map>

namespace {

constexpr double kPi = 3.14159265358979323846;

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
    Sketch& sketch(int id, const gp_Ax3& plane)
    {
        sketches.erase(id);
        return sketches.emplace(id, Sketch(plane)).first->second;
    }
};

void rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    const PointId a = s.addPoint({x0, y0}), b = s.addPoint({x1, y0}), c = s.addPoint({x1, y1}),
                  d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
}

Feature extrudeAt(int sketch, gp_Pnt2d inside, double distance)
{
    Feature f;
    f.kind = Feature::Kind::Extrude;
    f.regions = {{sketch, inside}};
    f.value = distance;
    return f;
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

bool inside(const TopoDS_Shape& body, const gp_Pnt& p)
{
    return BRepClass3d_SolidClassifier(body, p, 1e-7).State() == TopAbs_IN;
}

const gp_Ax3 kGround(gp::Origin(), gp::DZ(), gp::DX());

// A 40 × 40 × 10 plate on the ground (z 0..10) with a round boss r 3, 12 high on top
// (z 10..22) at the middle.
void plateWithBoss(Model& m, double radius = 3.0, double height = 12.0)
{
    rectangle(m.sketch(1, kGround), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
    Sketch& top = m.sketch(2, gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX()));
    top.addCircle(top.addPoint({0, 0}), radius);
    Feature boss = extrudeAt(2, {0, 0}, height);
    boss.operation = BodyOperation::Join;
    m.add(boss);
}

// --- Threads ------------------------------------------------------------------------

void testThreadSizes()
{
    const ThreadSpec m8 = nearestThread(ThreadStandard::Metric, 7.9);
    CHECK(near(m8.diameter, 8.0) && near(m8.pitch, 1.25));
    CHECK(threadCallout(m8) == "M8×1.25");
    CHECK(near(tapDrill(m8), 6.8));
    CHECK(near(minorDiameter(m8), 8.0 - 1.226869 * 1.25, 1e-5));

    const ThreadSpec quarter = nearestThread(ThreadStandard::UNC, 6.3);
    CHECK(threadCallout(quarter) == "1/4-20 UNC");
    CHECK(near(quarter.pitch, 25.4 / 20.0));
    CHECK(threadCallout(nearestThread(ThreadStandard::UNF, 4.8)) == "#10-32 UNF");

    // A hole drilled 6.8 is tapped M8; a 5.0 one M6.
    CHECK(near(threadForCylinder(ThreadStandard::Metric, 6.8, true).diameter, 8.0));
    CHECK(near(threadForCylinder(ThreadStandard::Metric, 5.0, true).diameter, 6.0));
    // A size not in the table still gets a callout and a drill.
    ThreadSpec odd{ThreadStandard::Metric, 7.5, 1.0, 0.0, false};
    CHECK(threadCallout(odd) == "M7.5×1");
    CHECK(near(tapDrill(odd), 6.5));
    // The same under a locale writing decimal commas (the app runs under Polish).
    const char* before = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = before ? before : "C";
    if (std::setlocale(LC_NUMERIC, "pl_PL.UTF-8")) {
        CHECK(threadCallout(nearestThread(ThreadStandard::Metric, 4.0)) == "M4×0.7");
        CHECK(threadCallout(odd) == "M7.5×1");
        std::setlocale(LC_NUMERIC, saved.c_str());
    }
}

Feature threadOn(const gp_Pnt& at, const gp_Dir& normal, const ThreadSpec& spec)
{
    Feature f;
    f.kind = Feature::Kind::Thread;
    f.faces = {{at, normal}};
    f.thread = spec;
    return f;
}

void testCosmeticThreadOnABoss()
{
    Model m;
    plateWithBoss(m);
    const double before = totalVolume(m.build().bodies);
    // M6 on the boss (r 3), picked near its top.
    m.add(threadOn(gp_Pnt(3, 0, 20), gp::DX(), nearestThread(ThreadStandard::Metric, 6.0)));
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(near(totalVolume(r.bodies), before, 1e-6)); // cosmetic: the boss stays smooth
    const std::vector<ThreadMark> marks = r.context.allThreads();
    CHECK(marks.size() == 1);
    if (marks.size() == 1) {
        const ThreadMark& t = marks[0];
        CHECK(!t.internal);
        CHECK(near(t.radius, 3.0, 1e-9));
        CHECK(near(t.length, 12.0, 1e-9));
        // From the free end, down toward the plate.
        CHECK(t.axis.Location().Distance(gp_Pnt(0, 0, 22)) < 1e-6);
        CHECK(t.axis.Direction().IsEqual(-gp::DZ(), 1e-9));
        CHECK(t.openStart && !t.openEnd);
    }

    // 8 long: still from the top.
    m.history[2].thread.length = 8.0;
    const auto shorter = m.build().context.allThreads();
    CHECK(shorter.size() == 1 && near(shorter[0].length, 8.0));

    // M12 doesn't fit a boss 6 across, nor does M2.
    m.history[2].thread = nearestThread(ThreadStandard::Metric, 12.0);
    CHECK(!m.build().errors.at(2).empty());
    m.history[2].thread = nearestThread(ThreadStandard::Metric, 2.0);
    CHECK(!m.build().errors.at(2).empty());
    // A flat face takes no thread.
    m.history[2] = threadOn(gp_Pnt(0, 0, 22), gp::DZ(), nearestThread(ThreadStandard::Metric, 6.0));
    CHECK(!m.build().errors.at(2).empty());
}

void testModelledThreadCutsAGroove()
{
    Model m;
    plateWithBoss(m, 3.0, 6.0);
    const double before = totalVolume(m.build().bodies);
    ThreadSpec spec = nearestThread(ThreadStandard::Metric, 6.0);
    spec.modelled = true;
    m.add(threadOn(gp_Pnt(3, 0, 13), gp::DX(), spec));
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(allValid(r.bodies));
    // The groove: about a (17/24 H deep) triangle around each of the 6 turns.
    const double removed = before - totalVolume(r.bodies);
    const double h = 1.0 * std::sqrt(3.0) / 2.0, depth = 17.0 / 24.0 * h;
    const double area = depth * depth * std::tan(kPi / 6.0);
    const double estimate = area * 2.0 * kPi * (3.0 - depth / 3.0) * 6.0;
    CHECK(removed > 0.6 * estimate && removed < 1.4 * estimate);
    CHECK(r.context.allThreads().size() == 1);
}

Feature tappedHole(const gp_Pnt& at, const ThreadSpec& spec, double depth)
{
    Feature f;
    f.kind = Feature::Kind::Hole;
    f.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
    f.points = {at};
    f.hole.tapped = true;
    f.hole.diameter = spec.diameter; // the thread's size
    f.hole.thread = spec;
    f.hole.depth = depth;
    return f;
}

void testTappedHoles()
{
    Model m;
    rectangle(m.sketch(1, kGround), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
    const double plate = 40.0 * 40.0 * 10.0;
    // M8, 6 deep: drilled 6.8.
    m.add(tappedHole(gp_Pnt(5, 5, 10), nearestThread(ThreadStandard::Metric, 8.0), 6.0));
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(near(totalVolume(r.bodies), plate - kPi * 3.4 * 3.4 * 6.0, 1e-3));
    std::vector<ThreadMark> marks = r.context.allThreads();
    CHECK(marks.size() == 1);
    if (marks.size() == 1) {
        CHECK(marks[0].internal);
        CHECK(near(marks[0].radius, 3.4, 1e-9));
        CHECK(near(marks[0].length, 6.0, 1e-6));
        CHECK(marks[0].axis.Location().Distance(gp_Pnt(5, 5, 10)) < 1e-6); // from the mouth
        CHECK(marks[0].axis.Direction().IsEqual(-gp::DZ(), 1e-9));
        CHECK(marks[0].openStart && !marks[0].openEnd);
    }

    // Through: threaded all the way, open at both ends.
    m.history[1].hole.depth = 0.0;
    marks = m.build().context.allThreads();
    CHECK(marks.size() == 1 && near(marks[0].length, 10.0, 1e-6) && marks[0].openEnd);

    // A pattern of the tapped hole repeats its threads.
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.axis.kind = AxisRef::Kind::Datum;
    pattern.axis.datum = kBaseX;
    pattern.count = 3;
    pattern.value = -10.0;
    pattern.source = m.history[1].id;
    m.add(pattern);
    RebuildResult patterned = m.build();
    CHECK(patterned.errors.at(2).empty());
    CHECK(patterned.context.allThreads().size() == 3);

    // Modelled, blind 5 deep in M6: the grooves cut more than the drill alone.
    Model n;
    rectangle(n.sketch(1, kGround), -20, -20, 20, 20);
    n.add(extrudeAt(1, {0, 0}, 10.0));
    ThreadSpec m6 = nearestThread(ThreadStandard::Metric, 6.0);
    n.add(tappedHole(gp_Pnt(0, 0, 10), m6, 5.0));
    const double drilled = totalVolume(n.build().bodies);
    n.history[1].hole.thread.modelled = true;
    RebuildResult modelled = n.build();
    CHECK(modelled.errors.at(1).empty());
    CHECK(allValid(modelled.bodies));
    CHECK(totalVolume(modelled.bodies) < drilled - 1.0);
}

// --- Draft, ribs ----------------------------------------------------------------------

void testDraft()
{
    // A 20 × 20 × 20 block; its four sides drafted 5° about the ground.
    Model m;
    rectangle(m.sketch(1, kGround), -10, -10, 10, 10);
    m.add(extrudeAt(1, {0, 0}, 20.0));
    Feature draft;
    draft.kind = Feature::Kind::Draft;
    draft.faces = {{gp_Pnt(10, 0, 10), gp::DX()},
                   {gp_Pnt(-10, 0, 10), -gp::DX()},
                   {gp_Pnt(0, 10, 10), gp::DY()},
                   {gp_Pnt(0, -10, 10), -gp::DY()}};
    draft.plane.kind = PlaneRef::Kind::Datum;
    draft.plane.datum = kBaseXY;
    draft.value = 5.0;
    m.add(draft);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(allValid(r.bodies));
    // Narrower going up: the top is 20 − 2 · 20 tan 5° across.
    const double top = 20.0 - 2.0 * 20.0 * std::tan(5.0 * kPi / 180.0);
    const double expected = 20.0 / 3.0 * (400.0 + top * top + 20.0 * top); // a frustum
    if (!r.bodies.empty()) {
        CHECK(near(volumeOf(r.bodies[0]), expected, 1e-2));
        CHECK(inside(r.bodies[0], gp_Pnt(9.9, 0, 0.5)));
        CHECK(!inside(r.bodies[0], gp_Pnt(9.9, 0, 19.5)));
    }
    // Negative: wider going up.
    m.history[1].value = -5.0;
    RebuildResult wider = m.build();
    CHECK(wider.errors.at(1).empty());
    CHECK(totalVolume(wider.bodies) > 8000.0);
    // Edited back to nothing: refused.
    m.history[1].value = 0.0;
    CHECK(!m.build().errors.at(1).empty());
}

void testRibFillsTheCorner()
{
    // An L: a plate (y 0..40, z 0..4) and a wall (y 0..4, z 4..34), 40 long in X.
    Model m;
    rectangle(m.sketch(1, kGround), -20, 0, 20, 40);
    m.add(extrudeAt(1, {0, 20}, 4.0));
    rectangle(m.sketch(2, gp_Ax3(gp_Pnt(0, 0, 4), gp::DZ(), gp::DX())), -20, 0, 20, 4);
    Feature wall = extrudeAt(2, {0, 2}, 30.0);
    wall.operation = BodyOperation::Join;
    m.add(wall);
    const double before = totalVolume(m.build().bodies);

    // The rib's line on the plane x = 0 (sketch x along Y, y along Z): from the wall at
    // z 24 down to the plate at y 24.
    Sketch& s = m.sketch(3, gp_Ax3(gp::Origin(), gp::DX(), gp::DY()));
    const PointId upper = s.addPoint({4, 24}), lower = s.addPoint({24, 4});
    const EntityId line = s.addLine(upper, lower);
    Feature rib;
    rib.kind = Feature::Kind::Rib;
    rib.pathSketch = 3;
    rib.pathEntity = line;
    rib.value = 3.0;
    m.add(rib);
    RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(allValid(r.bodies));
    // The triangle between the line, the wall and the plate (20 × 20 / 2), 3 thick.
    CHECK(near(totalVolume(r.bodies) - before, 200.0 * 3.0, 1e-2));
    if (!r.bodies.empty())
        CHECK(inside(r.bodies[0], gp_Pnt(0, 8, 8)));

    // A line that stops short of the walls is carried on to them.
    s.setPos(upper, {8, 20});
    s.setPos(lower, {20, 8});
    RebuildResult carried = m.build();
    CHECK(carried.errors.at(2).empty());
    CHECK(near(totalVolume(carried.bodies) - before, 200.0 * 3.0, 1e-2));

    // Thicker: follows.
    m.history[2].value = 5.0;
    CHECK(near(totalVolume(m.build().bodies) - before, 200.0 * 5.0, 1e-2));
}

// --- Emboss, engrave --------------------------------------------------------------------

void testEmbossOnAFlatFace()
{
    Model m;
    rectangle(m.sketch(1, kGround), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
    // A 10 × 4 label drawn on the ground, projected up onto the top.
    rectangle(m.sketch(2, kGround), -5, -2, 5, 2);
    Feature emboss;
    emboss.kind = Feature::Kind::Emboss;
    emboss.regions = {{2, {0, 0}}};
    emboss.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
    emboss.value = 1.0;
    emboss.operation = BodyOperation::Join;
    m.add(emboss);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 16000.0 + 40.0, 1e-3));
    // Engraved instead.
    m.history[1].operation = BodyOperation::Cut;
    RebuildResult engraved = m.build();
    CHECK(engraved.errors.at(1).empty());
    CHECK(near(totalVolume(engraved.bodies), 16000.0 - 40.0, 1e-3));
}

void testEngraveOnACylinder()
{
    // A cylinder r 10, 30 high, standing on the ground.
    Model m;
    Sketch& base = m.sketch(1, kGround);
    base.addCircle(base.addPoint({0, 0}), 10.0);
    m.add(extrudeAt(1, {0, 0}, 30.0));
    const double before = totalVolume(m.build().bodies);
    // A 6 × 4 patch on a plane beside it (x = 15), projected onto the round side.
    rectangle(m.sketch(2, gp_Ax3(gp_Pnt(15, 0, 0), gp::DX(), gp::DY())), -3, 13, 3, 17); // x along Y, y up
    Feature engrave;
    engrave.kind = Feature::Kind::Emboss;
    engrave.regions = {{2, {0, 15}}};
    engrave.faces = {{gp_Pnt(10, 0, 15), gp::DX()}};
    engrave.value = 0.5;
    engrave.operation = BodyOperation::Cut;
    m.add(engrave);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(allValid(r.bodies));
    const double removed = before - totalVolume(r.bodies);
    // About 6 × 4 × 0.5 (the patch follows the curve), on the near side only.
    CHECK(removed > 10.0 && removed < 14.0);
    if (!r.bodies.empty()) {
        CHECK(!inside(r.bodies[0], gp_Pnt(9.8, 0, 15)));
        CHECK(inside(r.bodies[0], gp_Pnt(-9.8, 0, 15)));
    }
    // Embossed: grows by about as much.
    m.history[1].operation = BodyOperation::Join;
    RebuildResult raised = m.build();
    CHECK(raised.errors.at(1).empty());
    const double added = totalVolume(raised.bodies) - before;
    CHECK(added > 10.0 && added < 14.0);
}

// --- Varying fillets ------------------------------------------------------------------

void testVaryingFillet()
{
    // A 20 × 20 × 20 block; its top front edge (along X at y −10, z 20) rounded from 1 to 4.
    Model m;
    rectangle(m.sketch(1, kGround), -10, -10, 10, 10);
    m.add(extrudeAt(1, {0, 0}, 20.0));
    Feature fillet;
    fillet.kind = Feature::Kind::Fillet;
    fillet.edges = {{gp_Pnt(0, -10, 20), gp::DX()}}; // picked toward +X
    fillet.value = 1.0;
    fillet.endValue = 4.0;
    m.add(fillet);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 1 && allValid(r.bodies));
    // Near x −10 the round is small: a point 0.6 in from both faces stays; near x 10 it's gone.
    if (!r.bodies.empty()) {
        CHECK(inside(r.bodies[0], gp_Pnt(-9.5, -9.4, 19.4)));
        CHECK(!inside(r.bodies[0], gp_Pnt(9.5, -9.4, 19.4)));
    }
    // Picked the other way round: the big end moves to x −10.
    m.history[1].edges[0].direction = -gp::DX();
    RebuildResult flipped = m.build();
    CHECK(flipped.errors.at(1).empty());
    if (!flipped.bodies.empty()) {
        CHECK(!inside(flipped.bodies[0], gp_Pnt(-9.5, -9.4, 19.4)));
        CHECK(inside(flipped.bodies[0], gp_Pnt(9.5, -9.4, 19.4)));
    }
    // An end radius of 0 (or the same): a constant round.
    m.history[1].endValue = 0.0;
    const double constant = totalVolume(m.build().bodies);
    CHECK(near(constant, 8000.0 - (1.0 - kPi / 4.0) * 20.0, 1e-3));
}

// A 20 × 20 × 20 block with its top and upright edges rounded (r 2): the four top
// corners each join three fillets.
Model roundedBlock(double setback)
{
    Model m;
    rectangle(m.sketch(1, kGround), -10, -10, 10, 10);
    m.add(extrudeAt(1, {0, 0}, 20.0));
    Feature fillet;
    fillet.kind = Feature::Kind::Fillet;
    fillet.edges = {{gp_Pnt(0, -10, 20), gp::DX()}, {gp_Pnt(0, 10, 20), gp::DX()},
                    {gp_Pnt(-10, 0, 20), gp::DY()}, {gp_Pnt(10, 0, 20), gp::DY()},
                    {gp_Pnt(-10, -10, 10), gp::DZ()}, {gp_Pnt(10, -10, 10), gp::DZ()},
                    {gp_Pnt(-10, 10, 10), gp::DZ()}, {gp_Pnt(10, 10, 10), gp::DZ()}};
    fillet.value = 2.0;
    fillet.setback = setback;
    m.add(fillet);
    return m;
}

void testSetbackCorners()
{
    const RebuildResult plain = roundedBlock(0.0).build();
    CHECK(plain.errors.at(1).empty() && allValid(plain.bodies));
    const double usual = totalVolume(plain.bodies);

    Model m = roundedBlock(5.0);
    const RebuildResult r = m.build();
    if (!r.errors.at(1).empty())
        std::printf("  %s\n", r.errors.at(1).c_str());
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 1 && allValid(r.bodies));
    // A softer corner: a little less material than the usual ball corner, the rest alike.
    const double set = totalVolume(r.bodies);
    CHECK(set < usual - 1.0 && set > usual - 4 * 10.0);
    if (!r.bodies.empty()) {
        CHECK(inside(r.bodies[0], gp_Pnt(0, 0, 19.9)));      // the top is untouched
        CHECK(inside(r.bodies[0], gp_Pnt(9.9, 0, 10)));      // so are the sides
        CHECK(inside(plain.bodies[0], gp_Pnt(9.1, 9.1, 19.1)));
        CHECK(!inside(r.bodies[0], gp_Pnt(9.1, 9.1, 19.1))); // the corner is rounder
        int smooth = 0; // one patch per corner, no ball pieces left
        for (TopExp_Explorer it(r.bodies[0], TopAbs_FACE); it.More(); it.Next()) {
            if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() == GeomAbs_BSplineSurface)
                ++smooth;
        }
        CHECK(smooth == 4);
    }
    // With a radius varying along the edges too.
    m.history[1].endValue = 3.0;
    const RebuildResult varying = m.build();
    CHECK(varying.errors.at(1).empty() && allValid(varying.bodies));
    // Too far: the setback reaches past the edges' other ends.
    m.history[1].endValue = 0.0;
    m.history[1].setback = 15.0;
    CHECK(!m.build().errors.at(1).empty());
    // A parameter drives it, and it is saved with the project.
    CHECK(featureField(m.history[1], "setback").has_value());
    setFeatureValue(m.history[1], "setback", 4.0);
    CHECK(near(*featureValue(m.history[1], "setback"), 4.0));
    ProjectData project;
    project.features = m.history;
    const auto loaded = projectFromJson(projectToJson(project));
    CHECK(loaded && loaded->features.size() == 2 && near(loaded->features[1].setback, 4.0));
}

// --- Text ----------------------------------------------------------------------------

void testTextInASketch()
{
    Model m;
    Sketch& s = m.sketch(1, kGround);
    const PointId at = s.addPoint({0, 0});
    const EntityId text = s.addText(at, "NIB", 10.0);
    CHECK(s.text(text) && s.text(text)->text == "NIB");
    // Letters bound regions: N, I and B, and B's two counters.
    const std::vector<TopoDS_Face> regions = findRegions(s);
    CHECK(regions.size() >= 5);
    const auto box = textBounds(s, *s.text(text));
    CHECK(box.has_value());
    if (box) {
        CHECK(box->first.X() >= -0.5 && box->second.X() > 10.0 && box->second.X() < 40.0);
        CHECK(box->second.Y() > 5.0 && box->second.Y() < 11.0); // capitals: about 0.7 of the size
    }
    CHECK(textAt(s, gp_Pnt2d(3, 3), 0.1) == text);
    CHECK(!textAt(s, gp_Pnt2d(-10, 3), 0.1));
    CHECK(!tessellate(s).segments.empty());

    // The letters follow their point.
    s.setPos(at, {100, 0});
    const auto moved = textBounds(s, *s.text(text));
    CHECK(moved && moved->first.X() > 99.0);
    s.setPos(at, {0, 0});

    // Extruding the I (its region holds the point in the middle of its stroke).
    const auto i = regionAt(regions, gp_Pnt(box->first.X() + (box->second.X() - box->first.X()) / 2.0, 3.0, 0.0));
    CHECK(i.has_value());

    // The point stays while the text does; removing the text takes it.
    s.removeEntities({});
    CHECK(s.point(at) != nullptr);
    s.removeTexts({text});
    CHECK(s.texts().empty() && s.point(at) == nullptr);

    // Nothing to show: no outline, no region.
    Sketch empty(kGround);
    empty.addText(empty.addPoint({0, 0}), "", 10.0);
    CHECK(findRegions(empty).empty());
    CHECK(!fontFamilies().empty());
}

void testEngravedLabel()
{
    // "8" engraved 0.5 deep into the top of a plate, from a sketch on the top.
    Model m;
    rectangle(m.sketch(1, kGround), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
    Sketch& label = m.sketch(2, gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX()));
    label.addText(label.addPoint({-5, -5}), "L", 12.0);
    const std::vector<TopoDS_Face> regions = findRegions(label);
    CHECK(regions.size() == 1);
    if (regions.size() != 1)
        return;
    Feature engrave;
    engrave.kind = Feature::Kind::Emboss;
    engrave.regions = {{2, *insidePoint(label, regions[0])}};
    engrave.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
    engrave.value = 0.5;
    engrave.operation = BodyOperation::Cut;
    m.add(engrave);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    double area = 0.0;
    {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(regions[0], props);
        area = props.Mass();
    }
    CHECK(area > 10.0);
    CHECK(near(totalVolume(r.bodies), 16000.0 - 0.5 * area, 1e-3));
}

void testThreadsInDrawings()
{
    // A tapped M8 hole through a plate: seen from the top, its circle is called out and
    // the thread's major diameter drawn thin (three quarters round); from the front, two
    // thin lines along the hole.
    Model m;
    rectangle(m.sketch(1, kGround), -20, -20, 20, 20);
    m.add(extrudeAt(1, {0, 0}, 10.0));
    m.add(tappedHole(gp_Pnt(0, 0, 10), nearestThread(ThreadStandard::Metric, 8.0), 0.0));
    RebuildResult r = m.build();
    const std::vector<ThreadMark> threads = r.context.allThreads();
    CHECK(threads.size() == 1);
    SheetView top;
    top.kind = ViewKind::Top;
    const ViewGeometry above = projectView(r.bodies, top, threads);
    bool called = false;
    for (const ViewGeometry::Circle& c : above.circles)
        called = called || (c.thread == "M8×1.25" && near(c.radius, 3.4, 1e-6));
    CHECK(called);
    CHECK(above.threads.size() == 1);
    if (above.threads.size() == 1) {
        double farthest = 0.0;
        for (const gp_Pnt2d& p : above.threads[0])
            farthest = std::max(farthest, p.Distance(projectPoint(ViewKind::Top, gp_Pnt(0, 0, 10))));
        CHECK(near(farthest, 4.0, 1e-6)); // the major radius
    }
    SheetView front;
    front.kind = ViewKind::Front;
    CHECK(projectView(r.bodies, front, threads).threads.size() == 2);
    // Without threads: nothing extra.
    CHECK(projectView(r.bodies, top).threads.empty());
}

// What M16 is done by: a 3D-printed enclosure with threaded screw bosses, a ribbed
// wall, draft and an engraved label, all applying together and editable afterwards.
void testEnclosure()
{
    Model m;
    rectangle(m.sketch(1, kGround), -40, -30, 40, 30);
    m.add(extrudeAt(1, {0, 0}, 30.0)); // 0
    Feature shell;
    shell.kind = Feature::Kind::Shell;
    shell.faces = {{gp_Pnt(0, 0, 30), gp::DZ()}};
    shell.value = 2.0;
    m.add(shell); // 1
    Feature draft;
    draft.kind = Feature::Kind::Draft;
    draft.faces = {{gp_Pnt(40, 0, 15), gp::DX()},
                   {gp_Pnt(-40, 0, 15), -gp::DX()},
                   {gp_Pnt(0, 30, 15), gp::DY()},
                   {gp_Pnt(0, -30, 15), -gp::DY()}};
    draft.plane.kind = PlaneRef::Kind::Datum;
    draft.plane.datum = kBaseXY;
    draft.value = 2.0;
    m.add(draft); // 2
    // Bosses r 4 on the floor (z 2), 20 high.
    Sketch& floor = m.sketch(2, gp_Ax3(gp_Pnt(0, 0, 2), gp::DZ(), gp::DX()));
    const gp_Pnt2d corners[] = {{32, 22}, {-32, 22}, {-32, -22}, {32, -22}};
    for (const gp_Pnt2d& c : corners)
        floor.addCircle(floor.addPoint(c), 4.0);
    Feature bosses;
    bosses.kind = Feature::Kind::Extrude;
    for (const gp_Pnt2d& c : corners)
        bosses.regions.push_back({2, c});
    bosses.value = 20.0;
    bosses.operation = BodyOperation::Join;
    m.add(bosses); // 3
    // M3 tapped into each, 8 deep.
    Feature holes = tappedHole(gp_Pnt(32, 22, 22), nearestThread(ThreadStandard::Metric, 3.0), 8.0);
    holes.faces = {{gp_Pnt(32, 22, 22), gp::DZ()}};
    holes.points = {gp_Pnt(32, 22, 22), gp_Pnt(-32, 22, 22), gp_Pnt(-32, -22, 22), gp_Pnt(32, -22, 22)};
    m.add(holes); // 4
    // A rib from the floor up the back wall (inner face y 28), on the plane x = 0.
    Sketch& ribSketch = m.sketch(3, gp_Ax3(gp::Origin(), gp::DX(), gp::DY()));
    const EntityId ribLine = ribSketch.addLine(ribSketch.addPoint({18, 2}), ribSketch.addPoint({28, 14}));
    Feature rib;
    rib.kind = Feature::Kind::Rib;
    rib.pathSketch = 3;
    rib.pathEntity = ribLine;
    rib.value = 2.0;
    m.add(rib); // 5
    // "NIB" engraved 0.6 deep into the front wall, from a sketch in front of it.
    Sketch& label = m.sketch(4, gp_Ax3(gp_Pnt(0, -45, 0), -gp::DY(), gp::DX()));
    label.addText(label.addPoint({-12, 8}), "NIB", 10.0);
    Feature engrave;
    engrave.kind = Feature::Kind::Emboss;
    for (const TopoDS_Face& region : findRegions(label)) {
        if (const auto inside = insidePoint(label, region); inside && textContains(label, label.texts()[0], *inside))
            engrave.regions.push_back({4, *inside});
    }
    engrave.faces = {{gp_Pnt(0, -29.65, 10), gp_Dir(0, -1, 0.035)}};
    engrave.value = 0.6;
    engrave.operation = BodyOperation::Cut;
    m.add(engrave); // 6

    RebuildResult r = m.build();
    for (std::size_t i = 0; i < r.errors.size(); ++i) {
        if (!r.errors[i].empty())
            std::printf("  feature %zu: %s\n", i, r.errors[i].c_str());
        CHECK(r.errors[i].empty());
    }
    CHECK(r.bodies.size() == 1);
    CHECK(allValid(r.bodies));
    CHECK(engrave.regions.size() == 3);
    CHECK(r.context.allThreads().size() == 4);

    // Editable afterwards: a thicker rib, M4 instead of M3, a deeper label.
    const double before = totalVolume(r.bodies);
    m.history[5].value = 3.0;
    m.history[4].hole.diameter = 4.0;
    m.history[6].value = 1.0;
    RebuildResult edited = m.build();
    for (const std::string& e : edited.errors)
        CHECK(e.empty());
    CHECK(edited.bodies.size() == 1);
    CHECK(std::abs(totalVolume(edited.bodies) - before) > 1.0);
    const auto threads = edited.context.allThreads();
    CHECK(threads.size() == 4 && threadCallout(threads[0].spec) == "M4×0.7");

    if (const char* path = std::getenv("NIBFORM_SAVE_ENCLOSURE")) {
        ProjectData project;
        for (const auto& [id, sketch] : m.sketches)
            project.sketches.push_back({id, sketch});
        project.features = m.history;
        CHECK(saveProject(QString::fromLocal8Bit(path), project).isEmpty());
    }
}

} // namespace

int main()
{
    return runTests({
        {"thread sizes and callouts", testThreadSizes},
        {"a cosmetic thread on a boss", testCosmeticThreadOnABoss},
        {"a modelled thread cuts a groove", testModelledThreadCutsAGroove},
        {"tapped holes", testTappedHoles},
        {"draft", testDraft},
        {"a rib fills the corner", testRibFillsTheCorner},
        {"emboss on a flat face", testEmbossOnAFlatFace},
        {"engrave on a cylinder", testEngraveOnACylinder},
        {"a fillet of varying radius", testVaryingFillet},
        {"setback corners", testSetbackCorners},
        {"text in a sketch", testTextInASketch},
        {"an engraved label", testEngravedLabel},
        {"threads in drawings", testThreadsInDrawings},
        {"an enclosure", testEnclosure},
    });
}
