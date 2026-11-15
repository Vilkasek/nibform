#pragma once

#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Sketch text as geometry: the outlines of its letters, from the system's fonts (through
// FreeType). Shapes are made once per text, font and size and reused (also from the
// rebuild thread).

// The edges of the letters' outlines, on the sketch plane.
std::vector<TopoDS_Edge> textEdges(const Sketch& sketch, const SketchText& text);
// The same outlines in sketch coordinates, as closed polylines (to draw and pick).
std::vector<std::vector<gp_Pnt2d>> textOutlines(const Sketch& sketch, const SketchText& text);
// The box around the letters, in sketch coordinates (lower left, upper right); empty for
// a text with nothing to show.
std::optional<std::pair<gp_Pnt2d, gp_Pnt2d>> textBounds(const Sketch& sketch, const SketchText& text);
// Whether `p` (sketch coordinates) is inside one of the text's letters (not in a
// letter's hole, nor between letters).
bool textContains(const Sketch& sketch, const SketchText& text, const gp_Pnt2d& p);
// The text whose box holds `p` (within `tolerance`), if any: the last drawn first.
std::optional<EntityId> textAt(const Sketch& sketch, const gp_Pnt2d& p, double tolerance);

// The font families text can use, sorted.
std::vector<std::string> fontFamilies();
