#pragma once

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

// Writes the bodies to an exchange file; returns an error message, empty on success.
std::string exportStep(const std::vector<TopoDS_Shape>& bodies, const std::string& path);
// Binary STL, meshed finely enough for printing (0.05 mm, 0.5°).
std::string exportStl(const std::vector<TopoDS_Shape>& bodies, const std::string& path);

// Reads the solids of a STEP file (used to check exports, and for importing later).
std::vector<TopoDS_Shape> importStep(const std::string& path, std::string* error = nullptr);
