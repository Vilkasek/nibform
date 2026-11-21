#pragma once

#include "model/Solids.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <string>
#include <vector>

// Sheet metal (M18): parts bent from a flat sheet of one thickness. They are ordinary
// solids made of flat walls joined by round bends, so every other feature (holes, cuts,
// fillets) works on them too; the flat pattern is found from the finished solid by
// rolling its bends out, so it shows everything done to it.

// The rules a sheet is bent by.
struct SheetSpec
{
    double thickness = 1.0;
    double radius = 1.0;   // inner bend radius
    double kFactor = 0.44; // where the neutral layer lies, as a fraction of the thickness from the inside of a bend
    bool operator==(const SheetSpec&) const = default;
};

// How an edge flange (or a hem) is bent, and how a corner relief is shaped.
struct FlangeSpec
{
    double angle = 90.0; // degrees from the sheet it is bent from
    double inset = 0.0;  // left out at both ends of the edge
    double gap = 0.5;    // hem: between the sheet and the part folded back onto it
    // Material inside: the flange's outside lies where the sheet's end was (the sheet
    // gives way to the bend, with reliefs where it is inset); otherwise the bend is added
    // beyond the end.
    bool inside = false;
    bool square = false; // corner relief: square, else round
    bool operator==(const FlangeSpec&) const = default;
};

// A face of the sheet, to find it again: a point on it and its outward normal.
struct SheetFace
{
    gp_Pnt middle;
    gp_Dir normal;
};

// Whether a face of a sheet `thickness` thick is one of its narrow edges (an end, a
// hole's wall) rather than one of its two sides.
bool isThicknessFace(const TopoDS_Face& face, double thickness);

// A sheet bent along an open path of lines and arcs on a plane (normal `normal`): the
// path is one side of the sheet, the material to its left looking along `normal` (to its
// right with `flip`), `depth` deep along `normal`. Corners between lines are bent with
// the spec's inner radius. `fixed` gets the side along the path's first piece.
std::optional<TopoDS_Shape> contourSheet(const TopoDS_Wire& path, const gp_Dir& normal, double depth,
                                         const SheetSpec& spec, bool flip, SheetFace& fixed, std::string& error);

// A flange bent up from `edge` of body `body`: a straight edge where a side of the sheet
// meets one of its ends. It bends toward the side the edge was picked on (the other way
// with `flip`) by `flange.angle`, `length` long measured outside (to where the outsides
// of the sheet and the flange would meet). A hem folds the end back flat onto the sheet,
// `flange.gap` from it, `length` long.
ModelResult edgeFlange(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Edge& edge,
                       const SheetSpec& spec, double length, const FlangeSpec& flange, bool flip, bool hem);

// A round (or square) hole `size` across through the sheet at `corner` of body `body`,
// cut square to the largest flat side there: room for the bends that meet at a corner.
ModelResult cornerRelief(const std::vector<TopoDS_Shape>& bodies, int body, const gp_Pnt& corner,
                         const SheetSpec& spec, double size, bool square);

// A shelled solid (walls of the spec's thickness) made into sheet metal: the walls that
// meet `fixed`'s, and so on outward, are bent to each other with the spec's radius; where
// walls meet otherwise they are ripped apart so that it unfolds.
ModelResult convertToSheet(const std::vector<TopoDS_Shape>& bodies, int body, const TopoDS_Face& fixed,
                           const SheetSpec& spec);

// How thick the wall behind a flat face is (the nearest face straight behind it).
std::optional<double> wallThickness(const TopoDS_Shape& body, const TopoDS_Face& face);

// --- The flat pattern ---------------------------------------------------------------

struct FlatBend
{
    gp_Pnt2d a, b;       // the bend line (the middle of the bend zone)
    double angle = 0.0;  // degrees
    double radius = 0.0; // inner radius
    bool up = true;      // toward the viewer looking at the flat pattern from above
};

// The sheet laid flat: in the XY plane, the side of the fixed face up (at z 0), the
// sheet below it, its lower left corner at the origin.
struct FlatPattern
{
    TopoDS_Shape solid;
    TopoDS_Face face; // its top: the outline to cut
    std::vector<FlatBend> bends;
    double width = 0.0, height = 0.0;
    std::string error; // empty when it unfolded
    bool ok() const { return error.empty(); }
};

// Unfolds a sheet metal solid: its walls are found from `fixed` (a flat side of it, which
// stays as it lies) through the bends joining them, each bend rolled out along its
// neutral layer (k-factor).
FlatPattern unfoldSheet(const TopoDS_Shape& body, const TopoDS_Face& fixed, const SheetSpec& spec);
