// Project files: sketches and history survive a save / load, and rebuild the same model.

#include "Check.h"

#include "io/ProjectFile.h"

#include <QTemporaryDir>

#include <map>

namespace {

ProjectData sample()
{
    ProjectData project;
    Sketch base(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ(), gp::DX()));
    const PointId a = base.addPoint({0, 0}), b = base.addPoint({10, 0});
    const PointId c = base.addPoint({10, 6}), d = base.addPoint({0, 6});
    const EntityId bottom = base.addLine(a, b);
    base.addLine(b, c);
    base.addLine(c, d);
    base.addLine(d, a);
    SketchConstraint h;
    h.kind = ConstraintKind::Horizontal;
    h.p1 = a;
    h.p2 = b;
    h.e1 = bottom;
    base.addConstraint(h);
    SketchConstraint length;
    length.kind = ConstraintKind::Distance;
    length.p1 = a;
    length.p2 = b;
    length.e1 = bottom;
    length.value = 10.0;
    length.offset = -3.0;
    base.addConstraint(length);
    project.sketches.push_back({4, base});

    Sketch side(gp_Ax3(gp_Pnt(0, 0, 5), gp::DZ(), gp::DX()));
    side.addArc(side.addPoint({5, 3}), side.addPoint({7, 3}), side.addPoint({3, 3}));
    project.sketches.push_back({9, side});

    Feature sketchMark;
    sketchMark.kind = Feature::Kind::Sketch;
    sketchMark.sketch = 4;
    Feature pull;
    pull.kind = Feature::Kind::Extrude;
    pull.regions = {{4, gp_Pnt2d(5, 3)}};
    pull.value = 5.0;
    pull.operation = BodyOperation::NewBody;
    Feature round;
    round.kind = Feature::Kind::Fillet;
    round.edges = {{gp_Pnt(5, 0, 5), gp::DX()}};
    round.value = 1.0;
    Feature cut;
    cut.kind = Feature::Kind::Combine;
    cut.boolean = BooleanKind::Subtract;
    cut.bodies = {gp_Pnt(1, 1, 1), gp_Pnt(9, 5, 1)};
    project.features = {sketchMark, pull, round, cut};
    return project;
}

void testRoundTrip()
{
    const ProjectData original = sample();
    const QByteArray json = projectToJson(original);
    const auto loaded = projectFromJson(json);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    // Same text when written again: nothing lost or reinterpreted.
    CHECK(projectToJson(*loaded) == json);

    CHECK(loaded->sketches.size() == 2 && loaded->sketches[0].id == 4 && loaded->sketches[1].id == 9);
    const Sketch& base = loaded->sketches[0].sketch;
    CHECK(base.entities().size() == 4 && base.constraints().size() == 2);
    CHECK(base.constraints()[1].kind == ConstraintKind::Distance && near(base.constraints()[1].offset, -3.0));
    CHECK(loaded->sketches[1].sketch.plane().Location().Z() == 5.0);
    CHECK(loaded->sketches[1].sketch.entities()[0].kind == EntityKind::Arc);
    // New ids keep counting from where they were.
    Sketch more = base;
    CHECK(more.addPoint({1, 1}) == original.sketches[0].sketch.contents().nextId);

    CHECK(loaded->features.size() == 4);
    CHECK(loaded->features[1].operation == BodyOperation::NewBody);
    CHECK(loaded->features[3].boolean == BooleanKind::Subtract && loaded->features[3].bodies.size() == 2);
}

void testSavedProjectRebuildsTheSameModel()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("model.shapr"));
    const ProjectData original = sample();
    CHECK(saveProject(path, original).isEmpty());
    QString error;
    const auto loaded = loadProject(path, &error);
    CHECK(loaded && error.isEmpty());
    if (!loaded)
        return;

    auto volumeOfProject = [](const ProjectData& p) {
        std::map<int, const Sketch*> byId;
        for (const auto& e : p.sketches)
            byId[e.id] = &e.sketch;
        const RebuildResult r = rebuild([&](int id) { return byId.count(id) ? byId.at(id) : nullptr; },
                                        std::vector<Feature>(p.features.begin(), p.features.begin() + 3));
        double v = 0.0;
        for (const TopoDS_Shape& b : r.bodies)
            v += volumeOf(b);
        return v;
    };
    CHECK(near(volumeOfProject(*loaded), volumeOfProject(original), 1e-9));
    CHECK(near(volumeOfProject(*loaded), 300.0 - (1.0 - M_PI / 4.0) * 10.0, 1e-6));
}

void testRejectsOtherFiles()
{
    QString error;
    CHECK(!projectFromJson("{\"hello\": 1}", &error) && !error.isEmpty());
    CHECK(!projectFromJson("not json at all", &error));
    CHECK(!loadProject(QStringLiteral("/nonexistent/x.shapr"), &error) && !error.isEmpty());
    const QByteArray newer = projectToJson(sample()).replace("\"version\": 1", "\"version\": 99");
    CHECK(!projectFromJson(newer, &error));
}

} // namespace

int main()
{
    return runTests({
        {"round trip", testRoundTrip},
        {"saved project rebuilds the same model", testSavedProjectRebuildsTheSameModel},
        {"rejects other files", testRejectsOtherFiles},
    });
}
