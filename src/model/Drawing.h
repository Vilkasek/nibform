#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <string>
#include <vector>

// 2D technical drawings of the bodies: sheets with projected views (first-angle
// arrangement, as ISO / Europe), section views, dimensions and notes. Sheet coordinates
// are millimetres from the sheet's lower left corner, Y up; view coordinates are model
// millimetres in the view's own plane (before its scale).

enum class ViewKind { Front, Back, Top, Bottom, Right, Left, Iso, Section };

struct SheetView
{
    ViewKind kind = ViewKind::Front;
    gp_Pnt2d at;        // the view's middle on the sheet
    double scale = 1.0; // sheet mm per model mm
    // Section: looking like `base` (an orthographic kind) at the model cut by a plane
    // across that direction, `offset` from the bodies' middle; the part toward the
    // viewer is taken away. `label` names it ("A" for A–A).
    ViewKind base = ViewKind::Front;
    double offset = 0.0;
    std::string label;
    bool hidden = true; // draw hidden edges (dashed)
};

struct SheetDimension
{
    enum class Kind { Distance, Horizontal, Vertical, Radius, Diameter };
    Kind kind = Kind::Distance;
    int view = 0;
    // What it measures, as model points found again after the model changes: corners or
    // round edges' centers; Radius / Diameter: the center (with `radius` to tell circles apart).
    gp_Pnt a, b;
    double radius = 0.0;
    double offset = 10.0; // sheet mm from what it measures to the dimension line (or the label, for radii)
    double angle = 45.0;  // Radius / Diameter: the leader's direction, degrees
};

struct SheetNote
{
    gp_Pnt2d at; // top left of the text
    std::string text;
    double height = 3.5; // mm
};

struct Drawing
{
    std::string title;
    double width = 297.0; // A4 landscape
    double height = 210.0;
    std::vector<SheetView> views;
    std::vector<SheetDimension> dimensions;
    std::vector<SheetNote> notes;
};

using Polyline2d = std::vector<gp_Pnt2d>;

// A view's lines and what dimensions can attach to.
struct ViewGeometry
{
    std::vector<Polyline2d> visible, hidden;
    std::vector<std::pair<gp_Pnt2d, gp_Pnt2d>> hatch; // section faces' hatching
    std::vector<Polyline2d> cut;                       // section faces' outlines
    struct Anchor
    {
        gp_Pnt model;
        gp_Pnt2d at;
    };
    std::vector<Anchor> corners;
    struct Circle
    {
        gp_Pnt center;
        gp_Pnt2d at;
        double radius = 0.0;
        bool full = true;  // a whole circle (else an arc: dimensioned by its radius)
        bool round = true; // seen face on (as a circle, not as a line or an ellipse)
    };
    std::vector<Circle> circles;
    gp_Pnt2d min, max; // extent, in view coordinates
    bool empty() const { return visible.empty() && hidden.empty() && cut.empty(); }
    gp_Pnt2d middle() const { return gp_Pnt2d((min.X() + max.X()) / 2.0, (min.Y() + max.Y()) / 2.0); }
};

// The view's plane: its direction points at the viewer, X right and Y up on the sheet.
gp_Ax2 viewAxes(ViewKind kind);
// Where a model point lands in a view (view coordinates).
gp_Pnt2d projectPoint(ViewKind kind, const gp_Pnt& p);

// Projects the bodies as the view sees them, hidden edges apart; a section view cuts them first.
ViewGeometry projectView(const std::vector<TopoDS_Shape>& bodies, const SheetView& view);

// Lines across closed loops (even-odd: holes stay empty), `spacing` apart at `angleDeg`.
std::vector<std::pair<gp_Pnt2d, gp_Pnt2d>> hatchLoops(const std::vector<Polyline2d>& loops, double spacing,
                                                       double angleDeg = 45.0);

// A view point on the sheet, and back.
gp_Pnt2d toSheet(const SheetView& view, const ViewGeometry& geometry, const gp_Pnt2d& p);
gp_Pnt2d fromSheet(const SheetView& view, const ViewGeometry& geometry, const gp_Pnt2d& p);

// The largest standard scale (1:1, 1:2, 2:1, ...) at which `width` × `height` model mm fit `room`.
double fittingScale(double width, double height, double roomWidth, double roomHeight);
// "1:2", "2:1"
std::string scaleText(double scale);

// A new drawing of the bodies: front, top (below it), left (right of it) and isometric
// views on an A4 / A3 sheet, at a standard scale that fits.
Drawing defaultDrawing(const std::vector<TopoDS_Shape>& bodies, const std::string& title);

// Where the dimension's points are now: the corner / circle center of the view nearest
// to each stored point. False when the view has nothing near enough (the dimension lost
// what it measured).
bool resolveDimension(const SheetDimension& dimension, const ViewGeometry& geometry, gp_Pnt2d& a, gp_Pnt2d& b,
                      double& radius, SheetDimension* followed = nullptr);
// The value it shows (model mm): the distance, its horizontal / vertical part, the radius or diameter.
double dimensionValue(const SheetDimension& dimension, const gp_Pnt2d& a, const gp_Pnt2d& b, double radius);
