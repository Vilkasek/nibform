// Fuzzing the project loader: damaged or hostile files must never crash, hang or
// assert. They are either refused (with an error) or loaded into something that saves,
// loads again and rebuilds like any other project.
//
// Deterministic: every case comes from a fixed seed, so a failure names the case that
// reproduces it (`test_project_fuzz <cases> <first case> [-v]` runs a range of them, -v
// naming each case before it runs).

#include "Check.h"

#include "io/ProjectFile.h"
#include "model/Export.h"
#include "model/Profiles.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <optional>
#include <random>

namespace {

// Valid projects to start from, covering everything a project file can hold.
ProjectData plateProject()
{
    ProjectData project;
    Sketch base(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ(), gp::DX()));
    const PointId a = base.addPoint({0, 0}), b = base.addPoint({40, 0});
    const PointId c = base.addPoint({40, 30}), d = base.addPoint({0, 30});
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
    length.value = 40.0;
    length.offset = -3.0;
    length.target = gp_Pnt2d(20, -3);
    base.addConstraint(length);
    const PointId mark = base.addPoint({10, 10});
    project.sketches.push_back({1, base});

    Feature sketchMark;
    sketchMark.kind = Feature::Kind::Sketch;
    sketchMark.sketch = 1;
    Feature pull;
    pull.kind = Feature::Kind::Extrude;
    pull.regions = {{1, gp_Pnt2d(20, 15)}};
    pull.value = 10.0;
    pull.operation = BodyOperation::NewBody;
    Feature round;
    round.kind = Feature::Kind::Fillet;
    round.edges = {{gp_Pnt(20, 0, 10), gp::DX()}};
    round.value = 1.0;
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.faces = {{gp_Pnt(20, 15, 10), gp::DZ()}};
    hole.hole = {HoleKind::Counterbore, 4.0, 0.0, 8.0, 2.0};
    hole.holeAt = {{1, mark}};
    hole.points = {gp_Pnt(30, 20, 10)};
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.pattern = Feature::PatternKind::Circular;
    pattern.axis.kind = AxisRef::Kind::Datum;
    pattern.axis.datum = kBaseZ;
    pattern.count = 3;
    pattern.value = 30.0; // degrees, spread over
    pattern.source = 5; // the hole
    Feature offset;
    offset.kind = Feature::Kind::Datum;
    offset.datum = Feature::DatumKind::OffsetPlane;
    offset.plane.kind = PlaneRef::Kind::Face;
    offset.plane.face = {gp_Pnt(20, 15, 10), gp::DZ()};
    offset.value = 5.0;
    Feature move;
    move.kind = Feature::Kind::Move;
    move.bodies = {gp_Pnt(35, 25, 5)};
    gp_Trsf t;
    t.SetRotation(gp::OZ(), 0.3);
    t.SetTranslationPart(gp_Vec(4, 5, 6));
    move.matrix = matrixOf(t);
    move.copy = true;
    Feature shell;
    shell.kind = Feature::Kind::Shell;
    shell.faces = {{gp_Pnt(20, 15, 0), -gp::DZ()}}; // open at the bottom
    shell.value = 0.8;
    project.features = {sketchMark, pull, shell, round, hole, pattern, offset, move};
    numberFeatures(project.features);

    BodyStyle style;
    style.inside = gp_Pnt(1, 1, 1);
    style.name = "Plate";
    style.color = std::array<double, 3>{0.25, 0.5, 0.75};
    project.bodyStyles = {style};

    Drawing sheet;
    sheet.title = "Plate";
    SheetView front;
    front.at = gp_Pnt2d(80, 60);
    SheetView section;
    section.kind = ViewKind::Section;
    section.base = ViewKind::Top;
    section.label = "A";
    section.offset = 2.0;
    section.at = gp_Pnt2d(200, 60);
    SheetView iso;
    iso.kind = ViewKind::Iso;
    iso.scale = 0.5;
    sheet.views = {front, section, iso};
    SheetDimension wide;
    wide.kind = SheetDimension::Kind::Horizontal;
    wide.a = gp_Pnt(0, 0, 0);
    wide.b = gp_Pnt(40, 0, 0);
    SheetDimension round2;
    round2.kind = SheetDimension::Kind::Diameter;
    round2.view = 1;
    round2.radius = 2.0;
    sheet.dimensions = {wide, round2};
    sheet.notes = {{gp_Pnt2d(10, 200), "Break sharp edges", 3.5}};
    project.drawings = {sheet};
    // Parameters, configurations and values typed as expressions (M15).
    ParameterTable& table = project.parameters;
    table.parameters = {{"width", {"40", LengthUnit::Millimetre}, ValueKind::Length, "outside"},
                        {"t", {"width / 20 + 1/8 in", LengthUnit::Inch}, ValueKind::Length, ""},
                        {"n", {"4", LengthUnit::Millimetre}, ValueKind::Number, ""},
                        {"a", {"360 / n", LengthUnit::Millimetre}, ValueKind::Angle, ""}};
    saveConfiguration(table, "Small");
    table.parameters[0].expression.text = "80";
    saveConfiguration(table, "Large");
    for (Feature& f : project.features) {
        if (f.kind == Feature::Kind::Extrude)
            f.expressions["value"] = {"t * 2", LengthUnit::Millimetre};
        else if (f.kind == Feature::Kind::Pattern)
            f.expressions["count"] = {"n", LengthUnit::Millimetre};
    }
    for (auto& entry : project.sketches) {
        for (const SketchConstraint& c : entry.sketch.constraints()) {
            if (isDimension(c.kind)) {
                entry.sketch.constraint(c.id)->expression = {"width / 2", LengthUnit::Millimetre};
                break;
            }
        }
    }
    return project;
}

ProjectData toolkitProject()
{
    ProjectData project;
    Sketch s(gp_Ax3(gp_Pnt(0, 0, 5), gp::DY(), gp::DZ()));
    const PointId center = s.addPoint({0, 0});
    s.addCircle(center, 6.0);
    s.addArc(s.addPoint({20, 0}), s.addPoint({25, 0}), s.addPoint({15, 0}));
    s.addEllipse(s.addPoint({0, 30}), s.addPoint({8, 30}), s.addPoint({0, 33}));
    s.addSpline({s.addPoint({40, 0}), s.addPoint({45, 5}), s.addPoint({50, 0})}, false);
    s.addSpline({s.addPoint({0, 60}), s.addPoint({5, 65}), s.addPoint({0, 70}), s.addPoint({-5, 65})}, true);
    const EntityId axis = s.addLine(s.addPoint({-20, -5}), s.addPoint({-20, 5}));
    s.setConstruction(axis, true);
    SketchConstraint radius;
    radius.kind = ConstraintKind::Radius;
    radius.e1 = 1;
    radius.value = 6.0;
    s.addConstraint(radius);
    ReferenceImage image;
    image.data = std::make_shared<const std::string>("\x89PNG\r\n\x1a\n not really a picture");
    image.pixelWidth = 64;
    image.pixelHeight = 48;
    image.corner = gp_Pnt2d(-10, -10);
    image.width = 40.0;
    s.setImage(image);
    project.sketches.push_back({7, s});
    // A smaller circle further along, to loft to.
    Sketch top(gp_Ax3(gp_Pnt(0, 20, 5), gp::DY(), gp::DZ()));
    top.addCircle(top.addPoint({0, 0}), 3.0);
    project.sketches.push_back({8, top});

    Feature revolve;
    revolve.kind = Feature::Kind::Revolve;
    revolve.regions = {{7, gp_Pnt2d(0, 0)}};
    revolve.axis.kind = AxisRef::Kind::SketchLine;
    revolve.axis.sketch = 7;
    revolve.axis.entity = axis;
    revolve.value = 270.0;
    Feature sweep;
    sweep.kind = Feature::Kind::Sweep;
    sweep.regions = {{7, gp_Pnt2d(20, 0)}};
    sweep.pathSketch = 7;
    sweep.pathEntity = 4;
    Feature loft;
    loft.kind = Feature::Kind::Loft;
    loft.regions = {{7, gp_Pnt2d(0, 0)}, {8, gp_Pnt2d(0, 0)}};
    Feature mirror;
    mirror.kind = Feature::Kind::Mirror;
    mirror.plane.kind = PlaneRef::Kind::Datum;
    mirror.plane.datum = kBaseYZ;
    mirror.bodies = {gp_Pnt(0, 5, 0)};
    mirror.operation = BodyOperation::Join;
    Feature row;
    row.kind = Feature::Kind::Pattern;
    row.pattern = Feature::PatternKind::Linear;
    row.axis.kind = AxisRef::Kind::Datum;
    row.axis.datum = kBaseX;
    row.count = 3;
    row.value = 20.0; // apart
    row.bodies = {gp_Pnt(4.9, 5.9, 6.9)}; // the imported box
    Feature threePoints;
    threePoints.kind = Feature::Kind::Datum;
    threePoints.datum = Feature::DatumKind::ThreePointPlane;
    threePoints.points = {gp_Pnt(0, 0, 0), gp_Pnt(1, 0, 0), gp_Pnt(0, 1, 1)};
    Feature twoPoints;
    twoPoints.kind = Feature::Kind::Datum;
    twoPoints.datum = Feature::DatumKind::TwoPointAxis;
    twoPoints.points = {gp_Pnt(0, 0, 0), gp_Pnt(0, 0, 9)};
    Feature angled;
    angled.kind = Feature::Kind::Datum;
    angled.datum = Feature::DatumKind::AnglePlane;
    angled.plane.kind = PlaneRef::Kind::Datum;
    angled.plane.datum = kBaseXY;
    angled.axis.kind = AxisRef::Kind::Datum;
    angled.axis.datum = kBaseX;
    angled.value = 30.0;
    Feature box;
    box.kind = Feature::Kind::Import;
    box.shape = std::make_shared<const std::string>(shapeToText(BRepPrimAPI_MakeBox(5, 6, 7).Shape()));
    box.label = "box.step";
    Feature reference = box;
    reference.reference = true;
    Feature combine;
    combine.kind = Feature::Kind::Combine;
    combine.boolean = BooleanKind::Intersect;
    combine.bodies = {gp_Pnt(1, 1, 1), gp_Pnt(2, 2, 2)};
    Feature remove;
    remove.kind = Feature::Kind::Delete;
    remove.bodies = {gp_Pnt(1, 1, 1)};
    Feature chamfer;
    chamfer.kind = Feature::Kind::Chamfer;
    chamfer.edges = {{gp_Pnt(2.5, 0, 0), gp::DX()}};
    chamfer.value = 0.5;
    project.features = {revolve, sweep, loft, mirror, box, reference, row, threePoints, twoPoints,
                        angled, chamfer, combine, remove};
    numberFeatures(project.features);
    BodyStyle hidden;
    hidden.inside = gp_Pnt(1, 1, 1);
    hidden.hidden = true;
    project.bodyStyles = {hidden};
    return project;
}

// A file from before the rename, with no ids and few of the newer keys.
const char* const kOldProject = R"({
    "format": "shapr-linux-project", "version": 1,
    "sketches": [{"id": 2, "plane": {"origin": [0, 0, 0], "normal": [0, 0, 1], "xDirection": [1, 0, 0]},
                  "points": [{"id": 1, "x": 0, "y": 0}, {"id": 2, "x": 5, "y": 0}, {"id": 3, "x": 0, "y": 5}],
                  "entities": [{"id": 4, "kind": "line", "a": 1, "b": 2}, {"id": 5, "kind": "line", "a": 2, "b": 3},
                               {"id": 6, "kind": "line", "a": 3, "b": 1}],
                  "constraints": [], "nextId": 7}],
    "features": [{"kind": "sketch", "sketch": 2},
                 {"kind": "extrude", "value": 3, "operation": "new", "regions": [{"sketch": 2, "inside": [1, 1]}]}]
})";

// Random choices, from one seed per case.
struct Random
{
    std::mt19937 gen;
    explicit Random(unsigned seed) : gen(seed) {}
    int below(int n) { return n <= 1 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(gen); }
    bool chance(int percent) { return below(100) < percent; }
    template <typename T>
    const T& pick(const std::vector<T>& from) { return from[static_cast<std::size_t>(below(static_cast<int>(from.size())))]; }
};

// Values that loaders tend to trust: extremes, wrong types, unknown names.
QJsonValue hostileValue(Random& r)
{
    // Mostly within what a file may hold (up to 1e9), some beyond it.
    static const std::vector<double> numbers{0.0,   -0.0,  1.0,    -1.0,  2.0,    3.0,   12.0,   0.5,     1e-13, 4.9e-324,
                                             359.999, 360.0, -360.0, 500.0, 501.0, 1e6,   -1e6,   1e9,     -1e9,  999999999.0,
                                             2147483647.0, -2147483649.0, 1e18, -1e308};
    static const std::vector<QString> strings{"", "line", "spline", "pattern", "move", "import", "circular", "datum",
                                              "section", "nope", "\u0000", "AAAA", "!!!not base64!!!", "DBRep_DrawableShape",
                                              QString(4000, QChar('x'))};
    switch (r.below(8)) {
    case 0:
    case 1:
        return r.pick(numbers);
    case 2:
        return r.pick(strings);
    case 3:
        return r.chance(50);
    case 4:
        return QJsonValue(QJsonValue::Null);
    case 5: {
        QJsonArray a;
        const int n = r.pick(std::vector<int>{0, 1, 2, 3, 4, 11, 12, 13, 64});
        for (int i = 0; i < n; ++i)
            a.append(r.chance(80) ? QJsonValue(r.pick(numbers)) : hostileValue(r));
        return a;
    }
    case 6:
        return QJsonObject{};
    default:
        return QJsonObject{{"id", r.pick(numbers)}, {"kind", r.pick(strings)}, {"x", r.pick(numbers)}};
    }
}

// A random array or object inside `value`, as a path of keys / indices.
using Path = std::vector<QJsonValue>;

Path randomPath(Random& r, const QJsonValue& root)
{
    Path path;
    QJsonValue at = root;
    while (true) {
        if (at.isObject()) {
            const QStringList keys = at.toObject().keys();
            if (keys.isEmpty() || (!path.empty() && r.chance(25)))
                return path;
            const QString key = keys[r.below(static_cast<int>(keys.size()))];
            path.push_back(key);
            at = at.toObject().value(key);
        } else if (at.isArray()) {
            const QJsonArray a = at.toArray();
            if (a.isEmpty() || (!path.empty() && r.chance(25)))
                return path;
            const int i = r.below(static_cast<int>(a.size()));
            path.push_back(i);
            at = a.at(i);
        } else {
            return path;
        }
    }
}

// Applies `edit` to the value at `path` within `root` (Qt's JSON values are copies, so
// the containers are rebuilt on the way back up).
QJsonValue editAt(const QJsonValue& root, const Path& path, std::size_t depth,
                  const std::function<std::optional<QJsonValue>(const QJsonValue&)>& edit, bool& removed)
{
    if (depth == path.size()) {
        const auto next = edit(root);
        removed = !next;
        return next.value_or(QJsonValue());
    }
    if (root.isObject()) {
        QJsonObject o = root.toObject();
        const QString key = path[depth].toString();
        bool gone = false;
        const QJsonValue v = editAt(o.value(key), path, depth + 1, edit, gone);
        if (gone && depth + 1 == path.size())
            o.remove(key);
        else
            o.insert(key, v);
        return o;
    }
    QJsonArray a = root.toArray();
    const int i = path[depth].toInt();
    bool gone = false;
    const QJsonValue v = editAt(a.at(i), path, depth + 1, edit, gone);
    if (gone && depth + 1 == path.size())
        a.removeAt(i);
    else
        a.replace(i, v);
    return a;
}

// Targeted damage to the parts that later code computes with.
void targeted(Random& r, QJsonObject& root)
{
    QJsonArray features = root.value("features").toArray();
    QJsonArray sketches = root.value("sketches").toArray();
    if (!features.isEmpty() && r.chance(70)) {
        const int i = r.below(static_cast<int>(features.size()));
        QJsonObject f = features.at(i).toObject();
        switch (r.below(10)) {
        case 0:
            f.insert("count", r.pick(std::vector<double>{1e9, 2147483647.0, -2147483648.0, 0.0, 1.0, 501.0, 1e300}));
            break;
        case 1: {
            QJsonArray m;
            const int n = r.pick(std::vector<int>{0, 1, 11, 13, 16, 100});
            for (int k = 0; k < n; ++k)
                m.append(r.chance(90) ? QJsonValue(k % 5 == 0 ? 1.0 : 0.0) : hostileValue(r));
            f.insert("matrix", m);
            break;
        }
        case 2:
            f.insert("matrix", QJsonArray{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0}); // scales: not rigid
            break;
        case 3:
            f.insert("shape", r.pick(std::vector<QString>{"garbage", "DBRep_DrawableShape\n\nCASCADE Topology V1",
                                                          "CASCADE Topology V1, (c) Matra-Datavision\nLocations 99999999\n"}));
            break;
        case 4:
            f.insert("faces", QJsonArray{QJsonObject{{"middle", QJsonArray{0, 0, 0}}, {"normal", QJsonArray{0, 0, 0}}}});
            break;
        case 5:
            f.insert("edges", QJsonArray{QJsonObject{{"middle", QJsonArray{0, 0, 0}}, {"direction", QJsonArray{}}}});
            break;
        case 6:
            f.insert("source", r.pick(std::vector<int>{-1, 0, 1, 999, 2147483647}));
            f.insert("id", r.pick(std::vector<int>{-5, 0, 1, 2147483647}));
            break;
        case 7:
            f.insert("kind", r.pick(std::vector<QString>{"pattern", "hole", "datum", "move", "import", "sweep"}));
            break;
        case 8:
            f.insert("hole", QJsonObject{{"kind", "countersink"}, {"diameter", hostileValue(r)}, {"headDiameter", -1}});
            break;
        default:
            f.insert("points", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{0, 0, 0}, QJsonArray{0, 0, 0}});
            break;
        }
        features.replace(i, f);
        root.insert("features", features);
    }
    if (!sketches.isEmpty() && r.chance(40)) {
        const int i = r.below(static_cast<int>(sketches.size()));
        QJsonObject s = sketches.at(i).toObject();
        switch (r.below(6)) {
        case 0: // no normal, or one along the x direction
            s.insert("plane", QJsonObject{{"origin", QJsonArray{0, 0, 0}},
                                          {"normal", r.chance(50) ? QJsonArray{0, 0, 0} : QJsonArray{1, 0, 0}},
                                          {"xDirection", QJsonArray{1, 0, 0}}});
            break;
        case 1:
            s.insert("nextId", r.pick(std::vector<int>{-1, 0, 1, 2147483647}));
            break;
        case 2:
            s.insert("image", QJsonObject{{"data", r.pick(std::vector<QString>{"%%%", "", "AA", "QUJD="})},
                                          {"pixelWidth", hostileValue(r)}, {"pixelHeight", 2147483647},
                                          {"width", 1e-300}});
            break;
        case 3: { // dangling and repeated ids
            QJsonArray entities = s.value("entities").toArray();
            entities.append(QJsonObject{{"id", 1}, {"kind", "spline"}, {"a", 999}, {"b", -7}, {"through", QJsonArray{999, 1, 1}},
                                        {"closed", true}});
            entities.append(QJsonObject{{"id", 3}, {"kind", "arc"}, {"a", 1}, {"b", 1}, {"c", 1}, {"radius", -4}});
            s.insert("entities", entities);
            break;
        }
        case 4: {
            QJsonArray constraints = s.value("constraints").toArray();
            constraints.append(QJsonObject{{"id", 2147483647}, {"kind", "tangent"}, {"e1", 123456}, {"e2", -1}, {"target", 5}});
            s.insert("constraints", constraints);
            break;
        }
        default:
            s.insert("points", QJsonArray{QJsonObject{{"id", 1}, {"x", 1e308}, {"y", -1e308}}, QJsonObject{{"id", 1}}});
            break;
        }
        sketches.replace(i, s);
        root.insert("sketches", sketches);
    }
}

// A random change somewhere in the tree: a key dropped, a value of another type, an
// array emptied.
void structural(Random& r, QJsonObject& root)
{
    const Path path = randomPath(r, root);
    if (path.empty())
        return;
    const int what = r.below(5);
    bool removed = false;
    root = editAt(root, path, 0,
                  [&](const QJsonValue& v) -> std::optional<QJsonValue> {
                      switch (what) {
                      case 0:
                          return std::nullopt;
                      case 1:
                          if (v.isArray())
                              return QJsonArray{};
                          return hostileValue(r);
                      case 2:
                          if (v.isArray() && !v.toArray().isEmpty()) {
                              // The same element many times over.
                              QJsonArray a = v.toArray();
                              const QJsonValue first = a.at(0);
                              for (int k = 0; k < 200; ++k)
                                  a.append(first);
                              return a;
                          }
                          return hostileValue(r);
                      default:
                          return hostileValue(r);
                      }
                  },
                  removed)
               .toObject();
}

// Damage to the text itself: flipped, cut, inserted and deleted bytes.
void bytewise(Random& r, QByteArray& text)
{
    const int edits = 1 + r.below(4);
    for (int e = 0; e < edits && !text.isEmpty(); ++e) {
        const int at = r.below(static_cast<int>(text.size()));
        switch (r.below(7)) {
        case 0:
            text[at] = static_cast<char>(text[at] ^ (1 << r.below(8)));
            break;
        case 1:
            text.truncate(at);
            break;
        case 2:
            text.insert(at, static_cast<char>(r.below(256)));
            break;
        case 3:
            text.remove(at, 1 + r.below(16));
            break;
        case 4:
            text.insert(at, r.pick(std::vector<QByteArray>{"[", "]", "{", "}", "\"", ",", ":", "null", "-", "e999",
                                                           "1e308", "\\u0000", "\\ud800"}));
            break;
        case 5: { // a digit changed, so numbers grow or flip sign
            if (std::isdigit(static_cast<unsigned char>(text[at])))
                text[at] = static_cast<char>('0' + r.below(10));
            else
                text.insert(at, '9');
            break;
        }
        default: { // a stretch repeated
            const int length = std::min<int>(1 + r.below(64), static_cast<int>(text.size()) - at);
            text.insert(at, text.mid(at, length));
            break;
        }
        }
    }
}

// Deeply nested arrays in place of some value (Qt's parser has a depth limit; past it
// the file is refused, below it the loader sees arrays where it wanted numbers).
QByteArray nested(Random& r, const QByteArray& text)
{
    const int depth = r.pick(std::vector<int>{3, 100, 1000, 5000});
    const QByteArray deep = QByteArray(depth, '[') + "1" + QByteArray(depth, ']');
    QByteArray out = text;
    const qsizetype at = out.indexOf(r.pick(std::vector<QByteArray>{"\"count\": ", "\"x\": ", "\"origin\": ", "\"matrix\": ",
                                                                    "\"inside\": ", "\"views\": "}));
    if (at < 0)
        return out;
    const qsizetype start = out.indexOf(':', at) + 2;
    qsizetype end = start;
    while (end < out.size() && out[end] != ',' && out[end] != '\n')
        ++end;
    out.replace(start, end - start, deep);
    return out;
}

const Sketch* findSketch(const ProjectData& project, int id)
{
    for (const auto& entry : project.sketches) {
        if (entry.id == id)
            return &entry.sketch;
    }
    return nullptr;
}

// Small enough to rebuild in a moment: few features, no long patterns.
bool lightEnough(const ProjectData& project)
{
    if (project.features.size() > 16 || project.sketches.size() > 4)
        return false;
    for (const auto& entry : project.sketches) {
        const Sketch::Contents c = entry.sketch.contents();
        if (c.points.size() > 40 || c.entities.size() > 20)
            return false;
    }
    for (const Feature& f : project.features) {
        if ((f.kind == Feature::Kind::Pattern && f.count > 8) || f.regions.size() > 4 || f.bodies.size() > 4
            || f.points.size() + f.holeAt.size() > 4 || f.edges.size() > 4)
            return false;
    }
    return true;
}

struct Stats
{
    int loaded = 0, refused = 0, rebuilt = 0;
    double rebuildSeconds = 0.0;
};

// What must hold for any input: no crash on the way, and anything loaded saves and
// loads again the same.
void checkInput(const QByteArray& text, bool tryRebuild, Stats& stats, int caseNumber)
{
    QString error;
    const auto project = projectFromJson(text, &error);
    sketchFromClipboard(text);
    bodiesFromClipboard(text);
    if (!project) {
        ++stats.refused;
        if (error.isEmpty()) {
            std::printf("  case %d: refused without an error\n", caseNumber);
            CHECK(!error.isEmpty());
        }
        return;
    }
    ++stats.loaded;
    const QByteArray saved = projectToJson(*project);
    const auto again = projectFromJson(saved, &error);
    if (!again) {
        std::printf("  case %d: doesn't load again: %s\n", caseNumber, qPrintable(error));
        CHECK(again.has_value());
        return;
    }
    bool same = again->sketches.size() == project->sketches.size()
             && again->features.size() == project->features.size()
             && again->bodyStyles.size() == project->bodyStyles.size()
             && again->drawings.size() == project->drawings.size();
    for (std::size_t i = 0; same && i < project->features.size(); ++i) {
        const Feature &a = project->features[i], &b = again->features[i];
        same = a.kind == b.kind && a.id == b.id && a.count == b.count && a.matrix == b.matrix
                    && a.regions.size() == b.regions.size() && (a.shape == nullptr) == (b.shape == nullptr);
    }
    for (std::size_t i = 0; same && i < project->sketches.size(); ++i) {
        const Sketch::Contents a = project->sketches[i].sketch.contents(), b = again->sketches[i].sketch.contents();
        same = project->sketches[i].id == again->sketches[i].id && a.points.size() == b.points.size()
                    && a.entities.size() == b.entities.size() && a.constraints.size() == b.constraints.size()
                    && a.nextId == b.nextId && a.image.has_value() == b.image.has_value();
    }
    if (!same) {
        std::printf("  case %d: changed on a second save / load\n", caseNumber);
        CHECK(same);
    }
    if (tryRebuild && lightEnough(*project)) {
        const auto start = std::chrono::steady_clock::now();
        // As opening it in the app does: the values follow the parameters first.
        ProjectData followed = *project;
        std::vector<std::pair<int, Sketch*>> sketches;
        for (auto& entry : followed.sketches)
            sketches.emplace_back(entry.id, &entry.sketch);
        applyParameters(followed.parameters, followed.features, sketches);
        const RebuildResult result =
            rebuild([&](int id) { return findSketch(followed, id); }, followed.features);
        CHECK(result.errors.size() == followed.features.size());
        ++stats.rebuilt;
        stats.rebuildSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
}

QByteArray mutate(unsigned seed, const std::vector<QByteArray>& seeds, bool& structuralOnly)
{
    Random r(seed);
    const QByteArray& original = r.pick(seeds);
    structuralOnly = false;
    switch (r.below(8)) {
    case 0:
    case 1:
    case 2: { // the tree changed in a few places
        QJsonObject root = QJsonDocument::fromJson(original).object();
        const int edits = 1 + r.below(3);
        for (int e = 0; e < edits; ++e)
            structural(r, root);
        structuralOnly = true;
        return QJsonDocument(root).toJson(r.chance(50) ? QJsonDocument::Compact : QJsonDocument::Indented);
    }
    case 3:
    case 4: { // the parts later code computes with
        QJsonObject root = QJsonDocument::fromJson(original).object();
        targeted(r, root);
        if (r.chance(30))
            structural(r, root);
        structuralOnly = true;
        return QJsonDocument(root).toJson();
    }
    case 5:
        return nested(r, original);
    default: {
        QByteArray text = original;
        bytewise(r, text);
        return text;
    }
    }
}

int g_cases = 3000;
int g_first = 0;
bool g_verbose = false;

void testMutatedProjects()
{
    std::vector<QByteArray> seeds{projectToJson(plateProject()), projectToJson(toolkitProject()), kOldProject};
    // The clipboard formats go through the same sketch reader.
    seeds.push_back(sketchClipboard(toolkitProject().sketches[0].sketch.contents()));
    seeds.push_back(bodiesClipboard(shapeToText(BRepPrimAPI_MakeBox(1, 2, 3).Shape())));

    // The starting points themselves load and build bodies.
    for (int i = 0; i < 3; ++i) {
        const auto project = projectFromJson(seeds[static_cast<std::size_t>(i)]);
        CHECK(project.has_value());
        if (!project)
            continue;
        const RebuildResult model = rebuild([&](int id) { return findSketch(*project, id); }, project->features);
        CHECK(!model.bodies.empty());
    }
    CHECK(sketchFromClipboard(seeds[3]).has_value());
    CHECK(bodiesFromClipboard(seeds[4]).has_value());

    Stats stats;
    const auto start = std::chrono::steady_clock::now();
    for (int c = g_first; c < g_first + g_cases; ++c) {
        bool structuralOnly = false;
        const QByteArray text = mutate(static_cast<unsigned>(c), seeds, structuralOnly);
        if (g_verbose) { // names the case that crashes, before it does
            std::printf("  case %d\n", c);
            std::fflush(stdout);
        }
        // Rebuilding is the slow part: only every few structurally changed cases.
        checkInput(text, structuralOnly && c % 12 == 0, stats, c);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %d cases (%d loaded, %d refused, %d rebuilt) in %.1f s (%.1f s rebuilding)\n", g_cases, stats.loaded,
                stats.refused, stats.rebuilt, seconds, stats.rebuildSeconds);
    // A good share must still load, or the cases only exercise the JSON parser.
    CHECK(stats.loaded > g_cases / 5);
}

// --- What the fuzzing found -----------------------------------------------------------

// The plate project with `edit` applied to its JSON.
std::optional<ProjectData> loadEdited(const std::function<void(QJsonObject&)>& edit, QString* error = nullptr)
{
    QJsonObject root = QJsonDocument::fromJson(projectToJson(plateProject())).object();
    edit(root);
    return projectFromJson(QJsonDocument(root).toJson(), error);
}

// Edits the first object of the array `key` of `root`.
void editFirst(QJsonObject& root, const QString& key, const std::function<void(QJsonObject&)>& edit)
{
    QJsonArray a = root.value(key).toArray();
    QJsonObject o = a.at(0).toObject();
    edit(o);
    a.replace(0, o);
    root.insert(key, a);
}

void testDamagedSketchPlanes()
{
    // gp_Dir threw on a zero normal, gp_Ax3 on a normal along the x direction: both
    // took the app down. Now the file is refused.
    for (const QJsonArray& normal : {QJsonArray{0, 0, 0}, QJsonArray{1, 0, 0}, QJsonArray{}, QJsonArray{1e308, 1e308, 0}}) {
        QString error;
        const auto project = loadEdited(
            [&](QJsonObject& root) {
                editFirst(root, "sketches", [&](QJsonObject& s) {
                    s.insert("plane", QJsonObject{{"origin", QJsonArray{0, 0, 0}}, {"normal", normal},
                                                  {"xDirection", QJsonArray{1, 0, 0}}});
                });
            },
            &error);
        CHECK(!project && !error.isEmpty());
    }
}

void testZeroPickDirections()
{
    // Faces and edges with no direction threw too; they get a default one, as axes do.
    const auto project = loadEdited([](QJsonObject& root) {
        QJsonArray features = root.value("features").toArray();
        QJsonObject round = features.at(2).toObject();
        round.insert("edges", QJsonArray{QJsonObject{{"middle", QJsonArray{1, 2, 3}}, {"direction", QJsonArray{0, 0, 0}}}});
        round.insert("faces", QJsonArray{QJsonObject{{"middle", QJsonArray{1, 2, 3}}, {"normal", QJsonArray{}}}});
        features.replace(2, round);
        root.insert("features", features);
    });
    CHECK(project.has_value());
    if (!project)
        return;
    CHECK(project->features[2].edges.size() == 1 && project->features[2].edges[0].direction.IsEqual(gp::DX(), 0.0));
    CHECK(project->features[2].faces.size() == 1 && project->features[2].faces[0].normal.IsEqual(gp::DZ(), 0.0));
}

void testPatternCountsAreLimited()
{
    // A count of a billion would build a billion solids.
    for (double count : {1e9, 1e6, 501.0}) {
        const auto project = loadEdited([&](QJsonObject& root) {
            QJsonArray features = root.value("features").toArray();
            QJsonObject pattern = features.at(5).toObject();
            pattern.insert("count", count);
            features.replace(5, pattern);
            root.insert("features", features);
        });
        CHECK(project && project->features[5].kind == Feature::Kind::Pattern);
        CHECK(project && project->features[5].count == kMaxPatternCount);
    }
    const auto plain = loadEdited([](QJsonObject&) {});
    CHECK(plain && plain->features[5].count == 3);
}

void testMatricesOfWrongLength()
{
    for (int length : {0, 11, 13, 1000}) {
        const auto project = loadEdited([&](QJsonObject& root) {
            QJsonArray features = root.value("features").toArray();
            QJsonObject move = features.at(7).toObject();
            QJsonArray m;
            for (int i = 0; i < length; ++i)
                m.append(i % 5 == 0 ? 1.0 : 0.0);
            move.insert("matrix", m);
            features.replace(7, move);
            root.insert("features", features);
        });
        CHECK(project && project->features[7].kind == Feature::Kind::Move && project->features[7].matrix.empty());
    }
    const auto plain = loadEdited([](QJsonObject&) {});
    CHECK(plain && plain->features[7].matrix.size() == 12 && trsfOf(plain->features[7].matrix));
}

void testSketchIdsAfterDamage()
{
    // A next id below the ids in use would hand out one of them again.
    const auto project = loadEdited([](QJsonObject& root) {
        editFirst(root, "sketches", [](QJsonObject& s) { s.insert("nextId", 1); });
    });
    CHECK(project.has_value());
    if (project) {
        Sketch s = project->sketches[0].sketch;
        const PointId fresh = s.addPoint({1, 2});
        CHECK(fresh == plateProject().sketches[0].sketch.contents().nextId);
    }
}

void testNumbersTooLarge()
{
    auto refused = [](const std::function<void(QJsonObject&)>& edit) {
        QString error;
        const auto project = loadEdited(edit, &error);
        return !project && !error.isEmpty();
    };
    // A hole on a sketch point at -1e308 mm crashed OpenCASCADE (a segmentation fault in
    // a boolean, nothing to catch); from about 1e17 mm on it does.
    CHECK(refused([](QJsonObject& root) {
        editFirst(root, "sketches", [](QJsonObject& s) {
            QJsonArray points = s.value("points").toArray();
            QJsonObject mark = points.last().toObject();
            mark.insert("x", -1e17);
            points.replace(points.size() - 1, mark);
            s.insert("points", points);
        });
    }));
    // Features without ids are numbered after the highest one: after 2^31 - 1 that
    // overflowed (undefined behaviour). So would a sketch's next id.
    CHECK(refused([](QJsonObject& root) {
        QJsonArray features = root.value("features").toArray();
        QJsonObject a = features.at(0).toObject(), b = features.at(1).toObject();
        a.insert("id", 2147483647);
        b.remove("id");
        features.replace(0, a);
        features.replace(1, b);
        root.insert("features", features);
    }));
    CHECK(refused([](QJsonObject& root) {
        editFirst(root, "sketches", [](QJsonObject& s) { s.insert("nextId", 2147483647); });
    }));
    CHECK(refused([](QJsonObject& root) { editFirst(root, "drawings", [](QJsonObject& d) { d.insert("width", -1e300); }); }));
    // Large but sane numbers still load, and ids just short of the end save and load
    // again (numbering the features without one stays below it).
    CHECK(!refused([](QJsonObject& root) {
        QJsonArray features = root.value("features").toArray();
        QJsonObject pull = features.at(1).toObject();
        pull.insert("value", 1e9);
        features.replace(1, pull);
        root.insert("features", features);
    }));
    const auto high = loadEdited([](QJsonObject& root) {
        QJsonArray features = root.value("features").toArray();
        QJsonObject a = features.at(0).toObject(), b = features.at(1).toObject();
        a.insert("id", 2147483645);
        b.remove("id");
        features.replace(0, a);
        features.replace(1, b);
        root.insert("features", features);
        editFirst(root, "sketches", [](QJsonObject& s) { s.insert("nextId", 2147483646); });
    });
    CHECK(high && high->features[1].id == 2147483646 && projectFromJson(projectToJson(*high)).has_value());
    // The clipboard too.
    Sketch::Contents far;
    far.points = {{1, gp_Pnt2d(0, 0)}, {2, gp_Pnt2d(1e300, 0)}};
    SketchEntity line;
    line.id = 3;
    line.a = 1;
    line.b = 2;
    far.entities = {line};
    far.nextId = 4;
    CHECK(!sketchFromClipboard(sketchClipboard(far)));
    far.points[1].pos = gp_Pnt2d(10, 0);
    CHECK(sketchFromClipboard(sketchClipboard(far)).has_value());
}

void testCurvesWithNoShape()
{
    // A line from a point to itself and a circle of negative radius: OpenCASCADE threw
    // while finding the sketch's regions (when showing the sketch or rebuilding).
    const auto project = loadEdited([](QJsonObject& root) {
        editFirst(root, "sketches", [](QJsonObject& s) {
            QJsonArray entities = s.value("entities").toArray();
            entities.append(QJsonObject{{"id", 100}, {"kind", "line"}, {"a", 1}, {"b", 1}});
            entities.append(QJsonObject{{"id", 101}, {"kind", "circle"}, {"c", 1}, {"radius", -4}});
            entities.append(QJsonObject{{"id", 102}, {"kind", "arc"}, {"c", 1}, {"a", 1}, {"b", 2}});
            s.insert("entities", entities);
        });
    });
    CHECK(project.has_value());
    if (!project)
        return;
    const Sketch& sketch = project->sketches[0].sketch;
    CHECK(toEdge(sketch, *sketch.entity(100)).IsNull());
    CHECK(toEdge(sketch, *sketch.entity(101)).IsNull());
    CHECK(findRegions(sketch).size() == 1); // the rectangle still bounds its region
    CHECK(!sketchPath(sketch, 100));
    const RebuildResult model = rebuild([&](int id) { return findSketch(*project, id); }, project->features);
    CHECK(!model.bodies.empty() && model.errors[1].empty()); // the extrude still applies
}

void testFeaturesWithAbsurdValues()
{
    // Extruding 1e308 mm threw from inside OpenCASCADE (making the prism's faces). Files
    // can't hold such values any more, but the model is the last line: the feature fails
    // like any other that doesn't apply.
    ProjectData project = plateProject();
    project.features[1].value = 1e308;
    const RebuildResult model = rebuild([&](int id) { return findSketch(project, id); }, project.features);
    CHECK(model.errors.size() == project.features.size() && !model.errors[1].empty());
    CHECK(model.bodies.empty());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1)
        g_cases = std::atoi(argv[1]);
    if (argc > 2)
        g_first = std::atoi(argv[2]);
    g_verbose = argc > 3;
    return runTests({
        {"mutated projects", testMutatedProjects},
        {"damaged sketch planes", testDamagedSketchPlanes},
        {"zero pick directions", testZeroPickDirections},
        {"pattern counts are limited", testPatternCountsAreLimited},
        {"matrices of wrong length", testMatricesOfWrongLength},
        {"sketch ids after damage", testSketchIdsAfterDamage},
        {"numbers too large", testNumbersTooLarge},
        {"curves with no shape", testCurvesWithNoShape},
        {"features with absurd values", testFeaturesWithAbsurdValues},
    });
}
