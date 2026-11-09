#pragma once

#include "model/Features.h"
#include "sketch/Sketch.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

// What a .nibform project holds: the sketches and the feature history. Bodies are not
// stored; opening a project rebuilds them, exactly as editing does.
struct ProjectData
{
    struct SketchEntry
    {
        int id = -1;
        Sketch sketch;
    };
    std::vector<SketchEntry> sketches;
    std::vector<Feature> features;
    std::vector<BodyStyle> bodyStyles; // names, visibility, colors of bodies
};

QByteArray projectToJson(const ProjectData& project);
std::optional<ProjectData> projectFromJson(const QByteArray& json, QString* error = nullptr);

// Returns an error message, empty on success.
QString saveProject(const QString& path, const ProjectData& project);
std::optional<ProjectData> loadProject(const QString& path, QString* error = nullptr);
