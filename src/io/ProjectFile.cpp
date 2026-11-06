#include "io/ProjectFile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <array>

namespace {

constexpr int kVersion = 1;
const QString kFormat = QStringLiteral("shapr-linux-project");

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

constexpr std::array<const char*, 3> kEntityKinds{"line", "circle", "arc"};
constexpr std::array<const char*, 16> kConstraintKinds{
    "horizontal", "vertical", "parallel", "perpendicular", "equal",     "tangent", "concentric", "pointOn",
    "midpoint",   "fixed",    "distance", "radius",        "diameter", "angle",   "pointLine",  "lineLine"};
constexpr std::array<const char*, 7> kFeatureKinds{"sketch",  "extrude", "fillet", "chamfer",
                                                   "combine", "delete",  "shell"};
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
        entities.append(QJsonObject{{"id", e.id},
                                    {"kind", nameOf(e.kind, kEntityKinds)},
                                    {"a", e.a},
                                    {"b", e.b},
                                    {"c", e.c},
                                    {"radius", e.radius},
                                    {"construction", e.construction}});
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
    return QJsonObject{
        {"id", id},
        {"plane", QJsonObject{{"origin", xyz(plane.Location().XYZ())},
                              {"normal", xyz(plane.Direction().XYZ())},
                              {"xDirection", xyz(plane.XDirection().XYZ())}}},
        {"points", points},
        {"entities", entities},
        {"constraints", constraints},
        {"nextId", c.nextId},
    };
}

std::optional<ProjectData::SketchEntry> sketchFromJson(const QJsonObject& o, QString* error)
{
    const QJsonObject plane = o.value("plane").toObject();
    const gp_Ax3 axes(gp_Pnt(toXYZ(plane.value("origin"))), gp_Dir(toXYZ(plane.value("normal"))),
                      gp_Dir(toXYZ(plane.value("xDirection"))));
    Sketch::Contents c;
    for (const QJsonValue& v : o.value("points").toArray()) {
        const QJsonObject p = v.toObject();
        c.points.push_back({p.value("id").toInt(), gp_Pnt2d(p.value("x").toDouble(), p.value("y").toDouble())});
    }
    for (const QJsonValue& v : o.value("entities").toArray()) {
        const QJsonObject e = v.toObject();
        const auto kind = fromName<EntityKind>(e.value("kind").toString(), kEntityKinds);
        if (!kind) {
            if (error)
                *error = QStringLiteral("Nieznany rodzaj krzywej: %1").arg(e.value("kind").toString());
            return std::nullopt;
        }
        c.entities.push_back({e.value("id").toInt(), *kind, e.value("a").toInt(kNoId), e.value("b").toInt(kNoId),
                              e.value("c").toInt(kNoId), e.value("radius").toDouble(),
                              e.value("construction").toBool(false)});
    }
    for (const QJsonValue& v : o.value("constraints").toArray()) {
        const QJsonObject k = v.toObject();
        const auto kind = fromName<ConstraintKind>(k.value("kind").toString(), kConstraintKinds);
        if (!kind) {
            if (error)
                *error = QStringLiteral("Nieznany rodzaj więzu: %1").arg(k.value("kind").toString());
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
    return QJsonObject{
        {"kind", nameOf(f.kind, kFeatureKinds)},
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
            *error = QStringLiteral("Nieznany rodzaj operacji: %1").arg(o.value("kind").toString());
        return std::nullopt;
    }
    Feature f;
    f.kind = *kind;
    f.sketch = o.value("sketch").toInt(-1);
    f.value = o.value("value").toDouble();
    f.operation = fromName<BodyOperation>(o.value("operation").toString(), kOperations).value_or(BodyOperation::Auto);
    f.boolean = fromName<BooleanKind>(o.value("boolean").toString(), kBooleans).value_or(BooleanKind::Union);
    for (const QJsonValue& v : o.value("regions").toArray())
        f.regions.push_back({v.toObject().value("sketch").toInt(-1), toXY(v.toObject().value("inside"))});
    for (const QJsonValue& v : o.value("faces").toArray()) {
        f.faces.push_back({gp_Pnt(toXYZ(v.toObject().value("middle"))),
                           gp_Dir(toXYZ(v.toObject().value("normal")))});
    }
    for (const QJsonValue& v : o.value("edges").toArray()) {
        f.edges.push_back({gp_Pnt(toXYZ(v.toObject().value("middle"))),
                           gp_Dir(toXYZ(v.toObject().value("direction")))});
    }
    for (const QJsonValue& v : o.value("bodies").toArray())
        f.bodies.push_back(gp_Pnt(toXYZ(v)));
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
    const QJsonObject root{{"format", kFormat}, {"version", kVersion}, {"sketches", sketches}, {"features", features}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<ProjectData> projectFromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    const QJsonObject root = doc.object();
    if (parse.error != QJsonParseError::NoError || root.value("format").toString() != kFormat) {
        if (error)
            *error = QStringLiteral("To nie jest plik projektu Shapr Linux");
        return std::nullopt;
    }
    if (root.value("version").toInt() > kVersion) {
        if (error)
            *error = QStringLiteral("Projekt zapisany nowszą wersją programu");
        return std::nullopt;
    }
    ProjectData project;
    for (const QJsonValue& v : root.value("sketches").toArray()) {
        auto entry = sketchFromJson(v.toObject(), error);
        if (!entry)
            return std::nullopt;
        project.sketches.push_back(std::move(*entry));
    }
    for (const QJsonValue& v : root.value("features").toArray()) {
        auto feature = featureFromJson(v.toObject(), error);
        if (!feature)
            return std::nullopt;
        project.features.push_back(std::move(*feature));
    }
    return project;
}

QString saveProject(const QString& path, const ProjectData& project)
{
    // Written next to the target and renamed into place: a failed save keeps the old file.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Nie można zapisać %1: %2").arg(path, file.errorString());
    file.write(projectToJson(project));
    if (!file.commit())
        return QStringLiteral("Nie można zapisać %1: %2").arg(path, file.errorString());
    return {};
}

std::optional<ProjectData> loadProject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Nie można otworzyć %1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    return projectFromJson(file.readAll(), error);
}
