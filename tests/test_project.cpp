// Project files: sketches and history survive a save / load, and rebuild the same model.

#include "Check.h"

#include "io/ProjectFile.h"
#include "io/Recovery.h"

#include <QTemporaryDir>

#include <sys/wait.h>
#include <unistd.h>

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
    Sketch::Contents withReference = side.contents();
    SketchEntity reference;
    reference.id = withReference.nextId++;
    reference.a = 2;
    reference.b = 3;
    reference.construction = true;
    withReference.entities.push_back(reference);
    side = Sketch(side.plane(), withReference);
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
    Feature hollow;
    hollow.kind = Feature::Kind::Shell;
    hollow.faces = {{gp_Pnt(5, 3, 5), gp::DZ()}};
    hollow.value = 0.8;
    project.features = {sketchMark, pull, round, cut, hollow};
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
    CHECK(!loaded->sketches[1].sketch.entities()[0].construction);
    CHECK(loaded->sketches[1].sketch.entities()[1].construction);
    // New ids keep counting from where they were.
    Sketch more = base;
    CHECK(more.addPoint({1, 1}) == original.sketches[0].sketch.contents().nextId);

    CHECK(loaded->features.size() == 5);
    CHECK(loaded->features[4].kind == Feature::Kind::Shell && near(loaded->features[4].value, 0.8));
    CHECK(loaded->features[1].operation == BodyOperation::NewBody);
    CHECK(loaded->features[3].boolean == BooleanKind::Subtract && loaded->features[3].bodies.size() == 2);
}

void testToolkitGeometryRoundTrip()
{
    // Ellipses, splines, the newer constraints and a reference image survive a save / load.
    ProjectData project;
    Sketch s;
    const PointId c = s.addPoint({0, 0}), a = s.addPoint({8, 0}), b = s.addPoint({0, 3});
    s.addEllipse(c, a, b);
    const std::vector<PointId> through{s.addPoint({20, 0}), s.addPoint({25, 5}), s.addPoint({30, 0})};
    const EntityId open = s.addSpline(through, false);
    const EntityId closed =
        s.addSpline({s.addPoint({0, 20}), s.addPoint({5, 25}), s.addPoint({0, 30}), s.addPoint({-5, 25})}, true);
    const EntityId axis = s.addLine(s.addPoint({50, -5}), s.addPoint({50, 5}));
    SketchConstraint sym;
    sym.kind = ConstraintKind::Symmetric;
    sym.p1 = through[0];
    sym.p2 = through[2];
    sym.e1 = axis;
    s.addConstraint(sym);
    SketchConstraint across;
    across.kind = ConstraintKind::HorizontalDistance;
    across.p1 = through[0];
    across.p2 = through[2];
    across.value = 10.0;
    across.offset = 2.0;
    s.addConstraint(across);
    SketchConstraint up = across;
    up.kind = ConstraintKind::VerticalDistance;
    up.value = -1.0;
    s.addConstraint(up);
    ReferenceImage image;
    const std::string bytes("\x89PNG fake bytes \0 with a zero", 30);
    image.data = std::make_shared<const std::string>(bytes);
    image.pixelWidth = 640;
    image.pixelHeight = 480;
    image.corner = gp_Pnt2d(-12.5, 3.25);
    image.width = 160.0;
    image.opacity = 0.75;
    s.setImage(image);
    project.sketches.push_back({1, s});

    const QByteArray json = projectToJson(project);
    const auto loaded = projectFromJson(json);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    CHECK(projectToJson(*loaded) == json);
    const Sketch& l = loaded->sketches[0].sketch;
    CHECK(l.entities()[0].kind == EntityKind::Ellipse && l.entities()[0].b == b);
    CHECK(l.entity(open)->kind == EntityKind::Spline && l.entity(open)->through == through && !l.entity(open)->closed);
    CHECK(l.entity(closed)->closed && l.entity(closed)->through.size() == 4);
    CHECK(l.constraints().size() == 3);
    CHECK(l.constraints()[0].kind == ConstraintKind::Symmetric);
    CHECK(l.constraints()[1].kind == ConstraintKind::HorizontalDistance && near(l.constraints()[1].value, 10.0));
    CHECK(l.constraints()[2].kind == ConstraintKind::VerticalDistance && near(l.constraints()[2].value, -1.0));
    CHECK(l.image().has_value());
    if (l.image()) {
        CHECK(*l.image()->data == bytes);
        CHECK(l.image()->pixelWidth == 640 && l.image()->pixelHeight == 480);
        CHECK(near(l.image()->corner, gp_Pnt2d(-12.5, 3.25)));
        CHECK(near(l.image()->width, 160.0) && near(l.image()->height(), 120.0));
        CHECK(near(l.image()->opacity, 0.75));
    }
}

void testSavedProjectRebuildsTheSameModel()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("model.nibform"));
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
    CHECK(!loadProject(QStringLiteral("/nonexistent/x.nibform"), &error) && !error.isEmpty());
    const QByteArray newer = projectToJson(sample()).replace("\"version\": 1", "\"version\": 99");
    CHECK(!projectFromJson(newer, &error));
}

void testOpensProjectsFromBeforeTheRename()
{
    const QByteArray former =
        projectToJson(sample()).replace("\"nibform-project\"", "\"shapr-linux-project\"");
    CHECK(former.contains("shapr-linux-project"));
    QString error;
    const auto loaded = projectFromJson(former, &error);
    CHECK(loaded && error.isEmpty() && loaded->features.size() == sample().features.size());
}

void testRecoveryKeepsWorkOfACrashedSession()
{
    QTemporaryDir dir;
    // A session that dies without cleaning up: a child process, killed after an autosave.
    const pid_t child = fork();
    if (child == 0) {
        RecoverySession crashed(dir.path());
        crashed.write(sample(), QStringLiteral("/home/someone/bracket.nibform"));
        _exit(0); // no destructors, as after kill -9
    }
    int status = 0;
    waitpid(child, &status, 0);

    const std::vector<RecoveredWork> found = findRecoveredWork(dir.path());
    CHECK(found.size() == 1);
    if (found.empty())
        return;
    CHECK(found[0].sourcePath == QStringLiteral("/home/someone/bracket.nibform"));
    CHECK(found[0].saved.isValid() && found[0].saved.secsTo(QDateTime::currentDateTime()) < 60);
    CHECK(projectToJson(found[0].project) == projectToJson(sample()));
    discardRecovery(found[0].file);
    CHECK(findRecoveredWork(dir.path()).empty());
    CHECK(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

void testRecoveryIgnoresRunningAndClosedSessions()
{
    QTemporaryDir dir;
    {
        RecoverySession running(dir.path());
        CHECK(running.write(sample(), {}).isEmpty());
        CHECK(QFile::exists(running.file()));
        CHECK(findRecoveredWork(dir.path()).empty()); // still alive: not left behind
        running.clear(); // saved, or closed on purpose
        CHECK(!QFile::exists(running.file()));
    }
    // Closed normally: nothing to recover, nothing left on disk.
    CHECK(findRecoveredWork(dir.path()).empty());
    CHECK(QDir(dir.path()).entryList(QDir::Files).isEmpty());

    // Ended without clearing (the window never closed): the work is offered.
    {
        RecoverySession unfinished(dir.path());
        CHECK(unfinished.write(sample(), {}).isEmpty());
    }
    CHECK(findRecoveredWork(dir.path()).size() == 1);
}

} // namespace

int main()
{
    return runTests({
        {"round trip", testRoundTrip},
        {"toolkit geometry round trip", testToolkitGeometryRoundTrip},
        {"saved project rebuilds the same model", testSavedProjectRebuildsTheSameModel},
        {"rejects other files", testRejectsOtherFiles},
        {"opens projects from before the rename", testOpensProjectsFromBeforeTheRename},
        {"recovery keeps work of a crashed session", testRecoveryKeepsWorkOfACrashedSession},
        {"recovery ignores running and closed sessions", testRecoveryIgnoresRunningAndClosedSessions},
    });
}
