#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_XYZ.hxx>

#include <optional>

// Measuring what is picked: one face, edge, corner or body, or the relation of two.
struct Measurement
{
    // One item
    std::optional<double> length;   // an edge
    std::optional<double> area;     // a face
    std::optional<double> radius;   // a round edge, a cylindrical / spherical face
    std::optional<gp_Pnt> position; // a corner
    // Two items
    std::optional<double> distance; // nearest points ...
    gp_Pnt from, to;                // ... these
    std::optional<double> angle;    // degrees: between two flat faces / straight edges, or a face and an edge
    std::optional<double> centers;  // between the centers of two round edges / faces
};

Measurement measure(const TopoDS_Shape& item);
Measurement measure(const TopoDS_Shape& a, const TopoDS_Shape& b);

struct MassProperties
{
    double volume = 0.0;  // mm³
    double area = 0.0;    // mm², of the surface
    gp_Pnt center;        // of mass, for a uniform density
    gp_XYZ size;          // of the axis-aligned bounding box
    gp_XYZ inertia;       // principal moments of inertia, per unit density (mm⁵)
};

MassProperties massProperties(const TopoDS_Shape& body);

// The move that lines `from` (part of a body to move) up with `to`: corners and round
// edges' centers meet; a flat face is laid flush against another (facing it); a point
// lands on a face's plane. Empty when the pair doesn't say how.
std::optional<gp_Trsf> alignment(const TopoDS_Shape& from, const TopoDS_Shape& to);
