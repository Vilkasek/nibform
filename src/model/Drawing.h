#pragma once

#include "model/SheetMetal.h"
#include "model/Threads.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

#include <map>
#include <optional>
#include <string>
#include <vector>

// 2D technical drawings of the bodies: sheets with projected views (first-angle
// arrangement, as ISO / Europe), section views, dimensions and notes. Sheet coordinates
// are millimetres from the sheet's lower left corner, Y up; view coordinates are model
// millimetres in the view's own plane (before its scale).

enum class ViewKind { Front, Back, Top, Bottom, Right, Left, Iso, Section, Flat };

// Flat: the sheet metal bodies' flat patterns (M18), side by side, seen from above, with
// their bend lines and notes.
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
    // An assembly drawing's view of the parts apart (as the exploded view has them), with
    // balloons numbering them.
    bool exploded = false;
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
    // Of the assembly's parts instead of the part model's bodies (M17), with a parts list
    // (above the title block).
    bool assembly = false;
    bool partsList = false;
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
        std::string thread; // a threaded hole's or bolt's callout ("M8×1.25"), else empty
    };
    std::vector<Circle> circles;
    // Cosmetic threads, drawn thin: a hole's major diameter or a bolt's minor one (three
    // quarters of a circle seen end on, two lines seen from the side).
    std::vector<Polyline2d> threads;
    // A flat pattern's bends: the bend line, and what its note says.
    struct Bend
    {
        gp_Pnt2d a, b;
        double degrees = 0.0;
        double radius = 0.0;
        bool up = true;
    };
    std::vector<Bend> bends;
    gp_Pnt2d min, max; // extent, in view coordinates
    bool empty() const { return visible.empty() && hidden.empty() && cut.empty(); }
    gp_Pnt2d middle() const { return gp_Pnt2d((min.X() + max.X()) / 2.0, (min.Y() + max.Y()) / 2.0); }
};

// The view's plane: its direction points at the viewer, X right and Y up on the sheet.
gp_Ax2 viewAxes(ViewKind kind);
// Where a model point lands in a view (view coordinates).
gp_Pnt2d projectPoint(ViewKind kind, const gp_Pnt& p);

// A parts list (bill of materials) row: the parts made from one source.
struct PartsListRow
{
    int item = 0; // numbered in the order the parts come
    std::string name;
    int quantity = 0;
};
// The rows for parts given by their source (id, name) in order; `itemOf` gets each
// source's item number.
std::vector<PartsListRow> partsList(const std::vector<std::pair<int, std::string>>& parts,
                                    std::map<int, int>* itemOf = nullptr);

// A balloon: an item number in a circle at `at`, its leader ending at `anchor` (view
// coordinates).
struct Balloon
{
    int item = 0;
    gp_Pnt2d anchor, at;
};
// One balloon per item, on the first part of it (`anchors`: item and where the part is in
// the view), set out from the view's middle by `distance` (view mm) so the leaders fan out.
std::vector<Balloon> placeBalloons(const ViewGeometry& view, const std::vector<std::pair<int, gp_Pnt2d>>& anchors,
                                   double distance);

// Projects the bodies as the view sees them, hidden edges apart; a section view cuts them
// first. The threads on them are drawn and called out (not in section or isometric views).
ViewGeometry projectView(const std::vector<TopoDS_Shape>& bodies, const SheetView& view,
                         const std::vector<ThreadMark>& threads = {});

// The flat patterns side by side (in order, `gap` model mm apart), as the view sees them
// from above, with their bend lines.
ViewGeometry projectFlat(const std::vector<FlatPattern>& flats, const SheetView& view, double gap = 10.0);

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
// A new drawing of an assembly: the parts together and apart (isometric views side by
// side, balloons on the exploded one) and the parts list.
Drawing assemblyDrawing(const std::vector<TopoDS_Shape>& together, const std::vector<TopoDS_Shape>& apart,
                        const std::string& title);

// Where the dimension's points are now: the corner / circle center of the view nearest
// to each stored point. False when the view has nothing near enough (the dimension lost
// what it measured).
bool resolveDimension(const SheetDimension& dimension, const ViewGeometry& geometry, gp_Pnt2d& a, gp_Pnt2d& b,
                      double& radius, SheetDimension* followed = nullptr);
// The value it shows (model mm): the distance, its horizontal / vertical part, the radius or diameter.
double dimensionValue(const SheetDimension& dimension, const gp_Pnt2d& a, const gp_Pnt2d& b, double radius);
