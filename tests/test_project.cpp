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
    numberFeatures(project.features); // as the editor keeps them
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

void testSolidFeaturesRoundTrip()
{
    // The fields of revolve, sweep, pattern, hole and datum features survive a save / load.
    ProjectData project;
    Feature revolve;
    revolve.kind = Feature::Kind::Revolve;
    revolve.id = 4;
    revolve.value = 270.0;
    revolve.axis.kind = AxisRef::Kind::SketchLine;
    revolve.axis.sketch = 2;
    revolve.axis.entity = 9;
    Feature sweep;
    sweep.kind = Feature::Kind::Sweep;
    sweep.id = 5;
    sweep.pathSketch = 3;
    sweep.pathEntity = 11;
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.id = 6;
    pattern.pattern = Feature::PatternKind::Circular;
    pattern.count = 8;
    pattern.source = 4;
    pattern.axis.kind = AxisRef::Kind::Edge;
    pattern.axis.edge = {gp_Pnt(1, 2, 3), gp::DY()};
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.id = 7;
    hole.hole = {HoleKind::Countersink, 4.5, 6.0, 9.0, 1.5};
    hole.holeAt = {{3, 21}};
    hole.points = {gp_Pnt(4, 5, 6)};
    Feature datum;
    datum.kind = Feature::Kind::Datum;
    datum.id = 8;
    datum.datum = Feature::DatumKind::AnglePlane;
    datum.plane.kind = PlaneRef::Kind::Face;
    datum.plane.face = {gp_Pnt(0, 0, 10), gp::DZ()};
    datum.axis.kind = AxisRef::Kind::Datum;
    datum.axis.datum = kBaseX;
    datum.value = 30.0;
    project.features = {revolve, sweep, pattern, hole, datum};

    QString error;
    const auto loaded = projectFromJson(projectToJson(project), &error);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    const auto& f = loaded->features;
    CHECK(f.size() == 5);
    CHECK(f[0].kind == Feature::Kind::Revolve && f[0].id == 4 && f[0].value == 270.0);
    CHECK(f[0].axis.kind == AxisRef::Kind::SketchLine && f[0].axis.sketch == 2 && f[0].axis.entity == 9);
    CHECK(f[1].kind == Feature::Kind::Sweep && f[1].pathSketch == 3 && f[1].pathEntity == 11);
    CHECK(f[2].pattern == Feature::PatternKind::Circular && f[2].count == 8 && f[2].source == 4);
    CHECK(f[2].axis.kind == AxisRef::Kind::Edge && near(f[2].axis.edge.middle.Y(), 2.0));
    CHECK(f[3].hole.kind == HoleKind::Countersink && f[3].hole.diameter == 4.5 && f[3].hole.depth == 6.0);
    CHECK(f[3].hole.headDiameter == 9.0 && f[3].hole.headDepth == 1.5);
    CHECK(f[3].holeAt.size() == 1 && f[3].holeAt[0].sketch == 3 && f[3].holeAt[0].point == 21);
    CHECK(f[3].points.size() == 1 && near(f[3].points[0].Z(), 6.0));
    CHECK(f[4].datum == Feature::DatumKind::AnglePlane && f[4].plane.kind == PlaneRef::Kind::Face);
    CHECK(f[4].axis.datum == kBaseX && f[4].value == 30.0);

    // A feature saved before ids existed gets one on loading.
    QByteArray old = projectToJson(project);
    old.replace("\"id\": 4", "\"id\": 0");
    const auto renumbered = projectFromJson(old, &error);
    CHECK(renumbered && renumbered->features[0].id == 9);
}

void testMovesAndBodyStylesRoundTrip()
{
    ProjectData project;
    Feature move;
    move.kind = Feature::Kind::Move;
    move.id = 3;
    move.bodies = {gp_Pnt(1, 2, 3)};
    gp_Trsf t;
    t.SetTranslation(gp_Vec(4, 5, 6));
    move.matrix = matrixOf(t);
    move.copy = true;
    project.features = {move};
    BodyStyle style;
    style.inside = gp_Pnt(7, 8, 9);
    style.name = "Pokrywa";
    style.hidden = true;
    style.color = std::array<double, 3>{0.25, 0.5, 0.75};
    BodyStyle plain;
    plain.inside = gp_Pnt(-1, 0, 0);
    project.bodyStyles = {style, plain};

    QString error;
    const auto loaded = projectFromJson(projectToJson(project), &error);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    const Feature& f = loaded->features.at(0);
    CHECK(f.kind == Feature::Kind::Move && f.copy && f.matrix == move.matrix);
    CHECK(loaded->bodyStyles.size() == 2);
    const BodyStyle& s = loaded->bodyStyles[0];
    CHECK(s.name == "Pokrywa" && s.hidden && s.color && (*s.color)[2] == 0.75);
    CHECK(near(s.inside.Distance(gp_Pnt(7, 8, 9)), 0.0));
    CHECK(!loaded->bodyStyles[1].color && !loaded->bodyStyles[1].hidden);
}

void testDrawingsRoundTrip()
{
    ProjectData project;
    Drawing d;
    d.title = "Wspornik";
    d.width = 420.0;
    d.height = 297.0;
    SheetView section;
    section.kind = ViewKind::Section;
    section.base = ViewKind::Top;
    section.at = gp_Pnt2d(120, 80);
    section.scale = 0.5;
    section.offset = -4.0;
    section.label = "A";
    section.hidden = false;
    d.views = {section};
    SheetDimension size;
    size.kind = SheetDimension::Kind::Diameter;
    size.a = size.b = gp_Pnt(1, 2, 3);
    size.radius = 4.5;
    size.angle = 30.0;
    d.dimensions = {size};
    d.notes = {{gp_Pnt2d(30, 40), "Ostre krawędzie stępić\nR0.5", 5.0}};
    project.drawings = {d};
    QString error;
    const auto loaded = projectFromJson(projectToJson(project), &error);
    CHECK(loaded && loaded->drawings.size() == 1);
    if (!loaded || loaded->drawings.empty())
        return;
    const Drawing& e = loaded->drawings[0];
    CHECK(e.title == "Wspornik" && e.width == 420.0 && e.height == 297.0);
    CHECK(e.views.size() == 1 && e.views[0].kind == ViewKind::Section && e.views[0].base == ViewKind::Top);
    CHECK(e.views[0].scale == 0.5 && e.views[0].offset == -4.0 && e.views[0].label == "A" && !e.views[0].hidden);
    CHECK(e.dimensions.size() == 1 && e.dimensions[0].kind == SheetDimension::Kind::Diameter);
    CHECK(e.dimensions[0].radius == 4.5 && e.dimensions[0].angle == 30.0 && near(e.dimensions[0].a.Z(), 3.0));
    CHECK(e.notes.size() == 1 && e.notes[0].text == "Ostre krawędzie stępić\nR0.5" && e.notes[0].height == 5.0);
}

void testParametersRoundTrip()
{
    ProjectData project = sample();
    ParameterTable& table = project.parameters;
    table.parameters = {{"width", {"40", LengthUnit::Millimetre}, ValueKind::Length, "outside"},
                        {"t", {"1/8", LengthUnit::Inch}, ValueKind::Length, ""},
                        {"n", {"4", LengthUnit::Millimetre}, ValueKind::Number, ""},
                        {"a", {"30", LengthUnit::Millimetre}, ValueKind::Angle, ""}};
    saveConfiguration(table, "Small");
    table.parameters[0].expression.text = "80";
    saveConfiguration(table, "Large");
    project.features.back().expressions["value"] = {"width / 4 + 2", LengthUnit::Inch};
    Sketch& sketch = project.sketches.front().sketch;
    const ConstraintId dimension = sketch.constraints().back().id;
    sketch.constraint(dimension)->expression = {"width", LengthUnit::Millimetre};

    const auto loaded = projectFromJson(projectToJson(project));
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    CHECK(loaded->parameters == project.parameters);
    CHECK(loaded->parameters.active == "Large");
    CHECK(loaded->features.back().expressions == project.features.back().expressions);
    CHECK(loaded->sketches.front().sketch.constraint(dimension)->expression == (Expression{"width", LengthUnit::Millimetre}));

    // Without parameters nothing is written, so files stay as they were.
    CHECK(!projectToJson(sample()).contains("parameters"));
    // Broken or repeated names (a hand-edited file) are dropped, not left ambiguous.
    QByteArray json = projectToJson(project);
    json.replace("\"name\": \"n\"", "\"name\": \"width\"");
    json.replace("\"name\": \"a\"", "\"name\": \"2a\"");
    const auto edited = projectFromJson(json);
    CHECK(edited && edited->parameters.parameters.size() == 2);
}

void testAssemblyRoundTrip()
{
    ProjectData project;
    Assembly& a = project.assembly;
    a.sources = {{ComponentSource::Kind::Body, 3, gp_Pnt(1, 2, 3), {}, {}, "Leaf"},
                 {ComponentSource::Kind::File, 5, {}, "/parts/pin.nibform",
                  std::make_shared<const std::string>("BREP"), "Pin"}};
    a.components = {{1, 3, {}, true, false}, {2, 5, {0, -1, 0, 4, 1, 0, 0, 5, 0, 0, 1, 6}, false, true}};
    Joint j;
    j.id = 7;
    j.kind = JointKind::Cylindrical;
    j.a = {1, gp_Ax3(gp_Pnt(1, 0, 0), gp::DY(), gp::DZ())};
    j.b = {2, gp_Ax3(gp_Pnt(0, 0, 2), -gp::DZ(), gp::DX())};
    j.flip = true;
    j.values = {45, 2.5, 0};
    j.min = {-10, 0, 0};
    j.max = {170, 8, 0};
    j.limited = {true, true, false};
    a.joints = {j};
    a.explode[2] = gp_Vec(0, 0, 15);
    a.exploded = true;
    Drawing sheet;
    sheet.assembly = true;
    sheet.partsList = true;
    SheetView apart;
    apart.kind = ViewKind::Iso;
    apart.exploded = true;
    sheet.views = {apart};
    project.drawings = {sheet};

    const QByteArray json = projectToJson(project);
    const auto loaded = projectFromJson(json);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    const Assembly& b = loaded->assembly;
    CHECK(b.sources.size() == 2 && b.components.size() == 2 && b.joints.size() == 1);
    if (b.sources.size() != 2 || b.components.size() != 2 || b.joints.size() != 1)
        return;
    CHECK(b.sources[0].kind == ComponentSource::Kind::Body && b.sources[0].inside.Distance(gp_Pnt(1, 2, 3)) < 1e-12);
    CHECK(b.sources[1].kind == ComponentSource::Kind::File && b.sources[1].path == "/parts/pin.nibform");
    CHECK(b.sources[1].cache && *b.sources[1].cache == "BREP" && b.sources[1].name == "Pin");
    CHECK(b.components[0].grounded && b.components[0].matrix.empty());
    CHECK(b.components[1].hidden && b.components[1].matrix == a.components[1].matrix);
    const Joint& k = b.joints[0];
    CHECK(k.id == 7 && k.kind == JointKind::Cylindrical && k.flip && k.a.component == 1 && k.b.component == 2);
    CHECK(k.a.frame.Location().Distance(gp_Pnt(1, 0, 0)) < 1e-12 && k.a.frame.Direction().IsEqual(gp::DY(), 1e-12));
    CHECK(k.a.frame.XDirection().IsEqual(gp::DZ(), 1e-12));
    CHECK(k.values == j.values && k.min == j.min && k.max == j.max && k.limited == j.limited);
    CHECK(b.exploded && b.explode.count(2) && b.explode.at(2).Z() == 15.0);
    CHECK(loaded->drawings.size() == 1 && loaded->drawings[0].assembly && loaded->drawings[0].partsList);
    CHECK(loaded->drawings.size() == 1 && loaded->drawings[0].views.size() == 1 && loaded->drawings[0].views[0].exploded);
    CHECK(projectToJson(*loaded) == json);
    // No assembly, no key: files without one stay as they were.
    CHECK(!projectToJson(ProjectData{}).contains("assembly"));
}

void testDetailsRoundTrip()
{
    // Threads, tapped holes, draft, ribs, emboss and varying fillets (M16).
    ProjectData project;
    Feature thread;
    thread.kind = Feature::Kind::Thread;
    thread.faces = {{gp_Pnt(3, 0, 20), gp::DX()}};
    thread.thread = {ThreadStandard::UNC, 6.35, 1.27, 8.0, true};
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.hole.tapped = true;
    hole.hole.thread = {ThreadStandard::Metric, 8.0, 1.25, 0.0, false};
    Feature draft;
    draft.kind = Feature::Kind::Draft;
    draft.value = -3.0;
    draft.plane.kind = PlaneRef::Kind::Datum;
    draft.plane.datum = kBaseXY;
    Feature rib;
    rib.kind = Feature::Kind::Rib;
    rib.flip = true;
    rib.value = 2.5;
    Feature emboss;
    emboss.kind = Feature::Kind::Emboss;
    emboss.operation = BodyOperation::Cut;
    Feature fillet;
    fillet.kind = Feature::Kind::Fillet;
    fillet.value = 1.0;
    fillet.endValue = 3.0;
    fillet.setback = 4.5;
    Feature offset; // an extrusion starting off its sketch (M21)
    offset.kind = Feature::Kind::Extrude;
    offset.value = 10.0;
    offset.startOffset = -7.5;
    project.features = {thread, hole, draft, rib, emboss, fillet, offset};
    numberFeatures(project.features);

    QString error;
    const QByteArray json = projectToJson(project);
    const auto loaded = projectFromJson(json, &error);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    const auto& f = loaded->features;
    CHECK(f.size() == 7);
    CHECK(f[0].kind == Feature::Kind::Thread && f[0].thread.standard == ThreadStandard::UNC);
    CHECK(f[0].thread.diameter == 6.35 && f[0].thread.pitch == 1.27 && f[0].thread.length == 8.0 && f[0].thread.modelled);
    CHECK(f[1].hole.tapped && f[1].hole.thread.diameter == 8.0 && f[1].hole.thread.pitch == 1.25);
    CHECK(f[2].kind == Feature::Kind::Draft && f[2].value == -3.0 && f[2].plane.datum == kBaseXY);
    CHECK(f[3].kind == Feature::Kind::Rib && f[3].flip && f[3].value == 2.5);
    CHECK(f[4].kind == Feature::Kind::Emboss && f[4].operation == BodyOperation::Cut);
    CHECK(f[5].endValue == 3.0 && f[5].setback == 4.5);
    CHECK(f.size() == 7 && f[6].startOffset == -7.5);
    CHECK(projectToJson(*loaded) == json);

    // Sketch text: its words, font and point.
    ProjectData texts;
    Sketch labelled(gp_Ax3(gp::Origin(), gp::DZ(), gp::DX()));
    const PointId at = labelled.addPoint({2, 3});
    labelled.addText(at, "Zażółć 8", 6.5, "FreeSans", true, false);
    texts.sketches.push_back({1, labelled});
    const QByteArray textJson = projectToJson(texts);
    const auto textLoaded = projectFromJson(textJson, &error);
    CHECK(textLoaded && textLoaded->sketches.size() == 1);
    if (textLoaded && textLoaded->sketches.size() == 1) {
        const Sketch& back = textLoaded->sketches[0].sketch;
        CHECK(back.texts().size() == 1);
        if (back.texts().size() == 1) {
            CHECK(back.texts()[0] == labelled.texts()[0]);
        }
        CHECK(projectToJson(*textLoaded) == textJson);
    }
    // A text whose point is missing is left out.
    QByteArray orphan = textJson;
    orphan.replace(QByteArray("\"at\": ") + QByteArray::number(at), "\"at\": 999");
    const auto withoutPoint = projectFromJson(orphan, &error);
    CHECK(withoutPoint && withoutPoint->sketches[0].sketch.texts().empty());

    // A thread with no pitch (a damaged file) keeps a sound default instead.
    QByteArray damaged = json;
    damaged.replace("\"pitch\": 1.27", "\"pitch\": 0");
    const auto repaired = projectFromJson(damaged, &error);
    CHECK(repaired && repaired->features[0].thread.pitch > 0.0);
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
        {"assembly round trip", testAssemblyRoundTrip},
        {"round trip", testRoundTrip},
        {"toolkit geometry round trip", testToolkitGeometryRoundTrip},
        {"solid features round trip", testSolidFeaturesRoundTrip},
        {"details round trip", testDetailsRoundTrip},
        {"moves and body styles round trip", testMovesAndBodyStylesRoundTrip},
        {"drawings round trip", testDrawingsRoundTrip},
        {"parameters round trip", testParametersRoundTrip},
        {"saved project rebuilds the same model", testSavedProjectRebuildsTheSameModel},
        {"rejects other files", testRejectsOtherFiles},
        {"opens projects from before the rename", testOpensProjectsFromBeforeTheRename},
        {"recovery keeps work of a crashed session", testRecoveryKeepsWorkOfACrashedSession},
        {"recovery ignores running and closed sessions", testRecoveryIgnoresRunningAndClosedSessions},
    });
}
