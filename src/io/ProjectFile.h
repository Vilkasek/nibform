#pragma once

#include "model/Drawing.h"
#include "model/Assembly.h"
#include "model/Features.h"
#include "model/Parameters.h"
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
    ParameterTable parameters; // and the configurations
    Assembly assembly;         // components and joints (M17)
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

// One sketch or one feature as the project file has it (JSON text), and back: what a
// recorded macro replays.
QByteArray sketchToJsonText(int id, const Sketch& sketch);
std::optional<ProjectData::SketchEntry> sketchFromJsonText(const QByteArray& json, QString* error = nullptr);
QByteArray featureToJsonText(const Feature& feature);
std::optional<Feature> featureFromJsonText(const QByteArray& json, QString* error = nullptr);

// Returns an error message, empty on success.
QString saveProject(const QString& path, const ProjectData& project);
std::optional<ProjectData> loadProject(const QString& path, QString* error = nullptr);
