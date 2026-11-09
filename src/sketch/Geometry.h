#pragma once

#include "sketch/Sketch.h"

#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_Curve.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec2d.hxx>

#include <optional>
#include <vector>

// Intersections of infinite lines (point + direction) and full circles.
// Tangent cases count as one point; parallel or concentric ones as none.

std::optional<gp_Pnt2d> intersectLines(const gp_Pnt2d& p1, const gp_Vec2d& d1, const gp_Pnt2d& p2,
                                       const gp_Vec2d& d2);

std::vector<gp_Pnt2d> intersectLineCircle(const gp_Pnt2d& p, const gp_Vec2d& d, const gp_Pnt2d& center,
                                          double radius);

std::vector<gp_Pnt2d> intersectCircles(const gp_Pnt2d& c1, double r1, const gp_Pnt2d& c2, double r2);

// Interpolating B-spline through `points` (chord-length parametrized, periodic when
// `closed`); repeated neighbours are dropped. When interpolation isn't possible (two
// points, or it fails), the straight polyline through them as a degree-1 B-spline.
// Null for fewer than two distinct points.
Handle(Geom2d_BSplineCurve) interpolateSpline(const std::vector<gp_Pnt2d>& points, bool closed);

// The curve as an OCCT 2D curve, bounded to its extent (full for circles and ellipses).
Handle(Geom2d_Curve) curve2d(const Sketch& sketch, const SketchEntity& entity);

// Where two curves cross, within their extents (any kinds; tangent touches count once).
std::vector<gp_Pnt2d> intersectCurves(const Sketch& sketch, const SketchEntity& a, const SketchEntity& b);
