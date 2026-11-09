// Interop (M12): STEP / IGES / STL in and out, imports in the history, 3MF, DXF and SVG
// sketches, and the clipboard.

#include "Check.h"

#include "io/ProjectFile.h"
#include "io/SketchExchange.h"
#include "model/Export.h"
#include "model/Features.h"
#include "sketch/SketchOps.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {

constexpr double kPi = 3.14159265358979323846;

double volume(const TopoDS_Shape& s)
{
    double v = 0.0;
    for (TopExp_Explorer it(s, TopAbs_SOLID); it.More(); it.Next())
        v += volumeOf(it.Current());
    return v;
}

int solids(const TopoDS_Shape& s)
{
    int n = 0;
    for (TopExp_Explorer it(s, TopAbs_SOLID); it.More(); it.Next())
        ++n;
    return n;
}

int count(const Sketch& s, EntityKind kind)
{
    return static_cast<int>(std::count_if(s.entities().begin(), s.entities().end(),
                                          [kind](const SketchEntity& e) { return e.kind == kind; }));
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void testShapesAsText()
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const auto back = shapeFromText(shapeToText(box));
    CHECK(back.has_value());
    if (back)
        CHECK(near(volume(*back), 6000.0, 1e-6));
    CHECK(!shapeFromText("not a shape"));
}

void testStepIgesStlRoundTrips()
{
    QTemporaryDir dir;
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    for (const char* name : {"part.step", "part.iges", "part.stl"}) {
        const std::string path = dir.filePath(QString::fromLatin1(name)).toStdString();
        const std::string ext = path.substr(path.size() - 4);
        const std::string written = ext == "step" ? exportStep({box}, path)
                                  : ext == "iges" ? exportIges({box}, path)
                                                  : exportStl({box}, path);
        CHECK(written.empty());
        std::string error;
        const auto shape = importShape(path, error);
        CHECK(shape.has_value());
        if (!shape) {
            std::printf("  %s: %s\n", name, error.c_str());
            continue;
        }
        CHECK(solids(*shape) == 1); // an STL box closes into a solid too
        CHECK(near(volume(*shape), 6000.0, 1e-3));
    }
    std::string error;
    CHECK(!importShape(dir.filePath(QStringLiteral("missing.step")).toStdString(), error) && !error.empty());
    CHECK(!importShape(dir.filePath(QStringLiteral("part.obj")).toStdString(), error));
}

void testImportsInTheHistory()
{
    const SketchLookup none = [](int) -> const Sketch* { return nullptr; };
    TopoDS_Compound two;
    BRep_Builder().MakeCompound(two);
    BRep_Builder().Add(two, BRepPrimAPI_MakeBox(10, 10, 10).Shape());
    BRep_Builder().Add(two, BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 5, 5, 5).Shape());
    Feature import;
    import.kind = Feature::Kind::Import;
    import.shape = std::make_shared<const std::string>(shapeToText(two));
    import.label = "parts.step";
    std::vector<Feature> history{import};
    numberFeatures(history);
    RebuildResult asBodies = rebuild(none, history);
    CHECK(asBodies.errors.at(0).empty());
    CHECK(asBodies.bodies.size() == 2);
    CHECK(asBodies.context.references.empty());

    history[0].reference = true;
    RebuildResult asReference = rebuild(none, history);
    CHECK(asReference.bodies.empty());
    CHECK(asReference.context.references.size() == 1);

    // Faces only (no solid) can't be bodies.
    TopoDS_Compound faces;
    BRep_Builder().MakeCompound(faces);
    for (TopExp_Explorer it(BRepPrimAPI_MakeBox(10, 10, 10).Shape(), TopAbs_FACE); it.More(); it.Next())
        BRep_Builder().Add(faces, it.Current());
    history[0].shape = std::make_shared<const std::string>(shapeToText(faces));
    history[0].reference = false;
    CHECK(!rebuild(none, history).errors.at(0).empty());

    // Saved and loaded with the project.
    ProjectData project;
    project.features = {import};
    QString error;
    const auto loaded = projectFromJson(projectToJson(project), &error);
    CHECK(loaded && loaded->features.at(0).shape && loaded->features[0].label == "parts.step");
    if (loaded)
        CHECK(rebuild(none, loaded->features).bodies.size() == 2);
}

void testOpenModifyExportStep()
{
    // A plate from "another CAD": imported, drilled in the history, exported back.
    QTemporaryDir dir;
    const std::string in = dir.filePath(QStringLiteral("other.step")).toStdString();
    const std::string out = dir.filePath(QStringLiteral("back.step")).toStdString();
    CHECK(exportStep({BRepPrimAPI_MakeBox(gp_Pnt(-20, -20, 0), 40, 40, 10).Shape()}, in).empty());
    std::string error;
    const auto shape = importShape(in, error);
    CHECK(shape.has_value());
    if (!shape)
        return;
    Feature import;
    import.kind = Feature::Kind::Import;
    import.shape = std::make_shared<const std::string>(shapeToText(*shape));
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.faces = {{gp_Pnt(0, 0, 10), gp::DZ()}};
    hole.points = {gp_Pnt(0, 0, 10)};
    hole.hole.diameter = 10.0;
    std::vector<Feature> history{import, hole};
    numberFeatures(history);
    const RebuildResult model = rebuild([](int) -> const Sketch* { return nullptr; }, history);
    CHECK(model.errors.at(1).empty());
    CHECK(exportStep(model.bodies, out).empty());
    const auto back = importShape(out, error);
    CHECK(back && near(volume(*back), 16000.0 - kPi * 25.0 * 10.0, 1e-2));
}

void testThreeMf()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("print.3mf"));
    std::vector<PrintBody> bodies{{BRepPrimAPI_MakeBox(10, 10, 10).Shape(), "Base & lid", {1.0, 0.0, 0.0}},
                                  {BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 10, 10, 10).Shape(), "Pin", {0.0, 0.5, 1.0}}};
    CHECK(export3mf(bodies, path.toStdString()).empty());
    const QByteArray zip = readAll(path);
    CHECK(zip.startsWith("PK\x03\x04"));
    CHECK(zip.contains("[Content_Types].xml") && zip.contains("_rels/.rels") && zip.contains("3D/3dmodel.model"));
    CHECK(zip.contains("unit=\"millimeter\""));
    CHECK(zip.contains("displaycolor=\"#FF0000\"") && zip.contains("displaycolor=\"#0080FF\""));
    CHECK(zip.contains("name=\"Base &amp; lid\""));
    CHECK(zip.count("<triangle ") == 24); // two boxes, 12 triangles each
    CHECK(zip.count("<vertex ") == 16);   // corners shared between faces
    CHECK(zip.contains("PK\x05\x06"));    // the archive's directory

    CHECK(export3mf(bodies, path.toStdString(), true).empty());
    const QByteArray inches = readAll(path);
    CHECK(inches.contains("unit=\"inch\"") && inches.contains("0.393700787"));
}

void testDxfRoundTrip()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({40, 0}), c = s.addPoint({40, 20});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addCircle(s.addPoint({10, 10}), 4.0);
    s.addArc(s.addPoint({40, 10}), c, s.addPoint({30, 10})); // a quarter, counter-clockwise
    s.addSpline({s.addPoint({0, 30}), s.addPoint({10, 35}), s.addPoint({20, 30})}, false);

    Sketch back;
    QString error;
    const auto made = importDxf(back, sketchToDxf(s), &error);
    CHECK(made.has_value());
    CHECK(count(back, EntityKind::Line) == 2);
    CHECK(count(back, EntityKind::Circle) == 1);
    CHECK(count(back, EntityKind::Arc) == 1);
    CHECK(count(back, EntityKind::Spline) == 1); // the spline comes back through its points
    for (const SketchEntity& e : back.entities()) {
        if (e.kind == EntityKind::Arc) {
            CHECK(near(back.pos(e.a), gp_Pnt2d(40, 20), 1e-9));
            CHECK(near(back.pos(e.b), gp_Pnt2d(30, 10), 1e-9));
        }
        if (e.kind == EntityKind::Circle)
            CHECK(near(e.radius, 4.0, 1e-9));
    }
    // The two lines share their corner, as they did.
    CHECK(back.findPoint({40, 0}, 1e-9).has_value());
    CHECK(back.entitiesAt(*back.findPoint({40, 0}, 1e-9)).size() == 2);
}

void testDxfPolylinesAndUnits()
{
    // A 1 × 1 inch square whose top side bulges up into a half circle.
    const QByteArray dxf =
        "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLWPOLYLINE\n8\n0\n90\n4\n70\n1\n"
        "10\n0\n20\n0\n10\n1\n20\n0\n10\n1\n20\n1\n42\n1\n10\n0\n20\n1\n"
        "0\nENDSEC\n0\nEOF\n";
    Sketch s;
    QString error;
    CHECK(importDxf(s, dxf, &error).has_value());
    CHECK(count(s, EntityKind::Line) == 3);
    CHECK(count(s, EntityKind::Arc) == 1);
    CHECK(s.findPoint({25.4, 25.4}, 1e-6).has_value());
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Arc)
            CHECK(near(s.pos(e.c).Distance(gp_Pnt2d(12.7, 25.4)), 0.0, 1e-6)); // half a circle over the top
    }
    CHECK(!importDxf(s, "hello", &error) && !error.isEmpty());
}

void testSvgImport()
{
    // 100 user units across 100 mm; a rectangle, a circle moved by a group, a path with
    // an arc and a curve.
    const QByteArray svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="50mm" viewBox="0 0 100 50">
      <defs><rect x="0" y="0" width="5" height="5"/></defs>
      <rect x="10" y="10" width="20" height="10"/>
      <g transform="translate(50 25)"><circle cx="0" cy="0" r="5"/></g>
      <path d="M 70 40 A 10 10 0 0 1 90 40 C 90 30 80 30 70 40 Z"/>
    </svg>)svg";
    Sketch s;
    QString error;
    const auto made = importSvg(s, svg, &error);
    CHECK(made.has_value());
    CHECK(count(s, EntityKind::Line) == 4); // the rectangle (the path closes where it already is)
    CHECK(count(s, EntityKind::Circle) == 1);
    CHECK(count(s, EntityKind::Arc) == 1);
    CHECK(count(s, EntityKind::Spline) == 1);
    // Y is flipped: the rectangle's top left (10, 10) lands at (10, -10).
    CHECK(s.findPoint({10, -10}, 1e-6).has_value());
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Circle) {
            CHECK(near(e.radius, 5.0, 1e-9));
            CHECK(near(s.pos(e.c), gp_Pnt2d(50, -25), 1e-9));
        }
        if (e.kind == EntityKind::Arc) {
            CHECK(near(s.pos(e.c).Distance(gp_Pnt2d(80, -40)), 0.0, 1e-6));
            CHECK(near(s.pos(e.c).Distance(s.pos(e.a)), 10.0, 1e-6));
        }
    }
    CHECK(!importSvg(s, "<svg xmlns=\"http://www.w3.org/2000/svg\"/>", &error));
}

void testSvgRoundTrip()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({30, 0});
    s.addLine(a, b);
    s.addCircle(s.addPoint({15, 10}), 3.0);
    s.addArc(s.addPoint({30, 10}), b, s.addPoint({40, 10}));
    Sketch back;
    QString error;
    CHECK(importSvg(back, sketchToSvg(s), &error).has_value());
    CHECK(count(back, EntityKind::Line) == 1);
    CHECK(count(back, EntityKind::Circle) == 1);
    CHECK(count(back, EntityKind::Arc) == 1);
    for (const SketchEntity& e : back.entities()) {
        if (e.kind == EntityKind::Arc) {
            CHECK(near(back.pos(e.a), gp_Pnt2d(30, 0), 1e-6)); // still counter-clockwise from (30, 0)
            CHECK(near(back.pos(e.b), gp_Pnt2d(40, 10), 1e-6));
        }
        if (e.kind == EntityKind::Circle)
            CHECK(near(back.pos(e.c), gp_Pnt2d(15, 10), 1e-6));
    }
}

void testClipboard()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({20, 0}), c = s.addPoint({20, 10});
    const EntityId bottom = s.addLine(a, b);
    const EntityId side = s.addLine(b, c);
    const EntityId other = s.addCircle(s.addPoint({50, 50}), 3.0);
    SketchConstraint level;
    level.kind = ConstraintKind::Horizontal;
    level.p1 = a;
    level.p2 = b;
    level.e1 = bottom;
    s.addConstraint(level);
    SketchConstraint length;
    length.kind = ConstraintKind::Distance;
    length.p1 = a;
    length.p2 = b;
    length.e1 = bottom;
    length.value = 20.0;
    s.addConstraint(length);
    SketchConstraint radius;
    radius.kind = ConstraintKind::Radius;
    radius.e1 = other;
    radius.value = 3.0;
    s.addConstraint(radius); // not copied: its circle isn't

    const auto part = sketchFromClipboard(sketchClipboard(copyEntities(s, {bottom, side})));
    CHECK(part.has_value());
    if (!part)
        return;
    CHECK(part->entities.size() == 2 && part->points.size() == 3 && part->constraints.size() == 2);
    Sketch target;
    target.addLine(target.addPoint({0, 0}), target.addPoint({1, 1}));
    const auto pasted = pasteContents(target, *part, gp_Vec2d(5, 5));
    CHECK(pasted.size() == 2);
    CHECK(target.entities().size() == 3);
    CHECK(target.constraints().size() == 2);
    CHECK(target.findPoint({25, 5}, 1e-9).has_value()); // shifted
    CHECK(target.entitiesAt(*target.findPoint({25, 5}, 1e-9)).size() == 2); // still joined

    const std::string brep = shapeToText(BRepPrimAPI_MakeBox(1, 2, 3).Shape());
    CHECK(bodiesFromClipboard(bodiesClipboard(brep)) == brep);
    CHECK(!bodiesFromClipboard(sketchClipboard(*part)));
    CHECK(!sketchFromClipboard("plain text"));
}

} // namespace

int main()
{
    return runTests({
        {"shapes as text", testShapesAsText},
        {"STEP, IGES, STL round trips", testStepIgesStlRoundTrips},
        {"imports in the history", testImportsInTheHistory},
        {"open, modify, export STEP", testOpenModifyExportStep},
        {"3MF", testThreeMf},
        {"DXF round trip", testDxfRoundTrip},
        {"DXF polylines and units", testDxfPolylinesAndUnits},
        {"SVG import", testSvgImport},
        {"SVG round trip", testSvgRoundTrip},
        {"clipboard", testClipboard},
    });
}
