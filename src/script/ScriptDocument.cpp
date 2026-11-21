#include "script/ScriptDocument.h"

#include "io/SketchExchange.h"
#include "model/Export.h"
#include "model/Parameters.h"

#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <utility>

ScriptDocument::ScriptDocument(ProjectData project)
    : m_project(std::move(project))
{
}

void ScriptDocument::reset(ProjectData project)
{
    m_project = std::move(project);
    m_built.reset();
    m_changed = false;
}

void ScriptDocument::touch()
{
    m_built.reset();
    m_changed = true;
}

int ScriptDocument::addSketch(const gp_Ax3& plane, std::optional<PlaneRef> on)
{
    int id = 1;
    for (const auto& entry : m_project.sketches)
        id = std::max(id, entry.id + 1);
    m_project.sketches.push_back({id, Sketch(plane)});
    Feature entry;
    entry.kind = Feature::Kind::Sketch;
    entry.sketch = id;
    if (on)
        entry.plane = *on;
    addFeature(entry);
    return id;
}

Sketch* ScriptDocument::sketch(int id)
{
    for (auto& entry : m_project.sketches) {
        if (entry.id == id)
            return &entry.sketch;
    }
    return nullptr;
}

const Sketch* ScriptDocument::sketch(int id) const
{
    for (const auto& entry : m_project.sketches) {
        if (entry.id == id)
            return &entry.sketch;
    }
    return nullptr;
}

SketchLookup ScriptDocument::lookup() const
{
    return [this](int id) { return sketch(id); };
}

int ScriptDocument::addFeature(Feature feature)
{
    m_project.features.push_back(std::move(feature));
    numberFeatures(m_project.features);
    touch();
    if (!m_project.features.back().expressions.empty())
        applyParameters();
    return static_cast<int>(m_project.features.size()) - 1;
}

std::vector<ParameterIssue> ScriptDocument::applyParameters()
{
    std::vector<std::pair<int, Sketch*>> sketches;
    for (auto& entry : m_project.sketches)
        sketches.emplace_back(entry.id, &entry.sketch);
    const ApplyResult result = ::applyParameters(m_project.parameters, m_project.features, sketches);
    touch();
    return result.issues;
}

const RebuildResult& ScriptDocument::built()
{
    if (!m_built) {
        // Sketches on faces follow them: the planes the rebuild found, kept.
        auto result = m_cache.rebuild(lookup(), m_project.features);
        m_built = result ? std::move(*result) : RebuildResult{};
        bool moved = false;
        for (auto& entry : m_project.sketches) {
            const auto at = m_built->context.sketchPlanes.find(entry.id);
            if (at != m_built->context.sketchPlanes.end() && !samePlane(entry.sketch.plane(), at->second)) {
                entry.sketch.setPlane(at->second);
                moved = true;
            }
        }
        if (moved) {
            result = m_cache.rebuild(lookup(), m_project.features);
            m_built = result ? std::move(*result) : RebuildResult{};
        }
    }
    return *m_built;
}

std::vector<TopoDS_Shape> ScriptDocument::bodies()
{
    return built().bodies;
}

std::vector<std::string> ScriptDocument::errors()
{
    return built().errors;
}

std::string ScriptDocument::exportTo(const std::string& path)
{
    const std::vector<TopoDS_Shape> shown = bodies();
    if (shown.empty())
        return "There are no bodies to export";
    const QString suffix = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    if (suffix == QLatin1String("step") || suffix == QLatin1String("stp"))
        return exportStep(shown, path);
    if (suffix == QLatin1String("iges") || suffix == QLatin1String("igs"))
        return exportIges(shown, path);
    if (suffix == QLatin1String("stl"))
        return exportStl(shown, path);
    if (suffix == QLatin1String("3mf")) {
        std::vector<PrintBody> print;
        for (const TopoDS_Shape& s : shown)
            print.push_back({s, "Body " + std::to_string(print.size() + 1), {0.6, 0.6, 0.65}});
        return export3mf(print, path);
    }
    return "Unknown file type (use .step, .iges, .stl or .3mf): " + path;
}

std::string ScriptDocument::exportFlatPattern(const std::string& path, int sheet)
{
    const RebuildResult& r = built();
    const std::vector<SheetFound> sheets = findSheets(r.bodies, r.context);
    if (sheet < 0 || sheet >= static_cast<int>(sheets.size()))
        return "There is no such sheet metal body";
    const SheetFound& s = sheets[sheet];
    const FlatPattern flat = unfoldSheet(r.bodies[s.body], s.fixed, s.sheet.spec);
    if (!flat.ok())
        return flat.error;
    return exportFlatPatternDxf(flat, QString::fromStdString(path)).toStdString();
}

std::string ScriptDocument::save(const std::string& path) const
{
    return saveProject(QString::fromStdString(path), m_project).toStdString();
}
