#pragma once

#include <TopoDS_Shape.hxx>

#include <array>
#include <optional>
#include <string>
#include <vector>

// Writes the bodies to an exchange file; returns an error message, empty on success.
std::string exportStep(const std::vector<TopoDS_Shape>& bodies, const std::string& path);
// Binary STL, meshed finely enough for printing (0.05 mm, 0.5°).
std::string exportStl(const std::vector<TopoDS_Shape>& bodies, const std::string& path);

std::string exportIges(const std::vector<TopoDS_Shape>& bodies, const std::string& path);

// 3MF for 3D printing: one object per body (meshed as for STL), with its name and color
// (sRGB 0..1); coordinates in millimetres, or inches with `inches`.
struct PrintBody
{
    TopoDS_Shape shape;
    std::string name;
    std::array<double, 3> color{0.6, 0.6, 0.65};
};
std::string export3mf(const std::vector<PrintBody>& bodies, const std::string& path, bool inches = false);

// Reads the solids of a STEP file (used to check exports).
std::vector<TopoDS_Shape> importStep(const std::string& path, std::string* error = nullptr);

// What a STEP, IGES or STL file holds (by its extension). An STL mesh is sewn into a
// solid when it is closed (and not too big to work with as one), otherwise it stays a
// shell, good as a reference. Empty (with `error`) when it can't be read.
std::optional<TopoDS_Shape> importShape(const std::string& path, std::string& error);

// Shapes as text (OpenCASCADE's BREP format), to keep imported geometry in a project.
std::string shapeToText(const TopoDS_Shape& shape);
std::optional<TopoDS_Shape> shapeFromText(const std::string& text);
