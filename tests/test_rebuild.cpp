// Rebuilding fast (M14): replaying only from the feature that changed, with the steps
// before it cached, and rebuilding on a thread of its own.

#include "Check.h"
#include "LongModel.h"

#include "model/Rebuild.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <atomic>
#include <chrono>

namespace {

double totalVolume(const std::vector<TopoDS_Shape>& bodies)
{
    double v = 0.0;
    for (const TopoDS_Shape& b : bodies)
        v += volumeOf(b);
    return v;
}

// The cached rebuild gives what a full one does.
bool sameAsFull(const LongModel& m, const RebuildResult& cached)
{
    const RebuildResult full = rebuild(m.lookup(), m.history);
    return full.bodies.size() == cached.bodies.size() && full.errors == cached.errors
        && near(totalVolume(full.bodies), totalVolume(cached.bodies), 1e-6)
        && full.context.tools.size() == cached.context.tools.size();
}

void testReplaysFromTheChange()
{
    LongModel m = longModel(12);
    const std::size_t n = m.history.size();
    RebuildCache cache;
    auto first = cache.rebuild(m.lookup(), m.history);
    CHECK(first && sameAsFull(m, *first));
    CHECK(cache.lastStats().reused == 0 && cache.lastStats().replayed == n);

    // Nothing changed: nothing replayed.
    auto again = cache.rebuild(m.lookup(), m.history);
    CHECK(again && cache.lastStats().reused == n && cache.lastStats().replayed == 0);
    CHECK(again && sameAsFull(m, *again));

    // A late value: only from there on.
    const std::size_t late = m.pocketFeature.back();
    m.history[late].value = -6.0;
    auto deeper = cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().reused == late && cache.lastStats().replayed == n - late);
    CHECK(deeper && sameAsFull(m, *deeper));

    // Back as it was: all of it is still cached.
    m.history[late].value = -4.0;
    cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().replayed == 0);

    // An early sketch's geometry moves: from its first use on (its entry in the timeline).
    Sketch& early = m.sketches.at(m.pocketSketch[1]);
    early.setRadius(early.entities().front().id, 5.0);
    auto wider = cache.rebuild(m.lookup(), m.history);
    const std::size_t entry = m.pocketFeature[1] - 1;
    CHECK(cache.lastStats().reused == entry);
    CHECK(wider && sameAsFull(m, *wider));
    CHECK(wider && totalVolume(wider->bodies) < totalVolume(again->bodies));

    // A relation that moves nothing changes nothing to build.
    SketchConstraint fix;
    fix.kind = ConstraintKind::Fixed;
    fix.p1 = early.points().front().id;
    early.addConstraint(fix);
    cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().replayed == 0);

    // A feature taken out: the ones after it are replayed.
    m.history.erase(m.history.begin() + static_cast<long>(m.pocketFeature[5]));
    auto removed = cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().reused == m.pocketFeature[5]);
    CHECK(removed && sameAsFull(m, *removed));
}

void testEveryFieldCounts()
{
    // Changing any part of a feature replays it: the cache never shows an old result
    // for something that was edited.
    LongModel m = longModel(6);
    const std::size_t at = m.pocketFeature[3];
    RebuildCache cache;
    cache.rebuild(m.lookup(), m.history);
    const std::vector<std::pair<const char*, std::function<void(Feature&)>>> edits = {
        {"kind", [](Feature& f) { f.kind = Feature::Kind::Revolve; }},
        {"id", [](Feature& f) { f.id += 100; }},
        {"sketch", [](Feature& f) { f.sketch = 77; }},
        {"value", [](Feature& f) { f.value = std::nextafter(f.value, 0.0); }},
        {"operation", [](Feature& f) { f.operation = BodyOperation::Join; }},
        {"boolean", [](Feature& f) { f.boolean = BooleanKind::Intersect; }},
        {"region sketch", [](Feature& f) { f.regions[0].sketch = 1; }},
        {"region point", [](Feature& f) { f.regions[0].inside.SetX(f.regions[0].inside.X() + 1e-9); }},
        {"face", [](Feature& f) { f.faces.push_back({gp_Pnt(1, 2, 3), gp::DZ()}); }},
        {"edge", [](Feature& f) { f.edges.push_back({gp_Pnt(1, 2, 3), gp::DX()}); }},
        {"body", [](Feature& f) { f.bodies.push_back(gp_Pnt(1, 2, 3)); }},
        {"axis kind", [](Feature& f) { f.axis.kind = AxisRef::Kind::Datum; }},
        {"axis edge", [](Feature& f) { f.axis.edge.middle = gp_Pnt(0, 0, 1); }},
        {"axis direction", [](Feature& f) { f.axis.edge.direction = gp::DZ(); }},
        {"axis sketch", [](Feature& f) { f.axis.sketch = 3; }},
        {"axis entity", [](Feature& f) { f.axis.entity = 3; }},
        {"axis datum", [](Feature& f) { f.axis.datum = kBaseZ; }},
        {"plane kind", [](Feature& f) { f.plane.kind = PlaneRef::Kind::Face; }},
        {"plane face", [](Feature& f) { f.plane.face.middle = gp_Pnt(0, 0, 1); }},
        {"plane normal", [](Feature& f) { f.plane.face.normal = gp::DY(); }}, // gp_Dir() is X
        {"plane datum", [](Feature& f) { f.plane.datum = kBaseXY; }},
        {"path sketch", [](Feature& f) { f.pathSketch = 2; }},
        {"path entity", [](Feature& f) { f.pathEntity = 2; }},
        {"pattern", [](Feature& f) { f.pattern = Feature::PatternKind::Circular; }},
        {"count", [](Feature& f) { f.count = 3; }},
        {"source", [](Feature& f) { f.source = 2; }},
        {"hole kind", [](Feature& f) { f.hole.kind = HoleKind::Countersink; }},
        {"hole diameter", [](Feature& f) { f.hole.diameter = 6.0; }},
        {"hole depth", [](Feature& f) { f.hole.depth = 1.0; }},
        {"head diameter", [](Feature& f) { f.hole.headDiameter = 10.0; }},
        {"head depth", [](Feature& f) { f.hole.headDepth = 1.0; }},
        {"hole at", [](Feature& f) { f.holeAt.push_back({2, 1}); }},
        {"points", [](Feature& f) { f.points.push_back(gp_Pnt(1, 2, 3)); }},
        {"datum", [](Feature& f) { f.datum = Feature::DatumKind::Axis; }},
        {"matrix", [](Feature& f) { f.matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; }},
        {"copy", [](Feature& f) { f.copy = true; }},
        {"shape", [](Feature& f) { f.shape = std::make_shared<const std::string>("x"); }},
        {"label", [](Feature& f) { f.label = "part"; }},
        {"reference", [](Feature& f) { f.reference = true; }},
    };
    for (const auto& [name, edit] : edits) {
        std::vector<Feature> changed = m.history;
        edit(changed[at]);
        cache.rebuild(m.lookup(), changed);
        if (cache.lastStats().reused != at)
            std::printf("  not seen: %s\n", name);
        CHECK(cache.lastStats().reused == at);
    }

    // So does a change of the geometry of a sketch it uses, wherever it is used from (a
    // sketch of its own, which nothing before it uses).
    constexpr int kOwn = 99;
    m.sketches.emplace(kOwn, Sketch(gp_Ax3(gp_Pnt(0, 0, 10), gp::DZ(), gp::DX())));
    Sketch& own = m.sketches.at(kOwn);
    own.addLine(own.addPoint({0, 0}), own.addPoint({5, 0}));
    auto sketchEdits = [&](const char* name, const std::function<void(Feature&)>& use) {
        std::vector<Feature> changed = m.history;
        use(changed[at]);
        cache.rebuild(m.lookup(), changed);
        own.setPos(own.points().front().id, own.points().front().pos.Translated(gp_Vec2d(1e-6, 0)));
        cache.rebuild(m.lookup(), changed);
        if (cache.lastStats().reused != at)
            std::printf("  sketch not seen: %s\n", name);
        CHECK(cache.lastStats().reused == at);
    };
    sketchEdits("axis", [](Feature& f) {
        f.axis.kind = AxisRef::Kind::SketchLine;
        f.axis.sketch = kOwn;
    });
    sketchEdits("path", [](Feature& f) { f.pathSketch = kOwn; });
    sketchEdits("hole at", [](Feature& f) { f.holeAt = {{kOwn, 1}}; });
    sketchEdits("region", [](Feature& f) { f.regions.push_back({kOwn, gp_Pnt2d()}); });
    sketchEdits("entry", [](Feature& f) {
        f.kind = Feature::Kind::Sketch;
        f.sketch = kOwn;
    });
}

void testRebuildsAreRepeatable()
{
    // What the cache relies on: the same history builds the same model, every time.
    LongModel m = longModel(8);
    const RebuildResult a = rebuild(m.lookup(), m.history);
    const RebuildResult b = rebuild(m.lookup(), m.history);
    CHECK(a.errors == b.errors);
    CHECK(a.bodies.size() == b.bodies.size());
    for (std::size_t i = 0; i < a.bodies.size() && i < b.bodies.size(); ++i)
        CHECK(near(volumeOf(a.bodies[i]), volumeOf(b.bodies[i]), 1e-9));
}

void testPicksFoundOnABusyBody()
{
    // A face, an edge and the body are found again among hundreds of look-alikes: the
    // top of a plate with 30 pockets, a pocket's floor, a pocket's rim.
    LongModel m = longModel(30, 0);
    const RebuildResult r = rebuild(m.lookup(), m.history);
    CHECK(r.bodies.size() == 1);
    const auto top = findFace(r.bodies, {gp_Pnt(0, 0, 10), gp::DZ()});
    CHECK(top && near(planarNormal(top->first)->Z(), 1.0));
    if (top) {
        // The top lies at 10 and is the big face, not a pocket's floor (at 6).
        GProp_GProps props;
        BRepGProp::SurfaceProperties(top->first, props);
        CHECK(near(props.CentreOfMass().Z(), 10.0, 1e-6));
        CHECK(props.Mass() > 0.9 * 400.0 * 400.0);
    }
    // The floor of pocket 7, by its middle.
    const double x = -200.0 + 20.0 + 20.0 * 7, y = -200.0 + 20.0;
    const auto floor = findFace(r.bodies, {gp_Pnt(x, y, 6), gp::DZ()});
    CHECK(floor.has_value());
    if (floor) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(floor->first, props);
        CHECK(near(props.CentreOfMass().X(), x, 1e-6) && near(props.CentreOfMass().Y(), y, 1e-6));
        CHECK(near(props.Mass(), M_PI * 16.0, 1e-6));
    }
    // Its rim: the circle at the top, found by a point on it.
    const auto rim = findEdge(r.bodies, {gp_Pnt(x + 4.0, y, 10), gp::DY()});
    CHECK(rim.has_value());
    if (rim) {
        Bnd_Box box;
        BRepBndLib::Add(rim->edge, box);
        CHECK(near(box.CornerMin().Z(), 10.0, 1e-3) && near(box.CornerMax().X(), x + 4.0, 1e-3));
    }
    // A point in a pocket is not in the body; one beside it is.
    CHECK(!bodyContaining(r.bodies, gp_Pnt(x, y, 8)));
    CHECK(bodyContaining(r.bodies, gp_Pnt(x + 10.0, y, 8)) == 0);
    CHECK(findBody(r.bodies, gp_Pnt(x + 10.0, y, 8)) == 0);
}

void testErrorsComeFromTheCacheToo()
{
    LongModel m = longModel(4);
    // A cut whose sketch is gone fails, and keeps failing when taken from the cache.
    m.sketches.erase(m.pocketSketch[2]);
    RebuildCache cache;
    auto built = cache.rebuild(m.lookup(), m.history);
    CHECK(built && !built->errors.at(m.pocketFeature[2]).empty());
    auto cached = cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().replayed == 0);
    CHECK(cached && cached->errors == built->errors);
}

void testCancelKeepsWhatWasBuilt()
{
    LongModel m = longModel(8);
    RebuildCache cache;
    std::size_t applied = 0;
    RebuildControl control;
    control.cancelled = [&] { return applied >= 5; };
    control.progress = [&](std::size_t done, std::size_t) { applied = done; };
    CHECK(!cache.rebuild(m.lookup(), m.history, control));
    auto rest = cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().reused == 5);
    CHECK(rest && sameAsFull(m, *rest));
}

void testSmallCacheStaysRight()
{
    LongModel m = longModel(10);
    RebuildCache cache(16); // fewer steps than the history has
    for (int round = 0; round < 4; ++round) {
        m.history[m.pocketFeature[round * 2]].value = -2.0 - round;
        auto r = cache.rebuild(m.lookup(), m.history);
        CHECK(r && sameAsFull(m, *r));
        CHECK(cache.size() <= 16);
    }
}

void testImportsAreKeyedByContent()
{
    // Two copies of the same text share a key; a different text doesn't.
    LongModel m = longModel(1);
    RebuildCache cache;
    cache.rebuild(m.lookup(), m.history);
    Feature tag;
    tag.kind = Feature::Kind::Import;
    tag.label = "part.step";
    tag.shape = std::make_shared<const std::string>("not a shape");
    tag.id = 1000;
    m.history.push_back(tag);
    cache.rebuild(m.lookup(), m.history);
    m.history.back().shape = std::make_shared<const std::string>("not a shape");
    cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().replayed == 0);
    m.history.back().shape = std::make_shared<const std::string>("not a shape either");
    cache.rebuild(m.lookup(), m.history);
    CHECK(cache.lastStats().replayed == 1);
}

BackgroundRebuilder::Job jobOf(const LongModel& m)
{
    BackgroundRebuilder::Job job;
    for (const auto& [id, sketch] : m.sketches)
        job.sketches.emplace_back(id, sketch);
    job.features = m.history;
    return job;
}

void testInTheBackground()
{
    LongModel m = longModel(10);
    std::atomic<int> ready = 0;
    std::atomic<std::size_t> reported = 0;
    BackgroundRebuilder builder([&] { ++ready; }, [&](int, std::size_t done, std::size_t) { reported = done; });

    // Requests one after another: the last one is what comes out.
    int ticket = 0;
    for (int i = 0; i < 5; ++i) {
        m.history[m.pocketFeature[0]].value = -1.0 - i;
        ticket = builder.request(jobOf(m));
    }
    CHECK(builder.wait(ticket, -1));
    auto done = builder.take();
    CHECK(done && done->ticket == ticket);
    CHECK(done && sameAsFull(m, done->result));
    CHECK(!builder.take()); // once
    CHECK(ready >= 1);
    CHECK(reported > 0);

    // The same again comes straight from the cache.
    ticket = builder.request(jobOf(m));
    CHECK(builder.wait(ticket, 5000));
    done = builder.take();
    CHECK(done && done->stats.replayed == 0);
    CHECK(!builder.busy());

    // Waiting with a time limit on something slow says so.
    m.history[m.pocketFeature[0]].value = -7.0;
    ticket = builder.request(jobOf(m));
    const bool quick = builder.wait(ticket, 0);
    CHECK(builder.wait(ticket, -1));
    CHECK(quick || builder.take());
}

void testStopsWhileBuilding()
{
    // Destroyed with work queued: it gives up at the next feature, without hanging.
    LongModel m = longModel(30);
    const auto start = std::chrono::steady_clock::now();
    {
        BackgroundRebuilder builder({}, {});
        builder.request(jobOf(m));
        builder.request(jobOf(m));
    }
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(10));
}

} // namespace

int main()
{
    return runTests({
        {"replays from the change", testReplaysFromTheChange},
        {"every field counts", testEveryFieldCounts},
        {"rebuilds are repeatable", testRebuildsAreRepeatable},
        {"picks found on a busy body", testPicksFoundOnABusyBody},
        {"errors come from the cache too", testErrorsComeFromTheCacheToo},
        {"cancel keeps what was built", testCancelKeepsWhatWasBuilt},
        {"small cache stays right", testSmallCacheStaysRight},
        {"imports are keyed by content", testImportsAreKeyedByContent},
        {"in the background", testInTheBackground},
        {"stops while building", testStopsWhileBuilding},
    });
}
