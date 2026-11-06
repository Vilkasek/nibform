#pragma once

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
