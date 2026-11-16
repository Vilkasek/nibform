#include "io/ComponentFiles.h"

#include "io/ProjectFile.h"
#include "model/Features.h"

#include <BRep_Builder.hxx>
#include <QCoreApplication>
#include <TopoDS_Compound.hxx>

#include <map>

std::optional<TopoDS_Shape> projectShape(const QString& path, QString* error)
{
    const std::optional<ProjectData> project = loadProject(path, error);
    if (!project)
        return std::nullopt;
    std::map<int, Sketch> sketches;
    for (const ProjectData::SketchEntry& e : project->sketches)
        sketches.emplace(e.id, e.sketch);
    const SketchLookup lookup = [&](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    std::vector<Feature> features = project->features;
    numberFeatures(features);
    const RebuildResult built = rebuild(lookup, features);

    // Its shown bodies (as its body list says).
    const std::vector<int> styles = matchBodyStyles(built.bodies, project->bodyStyles);
    std::vector<TopoDS_Shape> shown;
    for (std::size_t i = 0; i < built.bodies.size(); ++i) {
        if (styles[i] < 0 || !project->bodyStyles[styles[i]].hidden)
            shown.push_back(built.bodies[i]);
    }
    if (shown.empty()) {
        if (error)
            *error = QCoreApplication::translate("Model", "The project has no body to use");
        return std::nullopt;
    }
    if (shown.size() == 1)
        return shown.front();
    TopoDS_Compound all;
    BRep_Builder builder;
    builder.MakeCompound(all);
    for (const TopoDS_Shape& body : shown)
        builder.Add(all, body);
    return TopoDS_Shape(all);
}
