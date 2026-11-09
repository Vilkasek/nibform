#pragma once

#include "model/Drawing.h"
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
    std::vector<Drawing> drawings;
};

QByteArray projectToJson(const ProjectData& project);
std::optional<ProjectData> projectFromJson(const QByteArray& json, QString* error = nullptr);

// The clipboard: curves of a sketch (with their relations), or bodies (as BREP text),
// to paste into this or another project.
QByteArray sketchClipboard(const Sketch::Contents& part);
std::optional<Sketch::Contents> sketchFromClipboard(const QByteArray& data);
QByteArray bodiesClipboard(const std::string& shape);
std::optional<std::string> bodiesFromClipboard(const QByteArray& data);
inline const char* kClipboardMime = "application/x-nibform";

// Returns an error message, empty on success.
QString saveProject(const QString& path, const ProjectData& project);
std::optional<ProjectData> loadProject(const QString& path, QString* error = nullptr);
