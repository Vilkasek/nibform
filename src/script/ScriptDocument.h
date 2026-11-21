#pragma once

#include "io/ProjectFile.h"
#include "model/Rebuild.h"

#include <optional>
#include <string>
#include <vector>

// A project as a script works on it (M19): opened from a file, made new, or the window's
// own (taken before the script runs, given back as one undo step after). Changes are
// rebuilt when the script asks for bodies, errors or an export.
class ScriptDocument
{
public:
    ScriptDocument() = default;
    explicit ScriptDocument(ProjectData project);

    const ProjectData& project() const { return m_project; }
    // Replaces it all (no change of its own: the window's project as it is now).
    void reset(ProjectData project);
    bool changed() const { return m_changed; }
    void clearChanged() { m_changed = false; }
    // Something was changed through project() handles: rebuild before showing anything.
    void touch();

    // --- Sketches ---------------------------------------------------------------------
    // A new sketch on the plane, its entry at the end of the history; its id.
    int addSketch(const gp_Ax3& plane, std::optional<PlaneRef> on = std::nullopt);
    Sketch* sketch(int id);
    const Sketch* sketch(int id) const;
    SketchLookup lookup() const;

    // --- Features ---------------------------------------------------------------------
    // Added at the end of the history, numbered; values with expressions follow the
    // parameters at once. Its index.
    int addFeature(Feature feature);
    std::vector<Feature>& features() { return m_project.features; }

    // --- Parameters and configurations --------------------------------------------------
    ParameterTable& parameters() { return m_project.parameters; }
    // Every value with an expression from the parameters (after they changed); the issues.
    std::vector<ParameterIssue> applyParameters();

    // --- Results ----------------------------------------------------------------------
    const RebuildResult& built();
    std::vector<TopoDS_Shape> bodies();
    std::vector<std::string> errors(); // one per feature, empty when it applied

    // Exports by the file's suffix (.step / .stp, .iges / .igs, .stl, .3mf); an error, empty on success.
    std::string exportTo(const std::string& path);
    // The flat pattern of sheet metal body `sheet` (in findSheets() order) as DXF.
    std::string exportFlatPattern(const std::string& path, int sheet = 0);
    std::string save(const std::string& path) const;

private:
    ProjectData m_project;
    RebuildCache m_cache;
    std::optional<RebuildResult> m_built;
    bool m_changed = false;
};
