#include "io/ProjectFile.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <array>

namespace {

constexpr int kVersion = 1;
const QString kFormat = QStringLiteral("nibform-project");
// Projects saved before the app was renamed: the same format otherwise.
const QString kFormerFormat = QStringLiteral("shapr-linux-project");

// Enums are stored by name, so reordering them later doesn't break old files.
template <typename E, std::size_t N>
QString nameOf(E value, const std::array<const char*, N>& names)
{
    const auto i = static_cast<std::size_t>(value);
    return i < N ? QString::fromLatin1(names[i]) : QString();
}

template <typename E, std::size_t N>
std::optional<E> fromName(const QString& name, const std::array<const char*, N>& names)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (name == QLatin1String(names[i]))
            return static_cast<E>(i);
    }
    return std::nullopt;
}

constexpr std::array<const char*, 5> kEntityKinds{"line", "circle", "arc", "ellipse", "spline"};
constexpr std::array<const char*, 19> kConstraintKinds{
    "horizontal", "vertical",  "parallel",          "perpendicular",   "equal",    "tangent", "concentric",
    "pointOn",    "midpoint",  "fixed",             "distance",        "radius",   "diameter", "angle",
    "pointLine",  "lineLine",  "horizontalDistance", "verticalDistance", "symmetric"};
constexpr std::array<const char*, 15> kFeatureKinds{"sketch", "extrude", "fillet", "chamfer", "combine",
                                                    "delete", "shell",   "revolve", "sweep",  "loft",
                                                    "mirror", "pattern", "hole",    "datum",  "move"};
constexpr std::array<const char*, 4> kAxisKinds{"none", "edge", "sketchLine", "datum"};
constexpr std::array<const char*, 3> kPlaneKinds{"none", "face", "datum"};
constexpr std::array<const char*, 2> kPatternKinds{"linear", "circular"};
constexpr std::array<const char*, 3> kHoleKinds{"simple", "counterbore", "countersink"};
constexpr std::array<const char*, 5> kDatumKinds{"offsetPlane", "anglePlane", "threePointPlane", "axis",
                                                 "twoPointAxis"};
constexpr std::array<const char*, 4> kOperations{"auto", "new", "join", "cut"};
constexpr std::array<const char*, 3> kBooleans{"union", "subtract", "intersect"};

QJsonArray xyz(const gp_XYZ& v)
{
    return {v.X(), v.Y(), v.Z()};
}

QJsonArray xy(const gp_Pnt2d& p)
{
    return {p.X(), p.Y()};
}

gp_XYZ toXYZ(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    return {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()};
}

gp_Pnt2d toXY(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    return {a.at(0).toDouble(), a.at(1).toDouble()};
}

QJsonObject sketchToJson(int id, const Sketch& sketch)
{
    const gp_Ax3& plane = sketch.plane();
    const Sketch::Contents c = sketch.contents();
    QJsonArray points, entities, constraints;
    for (const SketchPoint& p : c.points)
        points.append(QJsonObject{{"id", p.id}, {"x", p.pos.X()}, {"y", p.pos.Y()}});
    for (const SketchEntity& e : c.entities) {
        QJsonObject o{{"id", e.id},
                      {"kind", nameOf(e.kind, kEntityKinds)},
                      {"a", e.a},
                      {"b", e.b},
                      {"c", e.c},
                      {"radius", e.radius},
                      {"construction", e.construction}};
        if (e.kind == EntityKind::Spline) {
            QJsonArray through;
            for (PointId p : e.through)
                through.append(p);
            o.insert("through", through);
            o.insert("closed", e.closed);
        }
        entities.append(o);
    }
    for (const SketchConstraint& k : c.constraints) {
        constraints.append(QJsonObject{{"id", k.id},
                                       {"kind", nameOf(k.kind, kConstraintKinds)},
                                       {"p1", k.p1},
                                       {"p2", k.p2},
                                       {"e1", k.e1},
                                       {"e2", k.e2},
                                       {"value", k.value},
                                       {"target", xy(k.target)},
                                       {"offset", k.offset}});
    }
    QJsonObject out{
        {"id", id},
        {"plane", QJsonObject{{"origin", xyz(plane.Location().XYZ())},
                              {"normal", xyz(plane.Direction().XYZ())},
                              {"xDirection", xyz(plane.XDirection().XYZ())}}},
        {"points", points},
        {"entities", entities},
        {"constraints", constraints},
        {"nextId", c.nextId},
    };
    if (c.image && c.image->data) {
        const ReferenceImage& image = *c.image;
        const QByteArray bytes = QByteArray::fromRawData(image.data->data(), static_cast<qsizetype>(image.data->size()));
        out.insert("image", QJsonObject{{"data", QString::fromLatin1(bytes.toBase64())},
                                        {"pixelWidth", image.pixelWidth},
                                        {"pixelHeight", image.pixelHeight},
                                        {"corner", xy(image.corner)},
                                        {"width", image.width},
                                        {"opacity", image.opacity}});
    }
    return out;
}

std::optional<ProjectData::SketchEntry> sketchFromJson(const QJsonObject& o, QString* error)
{
    const QJsonObject plane = o.value("plane").toObject();
    const gp_Ax3 axes(gp_Pnt(toXYZ(plane.value("origin"))), gp_Dir(toXYZ(plane.value("normal"))),
                      gp_Dir(toXYZ(plane.value("xDirection"))));
    Sketch::Contents c;
    for (const auto& v : o.value("points").toArray()) {
        const QJsonObject p = v.toObject();
        c.points.push_back({p.value("id").toInt(), gp_Pnt2d(p.value("x").toDouble(), p.value("y").toDouble())});
    }
    for (const auto& v : o.value("entities").toArray()) {
        const QJsonObject e = v.toObject();
        const auto kind = fromName<EntityKind>(e.value("kind").toString(), kEntityKinds);
        if (!kind) {
            if (error)
                *error = QCoreApplication::translate("ProjectFile", "Unknown curve kind: %1").arg(e.value("kind").toString());
            return std::nullopt;
        }
        SketchEntity entity;
        entity.id = e.value("id").toInt();
        entity.kind = *kind;
        entity.a = e.value("a").toInt(kNoId);
        entity.b = e.value("b").toInt(kNoId);
        entity.c = e.value("c").toInt(kNoId);
        entity.radius = e.value("radius").toDouble();
        entity.construction = e.value("construction").toBool(false);
        for (const auto& p : e.value("through").toArray())
            entity.through.push_back(p.toInt(kNoId));
        entity.closed = e.value("closed").toBool(false);
        c.entities.push_back(std::move(entity));
    }
    for (const auto& v : o.value("constraints").toArray()) {
        const QJsonObject k = v.toObject();
        const auto kind = fromName<ConstraintKind>(k.value("kind").toString(), kConstraintKinds);
        if (!kind) {
            if (error)
                *error = QCoreApplication::translate("ProjectFile", "Unknown constraint kind: %1").arg(k.value("kind").toString());
            return std::nullopt;
        }
        SketchConstraint sc;
        sc.id = k.value("id").toInt();
        sc.kind = *kind;
        sc.p1 = k.value("p1").toInt(kNoId);
        sc.p2 = k.value("p2").toInt(kNoId);
        sc.e1 = k.value("e1").toInt(kNoId);
        sc.e2 = k.value("e2").toInt(kNoId);
        sc.value = k.value("value").toDouble();
        sc.target = toXY(k.value("target"));
        sc.offset = k.value("offset").toDouble();
        c.constraints.push_back(sc);
    }
    c.nextId = o.value("nextId").toInt(1);
    if (o.contains("image")) {
        const QJsonObject i = o.value("image").toObject();
        const QByteArray bytes = QByteArray::fromBase64(i.value("data").toString().toLatin1());
        ReferenceImage image;
        image.data = std::make_shared<const std::string>(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        image.pixelWidth = i.value("pixelWidth").toInt();
        image.pixelHeight = i.value("pixelHeight").toInt();
        image.corner = toXY(i.value("corner"));
        image.width = i.value("width").toDouble(100.0);
        image.opacity = i.value("opacity").toDouble(0.5);
        if (!bytes.isEmpty() && image.pixelWidth > 0 && image.pixelHeight > 0 && image.width > 0.0)
            c.image = std::move(image);
    }
    return ProjectData::SketchEntry{o.value("id").toInt(), Sketch(axes, std::move(c))};
}

QJsonObject featureToJson(const Feature& f)
{
    QJsonArray regions, faces, edges, bodies;
    for (const RegionPick& r : f.regions)
        regions.append(QJsonObject{{"sketch", r.sketch}, {"inside", xy(r.inside)}});
    for (const FacePick& p : f.faces)
        faces.append(QJsonObject{{"middle", xyz(p.middle.XYZ())}, {"normal", xyz(p.normal.XYZ())}});
    for (const EdgePick& p : f.edges)
        edges.append(QJsonObject{{"middle", xyz(p.middle.XYZ())}, {"direction", xyz(p.direction.XYZ())}});
    for (const gp_Pnt& p : f.bodies)
        bodies.append(xyz(p.XYZ()));
    QJsonArray holeAt, points;
    for (const SketchPointRef& r : f.holeAt)
        holeAt.append(QJsonObject{{"sketch", r.sketch}, {"point", r.point}});
    for (const gp_Pnt& p : f.points)
        points.append(xyz(p.XYZ()));
    return QJsonObject{
        {"kind", nameOf(f.kind, kFeatureKinds)},
        {"id", f.id},
        {"axis", QJsonObject{{"kind", nameOf(f.axis.kind, kAxisKinds)},
                             {"middle", xyz(f.axis.edge.middle.XYZ())},
                             {"direction", xyz(f.axis.edge.direction.XYZ())},
                             {"sketch", f.axis.sketch},
                             {"entity", f.axis.entity},
                             {"datum", f.axis.datum}}},
        {"plane", QJsonObject{{"kind", nameOf(f.plane.kind, kPlaneKinds)},
                              {"middle", xyz(f.plane.face.middle.XYZ())},
                              {"normal", xyz(f.plane.face.normal.XYZ())},
                              {"datum", f.plane.datum}}},
        {"pathSketch", f.pathSketch},
        {"pathEntity", f.pathEntity},
        {"pattern", nameOf(f.pattern, kPatternKinds)},
        {"count", f.count},
        {"source", f.source},
        {"hole", QJsonObject{{"kind", nameOf(f.hole.kind, kHoleKinds)},
                             {"diameter", f.hole.diameter},
                             {"depth", f.hole.depth},
                             {"headDiameter", f.hole.headDiameter},
                             {"headDepth", f.hole.headDepth}}},
        {"holeAt", holeAt},
        {"points", points},
        {"datum", nameOf(f.datum, kDatumKinds)},
        {"matrix", [&] {
             QJsonArray m;
             for (double v : f.matrix)
                 m.append(v);
             return m;
         }()},
        {"copy", f.copy},
        {"sketch", f.sketch},
        {"value", f.value},
        {"operation", nameOf(f.operation, kOperations)},
        {"boolean", nameOf(f.boolean, kBooleans)},
        {"regions", regions},
        {"faces", faces},
        {"edges", edges},
        {"bodies", bodies},
    };
}

std::optional<Feature> featureFromJson(const QJsonObject& o, QString* error)
{
    const auto kind = fromName<Feature::Kind>(o.value("kind").toString(), kFeatureKinds);
    if (!kind) {
        if (error)
            *error = QCoreApplication::translate("ProjectFile", "Unknown feature kind: %1").arg(o.value("kind").toString());
        return std::nullopt;
    }
    Feature f;
    f.kind = *kind;
    f.sketch = o.value("sketch").toInt(-1);
    f.value = o.value("value").toDouble();
    f.operation = fromName<BodyOperation>(o.value("operation").toString(), kOperations).value_or(BodyOperation::Auto);
    f.boolean = fromName<BooleanKind>(o.value("boolean").toString(), kBooleans).value_or(BooleanKind::Union);
    for (const auto& v : o.value("regions").toArray())
        f.regions.push_back({v.toObject().value("sketch").toInt(-1), toXY(v.toObject().value("inside"))});
    for (const auto& v : o.value("faces").toArray()) {
        f.faces.push_back({gp_Pnt(toXYZ(v.toObject().value("middle"))),
                           gp_Dir(toXYZ(v.toObject().value("normal")))});
    }
    for (const auto& v : o.value("edges").toArray()) {
        f.edges.push_back({gp_Pnt(toXYZ(v.toObject().value("middle"))),
                           gp_Dir(toXYZ(v.toObject().value("direction")))});
    }
    for (const auto& v : o.value("bodies").toArray())
        f.bodies.push_back(gp_Pnt(toXYZ(v)));
    // Added with solid features (M10); older files have none of these.
    f.id = o.value("id").toInt(0);
    auto direction = [](const QJsonValue& v, const gp_Dir& fallback) {
        const gp_XYZ d = toXYZ(v);
        return d.Modulus() > 1e-12 ? gp_Dir(d) : fallback;
    };
    if (o.contains("axis")) {
        const QJsonObject a = o.value("axis").toObject();
        f.axis.kind = fromName<AxisRef::Kind>(a.value("kind").toString(), kAxisKinds).value_or(AxisRef::Kind::None);
        f.axis.edge = {gp_Pnt(toXYZ(a.value("middle"))), direction(a.value("direction"), gp::DX())};
        f.axis.sketch = a.value("sketch").toInt(-1);
        f.axis.entity = a.value("entity").toInt(-1);
        f.axis.datum = a.value("datum").toInt(0);
    }
    if (o.contains("plane")) {
        const QJsonObject p = o.value("plane").toObject();
        f.plane.kind = fromName<PlaneRef::Kind>(p.value("kind").toString(), kPlaneKinds).value_or(PlaneRef::Kind::None);
        f.plane.face = {gp_Pnt(toXYZ(p.value("middle"))), direction(p.value("normal"), gp::DZ())};
        f.plane.datum = p.value("datum").toInt(0);
    }
    f.pathSketch = o.value("pathSketch").toInt(-1);
    f.pathEntity = o.value("pathEntity").toInt(-1);
    f.pattern = fromName<Feature::PatternKind>(o.value("pattern").toString(), kPatternKinds)
                    .value_or(Feature::PatternKind::Linear);
    f.count = o.value("count").toInt(2);
    f.source = o.value("source").toInt(0);
    if (o.contains("hole")) {
        const QJsonObject h = o.value("hole").toObject();
        f.hole.kind = fromName<HoleKind>(h.value("kind").toString(), kHoleKinds).value_or(HoleKind::Simple);
        f.hole.diameter = h.value("diameter").toDouble(f.hole.diameter);
        f.hole.depth = h.value("depth").toDouble(0.0);
        f.hole.headDiameter = h.value("headDiameter").toDouble(f.hole.headDiameter);
        f.hole.headDepth = h.value("headDepth").toDouble(f.hole.headDepth);
    }
    for (const auto& v : o.value("holeAt").toArray())
        f.holeAt.push_back({v.toObject().value("sketch").toInt(-1), v.toObject().value("point").toInt(-1)});
    for (const auto& v : o.value("points").toArray())
        f.points.push_back(gp_Pnt(toXYZ(v)));
    f.datum = fromName<Feature::DatumKind>(o.value("datum").toString(), kDatumKinds)
                  .value_or(Feature::DatumKind::OffsetPlane);
    for (const auto& v : o.value("matrix").toArray())
        f.matrix.push_back(v.toDouble());
    f.copy = o.value("copy").toBool(false);
    return f;
}

} // namespace

QByteArray projectToJson(const ProjectData& project)
{
    QJsonArray sketches, features;
    for (const auto& entry : project.sketches)
        sketches.append(sketchToJson(entry.id, entry.sketch));
    for (const Feature& f : project.features)
        features.append(featureToJson(f));
    QJsonArray styles;
    for (const BodyStyle& b : project.bodyStyles) {
        QJsonObject o{{"inside", xyz(b.inside.XYZ())}, {"name", QString::fromStdString(b.name)}, {"hidden", b.hidden}};
        if (b.color)
            o.insert("color", QJsonArray{(*b.color)[0], (*b.color)[1], (*b.color)[2]});
        styles.append(o);
    }
    const QJsonObject root{{"format", kFormat},     {"version", kVersion},  {"sketches", sketches},
                           {"features", features}, {"bodies", styles}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<ProjectData> projectFromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    const QJsonObject root = doc.object();
    const QString format = root.value("format").toString();
    if (parse.error != QJsonParseError::NoError || (format != kFormat && format != kFormerFormat)) {
        if (error)
            *error = QCoreApplication::translate("ProjectFile", "Not a Nibform project file");
        return std::nullopt;
    }
    if (root.value("version").toInt() > kVersion) {
        if (error)
            *error = QCoreApplication::translate("ProjectFile", "The project was saved by a newer version of the app");
        return std::nullopt;
    }
    ProjectData project;
    for (const auto& v : root.value("sketches").toArray()) {
        auto entry = sketchFromJson(v.toObject(), error);
        if (!entry)
            return std::nullopt;
        project.sketches.push_back(std::move(*entry));
    }
    for (const auto& v : root.value("features").toArray()) {
        auto feature = featureFromJson(v.toObject(), error);
        if (!feature)
            return std::nullopt;
        project.features.push_back(std::move(*feature));
    }
    for (const auto& v : root.value("bodies").toArray()) {
        const QJsonObject o = v.toObject();
        BodyStyle b;
        b.inside = gp_Pnt(toXYZ(o.value("inside")));
        b.name = o.value("name").toString().toStdString();
        b.hidden = o.value("hidden").toBool(false);
        if (o.contains("color")) {
            const QJsonArray c = o.value("color").toArray();
            b.color = std::array<double, 3>{c.at(0).toDouble(), c.at(1).toDouble(), c.at(2).toDouble()};
        }
        project.bodyStyles.push_back(std::move(b));
    }
    numberFeatures(project.features); // older files: ids in history order
    return project;
}

QString saveProject(const QString& path, const ProjectData& project)
{
    // Written next to the target and renamed into place: a failed save keeps the old file.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QCoreApplication::translate("ProjectFile", "Can't save %1: %2").arg(path, file.errorString());
    file.write(projectToJson(project));
    if (!file.commit())
        return QCoreApplication::translate("ProjectFile", "Can't save %1: %2").arg(path, file.errorString());
    return {};
}

std::optional<ProjectData> loadProject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QCoreApplication::translate("ProjectFile", "Can't open %1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    return projectFromJson(file.readAll(), error);
}
