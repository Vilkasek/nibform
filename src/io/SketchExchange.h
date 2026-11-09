#pragma once

#include "sketch/Sketch.h"

#include <QByteArray>
#include <QString>

#include <optional>
#include <vector>

// 2D drawings in and out of a sketch: DXF (ASCII, R12-style entities) and SVG.
// Sketch coordinates are millimetres with Y up; SVG's Y runs down, so it is flipped.

// Writes the sketch's curves (construction ones on their own layer / dashed); returns an
// error message, empty on success.
QString exportSketchDxf(const Sketch& sketch, const QString& path);
QString exportSketchSvg(const Sketch& sketch, const QString& path);
QByteArray sketchToDxf(const Sketch& sketch);
QByteArray sketchToSvg(const Sketch& sketch);

// Adds the drawing's curves to the sketch: DXF lines, circles, arcs, (lightweight)
// polylines with bulges, splines, ellipses and points, in the file's units; SVG paths
// (lines, arcs, curves), lines, rectangles, circles, ellipses and polylines, with their
// transforms, scaled to millimetres. Curves become splines; touching ends are joined.
// Returns the new curves, or empty with `error` set.
std::optional<std::vector<EntityId>> importSketchFile(Sketch& sketch, const QString& path, QString* error);
std::optional<std::vector<EntityId>> importDxf(Sketch& sketch, const QByteArray& data, QString* error);
std::optional<std::vector<EntityId>> importSvg(Sketch& sketch, const QByteArray& data, QString* error);
