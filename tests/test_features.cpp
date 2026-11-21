// Parametric history: features replayed over changed sketches, and exports.

#include "Check.h"

#include "model/Export.h"
#include "model/Features.h"
#include "model/Profiles.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

namespace {

struct Rect
{
    PointId a, b, c, d;
};

Rect rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    Rect r{s.addPoint({x0, y0}), s.addPoint({x1, y0}), s.addPoint({x1, y1}), s.addPoint({x0, y1})};
    s.addLine(r.a, r.b);
    s.addLine(r.b, r.c);
    s.addLine(r.c, r.d);
    s.addLine(r.d, r.a);
    return r;
}

double totalVolume(const std::vector<TopoDS_Shape>& bodies)
{
    double v = 0.0;
    for (const TopoDS_Shape& b : bodies)
        v += volumeOf(b);
    return v;
}

Feature extrudeAt(int sketch, gp_Pnt2d inside, double distance)
{
    Feature f;
    f.kind = Feature::Kind::Extrude;
    f.regions = {{sketch, inside}};
    f.value = distance;
    return f;
}

// The top front edge (y = 0, z = top) of a box standing on the origin.
EdgePick topFrontEdge(double width, double height)
{
    return {gp_Pnt(width / 2.0, 0, height), gp::DX()};
}

void testChangingTheSketchRebuildsTheBody()
{
    std::map<int, Sketch> sketches;
    const Rect r = rectangle(sketches[1], 0, 0, 10, 6);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    const std::vector<Feature> history = {extrudeAt(1, {5, 3}, 5.0)};

    RebuildResult first = rebuild(lookup, history);
    CHECK(first.bodies.size() == 1 && first.errors.at(0).empty());
    CHECK(near(totalVolume(first.bodies), 300.0, 1e-6));

    // Twice as wide: the region is still found through the same inside point.
    sketches[1].setPos(r.b, {20, 0});
    sketches[1].setPos(r.c, {20, 6});
    RebuildResult wider = rebuild(lookup, history);
    CHECK(near(totalVolume(wider.bodies), 600.0, 1e-6));
}

void testFacesAndEdgesAreFoundAgain()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 10, 6);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };

    std::vector<Feature> history = {extrudeAt(1, {5, 3}, 5.0)};
    Feature pull;
    pull.kind = Feature::Kind::Extrude;
    pull.faces = {{gp_Pnt(5, 3, 5), gp::DZ()}}; // the top face, as picked
    pull.value = 2.0;
    history.push_back(pull);
    Feature round;
    round.kind = Feature::Kind::Fillet;
    round.edges = {topFrontEdge(10, 7)};
    round.value = 1.0;
    history.push_back(round);

    const double rounding = (1.0 - M_PI / 4.0) * 10.0;
    RebuildResult result = rebuild(lookup, history);
    CHECK(result.bodies.size() == 1);
    for (const std::string& e : result.errors)
        CHECK(e.empty());
    CHECK(near(totalVolume(result.bodies), 420.0 - rounding, 1e-6));

    // Taller base: the top face moved up, the pull and the rounded edge follow it.
    history[0].value = 8.0;
    result = rebuild(lookup, history);
    for (const std::string& e : result.errors)
        CHECK(e.empty());
    CHECK(near(totalVolume(result.bodies), 600.0 - rounding, 1e-6));
}

void testMissingInputIsReportedAndSkipped()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 10, 6);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    const std::vector<Feature> history = {
        extrudeAt(1, {5, 3}, 5.0),
        extrudeAt(1, {50, 50}, 5.0), // nothing closed there
        extrudeAt(7, {5, 3}, 5.0),   // no such sketch
    };
    RebuildResult result = rebuild(lookup, history);
    CHECK(result.errors.size() == 3);
    CHECK(result.errors[0].empty() && !result.errors[1].empty() && !result.errors[2].empty());
    CHECK(near(totalVolume(result.bodies), 300.0, 1e-6));
}

void testBodiesFoundByInsidePoints()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 10, 10);
    rectangle(sketches[2], 5, 5, 15, 15);
    rectangle(sketches[3], 30, 0, 32, 2);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };

    std::vector<Feature> history;
    for (int id : {1, 2, 3}) {
        Feature f = extrudeAt(id, id == 3 ? gp_Pnt2d(31, 1) : gp_Pnt2d(id == 1 ? 1 : 14, id == 1 ? 1 : 14), 10.0);
        f.operation = BodyOperation::NewBody;
        history.push_back(f);
    }
    Feature minus;
    minus.kind = Feature::Kind::Combine;
    minus.boolean = BooleanKind::Subtract;
    minus.bodies = {gp_Pnt(1, 1, 5), gp_Pnt(14, 14, 5)};
    history.push_back(minus);
    Feature drop;
    drop.kind = Feature::Kind::Delete;
    drop.bodies = {gp_Pnt(31, 1, 5)};
    history.push_back(drop);

    RebuildResult result = rebuild(lookup, history);
    for (const std::string& e : result.errors)
        CHECK(e.empty());
    CHECK(result.bodies.size() == 1);
    CHECK(near(totalVolume(result.bodies), 750.0, 1e-6));
}

void testShellFollowsTheBody()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 10, 6);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    std::vector<Feature> history = {extrudeAt(1, {5, 3}, 5.0)};
    Feature shell;
    shell.kind = Feature::Kind::Shell;
    shell.faces = {{gp_Pnt(5, 3, 5), gp::DZ()}};
    shell.value = 1.0;
    history.push_back(shell);

    RebuildResult result = rebuild(lookup, history);
    CHECK(result.errors.at(1).empty());
    CHECK(near(totalVolume(result.bodies), 172.0, 1e-6));
    // A taller box: still open at its (moved) top.
    history[0].value = 9.0;
    result = rebuild(lookup, history);
    CHECK(result.errors.at(1).empty());
    CHECK(near(totalVolume(result.bodies), 540.0 - 8.0 * 4.0 * 8.0, 1e-6));
}

void testInsidePointAvoidsHoles()
{
    Sketch s;
    rectangle(s, 0, 0, 10, 10);
    s.addCircle(s.addPoint({5, 5}), 3.0); // the middle of the frame is the hole
    for (const TopoDS_Face& region : findRegions(s)) {
        const auto p = insidePoint(s, region);
        CHECK(p.has_value());
        if (p)
            CHECK(regionAt({region}, s.to3d(*p)).has_value());
    }
}

void testStepAndStlExport()
{
    Sketch s;
    rectangle(s, 0, 0, 10, 6);
    std::vector<Profile> profiles;
    for (const TopoDS_Face& f : findRegions(s))
        profiles.push_back({f, gp::DZ(), -1});
    const std::vector<TopoDS_Shape> bodies = extrude({}, profiles, 5.0, BodyOperation::Auto).bodies;

    const auto dir = std::filesystem::temp_directory_path();
    const std::string step = (dir / "nibform_test_export.step").string();
    const std::string stl = (dir / "nibform_test_export.stl").string();

    CHECK(exportStep(bodies, step).empty());
    const auto back = importStep(step);
    CHECK(back.size() == 1 && near(totalVolume(back), 300.0, 1e-6));

    CHECK(exportStl(bodies, stl).empty());
    const auto size = std::filesystem::file_size(stl);
    CHECK(size > 84 && (size - 84) % 50 == 0); // binary STL: header + 50 bytes a triangle

    CHECK(!exportStep({}, step).empty());
    std::filesystem::remove(step);
    std::filesystem::remove(stl);
}

} // namespace

// An extrusion that starts away from its sketch: inside the part (a closed pocket), off
// the ground (a body floating above), off a body's face (a separate body).
void testExtrudeFromAnOffset()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 20, 20);
    sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(0, 0, 20), gp::DZ(), gp::DX())));
    sketches.at(2).addCircle(sketches.at(2).addPoint({10, 10}), 3.0);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    std::vector<Feature> history = {extrudeAt(1, {10, 10}, 20.0), extrudeAt(2, {10, 10}, 10.0)}; // a 20 cube
    history[1].startOffset = -15.0; // from z 5 up to z 15, all inside: Automatic cuts
    auto build = [&] {
        numberFeatures(history);
        return rebuild(lookup, history);
    };
    RebuildResult r = build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 8000.0 - M_PI * 9.0 * 10.0, 1e-3));

    // Pulled the other way from the same start: z 5 down to z −5, half of it outside.
    history[1].value = -10.0;
    r = build();
    CHECK(r.errors.at(1).empty());
    CHECK(near(totalVolume(r.bodies), 8000.0 - M_PI * 9.0 * 5.0, 1e-3));

    // Up from 30 above the ground: a new body, not touching the cube.
    history[1] = extrudeAt(1, {10, 10}, 5.0);
    history[1].startOffset = 30.0;
    r = build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 2 && near(totalVolume(r.bodies), 10000.0, 1e-3));

    // A body's top face pulled from 5 above it: a separate body again.
    Feature face;
    face.kind = Feature::Kind::Extrude;
    face.faces = {{gp_Pnt(10, 10, 20), gp::DZ()}};
    face.value = 5.0;
    face.startOffset = 5.0;
    history[1] = face;
    r = build();
    CHECK(r.errors.at(1).empty());
    CHECK(r.bodies.size() == 2 && near(totalVolume(r.bodies), 10000.0, 1e-3));
}

// A sketch drawn on a face rides along when an earlier feature moves that face.
void testSketchOnAFaceFollowsIt()
{
    std::map<int, Sketch> sketches;
    rectangle(sketches[1], 0, 0, 20, 20);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    std::vector<Feature> history = {extrudeAt(1, {10, 10}, 20.0)};
    RebuildResult r = rebuild(lookup, history);

    // Drawn on the top face, as the editor makes it: centered there, facing up.
    sketches.emplace(2, Sketch(gp_Ax3(gp_Pnt(10, 10, 20), gp::DZ(), gp::DX())));
    sketches.at(2).addCircle(sketches.at(2).addPoint({0, 0}), 3.0);
    CHECK(!planeUnder(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX()), r.bodies, r.context)); // a base plane
    const auto under = planeUnder(sketches.at(2).plane(), r.bodies, r.context);
    CHECK(under && under->kind == PlaneRef::Kind::Face);
    Feature entry;
    entry.kind = Feature::Kind::Sketch;
    entry.sketch = 2;
    entry.plane = *under;
    history.push_back(entry);
    history.push_back(extrudeAt(2, {0, 0}, 10.0));
    numberFeatures(history);
    r = rebuild(lookup, history);
    CHECK(r.errors.at(2).empty() && r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 8000.0 + M_PI * 9.0 * 10.0, 1e-3));

    // The cube 30 high: the boss starts on its new top, still one body.
    history[0].value = 30.0;
    r = rebuild(lookup, history);
    CHECK(r.errors.at(1).empty() && r.errors.at(2).empty());
    CHECK(r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 12000.0 + M_PI * 9.0 * 10.0, 1e-3));
    CHECK(r.context.sketchPlanes.count(2) && near(r.context.sketchPlanes.at(2).Location().Z(), 30.0, 1e-9));
    CHECK(near(r.context.sketchPlanes.at(2).Location().X(), 10.0, 1e-9));

    // Stored where it went (as the editor does), it stays there.
    sketches.at(2).setPlane(r.context.sketchPlanes.at(2));
    r = rebuild(lookup, history);
    CHECK(samePlane(r.context.sketchPlanes.at(2), sketches.at(2).plane()));
    CHECK(near(totalVolume(r.bodies), 12000.0 + M_PI * 9.0 * 10.0, 1e-3));
}

int main()
{
    return runTests({
        {"changing the sketch rebuilds the body", testChangingTheSketchRebuildsTheBody},
        {"faces and edges are found again", testFacesAndEdgesAreFoundAgain},
        {"missing input is reported and skipped", testMissingInputIsReportedAndSkipped},
        {"bodies found by inside points", testBodiesFoundByInsidePoints},
        {"shell follows the body", testShellFollowsTheBody},
        {"inside point avoids holes", testInsidePointAvoidsHoles},
        {"STEP and STL export", testStepAndStlExport},
        {"extrude from an offset", testExtrudeFromAnOffset},
        {"sketch on a face follows it", testSketchOnAFaceFollowsIt},
    });
}
