// Drawings (M13): projected views with hidden edges, sections and their hatching,
// layout and scales, dimensions that follow the model.

#include "Check.h"

#include "model/Drawing.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

// A 60 × 40 × 10 plate (corner at the origin) with a 10 mm hole through it at (20, 20).
TopoDS_Shape plate(double holeX = 20.0)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(60, 40, 10).Shape();
    const TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(holeX, 20, -1), gp::DZ()), 5.0, 12.0).Shape();
    return BRepAlgoAPI_Cut(box, hole).Shape();
}

SheetView view(ViewKind kind)
{
    SheetView v;
    v.kind = kind;
    v.at = gp_Pnt2d(100, 100);
    return v;
}

double lengthOf(const std::vector<Polyline2d>& lines)
{
    double total = 0.0;
    for (const Polyline2d& l : lines) {
        for (std::size_t i = 1; i < l.size(); ++i)
            total += l[i - 1].Distance(l[i]);
    }
    return total;
}

void testViewDirections()
{
    // Front: X right, Z up; top: X right, Y up; left: -Y right, Z up.
    CHECK(near(projectPoint(ViewKind::Front, gp_Pnt(1, 2, 3)), gp_Pnt2d(1, 3)));
    CHECK(near(projectPoint(ViewKind::Top, gp_Pnt(1, 2, 3)), gp_Pnt2d(1, 2)));
    CHECK(near(projectPoint(ViewKind::Left, gp_Pnt(1, 2, 3)), gp_Pnt2d(-2, 3)));
    CHECK(near(projectPoint(ViewKind::Right, gp_Pnt(1, 2, 3)), gp_Pnt2d(2, 3)));
    CHECK(projectPoint(ViewKind::Iso, gp_Pnt(0, 0, 1)).Y() > 0.0); // up stays up
}

void testTopViewSeesTheHole()
{
    const ViewGeometry g = projectView({plate()}, view(ViewKind::Top));
    CHECK(!g.visible.empty());
    CHECK(near(g.max.X() - g.min.X(), 60.0, 1e-6));
    CHECK(near(g.max.Y() - g.min.Y(), 40.0, 1e-6));
    // The outline and the hole: 2 × (60 + 40) + 10π of visible edges.
    CHECK(near(lengthOf(g.visible), 200.0 + 10.0 * kPi, 0.05));
    CHECK(g.hidden.empty());
    // The hole's rims are round here, at (20, 20).
    bool rim = false;
    for (const ViewGeometry::Circle& c : g.circles)
        rim |= c.round && c.full && near(c.radius, 5.0, 1e-9) && near(c.at, gp_Pnt2d(20, 20), 1e-9);
    CHECK(rim);
    CHECK(g.corners.size() >= 8);
}

void testFrontViewHidesTheHole()
{
    // From the front the hole is two hidden lines through the plate.
    const ViewGeometry g = projectView({plate()}, view(ViewKind::Front));
    CHECK(near(g.max.X() - g.min.X(), 60.0, 1e-6));
    CHECK(near(g.max.Y() - g.min.Y(), 10.0, 1e-6));
    CHECK(near(lengthOf(g.hidden), 20.0, 0.05));
    SheetView plain = view(ViewKind::Front);
    plain.hidden = false;
    CHECK(projectView({plate()}, plain).hidden.empty());
}

void testSectionThroughTheHole()
{
    // Cut through the hole's middle (y = 20 is the plate's middle too), seen from the front.
    SheetView section = view(ViewKind::Section);
    section.base = ViewKind::Front;
    section.offset = 0.0;
    section.scale = 1.0;
    const ViewGeometry g = projectView({plate()}, section);
    CHECK(g.cut.size() == 2);           // two pieces either side of the hole
    CHECK(!g.hatch.empty());
    CHECK(g.hidden.empty() || lengthOf(g.hidden) < 1e-6); // nothing hides behind the cut
    // The hatching covers the cut faces: (60 - 10) × 10 mm² at 2.5 mm spacing ≈ 200 mm of lines.
    double hatched = 0.0;
    for (const auto& [a, b] : g.hatch)
        hatched += a.Distance(b);
    CHECK(near(hatched, 500.0 / 2.5, 25.0));
}

void testHatching()
{
    // A 10 × 10 square with a 4 × 4 hole: lines only where there's material.
    const Polyline2d outer{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const Polyline2d inner{{3, 3}, {7, 3}, {7, 7}, {3, 7}};
    const auto lines = hatchLoops({outer, inner}, 1.0, 0.0);
    double total = 0.0;
    for (const auto& [a, b] : lines) {
        total += a.Distance(b);
        CHECK(!(a.X() > 3.0 + 1e-9 && b.X() < 7.0 - 1e-9 && a.Y() > 3.0 && a.Y() < 7.0)); // never inside the hole
    }
    CHECK(near(total, 100.0 - 16.0, 1e-9));
}

void testScales()
{
    CHECK(near(fittingScale(100, 50, 250, 150), 2.0));
    CHECK(near(fittingScale(400, 50, 250, 150), 0.5));
    CHECK(scaleText(2.0) == "2:1");
    CHECK(scaleText(0.2) == "1:5");
    CHECK(scaleText(1.0) == "1:1");
}

void testDefaultDrawing()
{
    const Drawing d = defaultDrawing({plate()}, "Plate");
    CHECK(d.title == "Plate");
    CHECK(d.views.size() == 4);
    CHECK(d.views[0].kind == ViewKind::Front && d.views[1].kind == ViewKind::Top);
    // First-angle: the top view below the front one, the left view to its right.
    CHECK(d.views[1].at.Y() < d.views[0].at.Y() && near(d.views[1].at.X(), d.views[0].at.X()));
    CHECK(d.views[2].at.X() > d.views[0].at.X() && near(d.views[2].at.Y(), d.views[0].at.Y()));
    for (const SheetView& v : d.views) {
        CHECK(v.at.X() > 0 && v.at.X() < d.width && v.at.Y() > 0 && v.at.Y() < d.height);
        CHECK(near(v.scale, d.views[0].scale));
    }
    // A big part gets a smaller scale (and a bigger sheet when it must).
    const Drawing big = defaultDrawing({BRepPrimAPI_MakeBox(900, 300, 200).Shape()}, "");
    CHECK(big.views[0].scale < 0.5);
}

void testDimensionsFollowTheModel()
{
    // The hole's center to the plate's left front top corner, in the top view; and its diameter.
    SheetDimension across;
    across.kind = SheetDimension::Kind::Horizontal;
    across.a = gp_Pnt(0, 0, 10);
    across.b = gp_Pnt(20, 20, 10);
    SheetDimension size;
    size.kind = SheetDimension::Kind::Diameter;
    size.a = size.b = gp_Pnt(20, 20, 10);
    size.radius = 5.0;

    const ViewGeometry before = projectView({plate(20.0)}, view(ViewKind::Top));
    gp_Pnt2d a, b;
    double r = 0.0;
    CHECK(resolveDimension(across, before, a, b, r));
    CHECK(near(dimensionValue(across, a, b, r), 20.0, 1e-9));
    CHECK(resolveDimension(size, before, a, b, r));
    CHECK(near(dimensionValue(size, a, b, r), 10.0, 1e-9));

    // The hole moves 3 mm: the dimension goes with it, and remembers where it is now.
    const ViewGeometry after = projectView({plate(23.0)}, view(ViewKind::Top));
    SheetDimension followed = across;
    CHECK(resolveDimension(across, after, a, b, r, &followed));
    CHECK(near(dimensionValue(across, a, b, r), 23.0, 1e-9));
    CHECK(near(followed.b.X(), 23.0, 1e-9));

    // Far from anything: lost.
    SheetDimension lost = across;
    lost.b = gp_Pnt(500, 500, 500);
    CHECK(!resolveDimension(lost, after, a, b, r));
}

} // namespace

int main()
{
    return runTests({
        {"view directions", testViewDirections},
        {"top view sees the hole", testTopViewSeesTheHole},
        {"front view hides the hole", testFrontViewHidesTheHole},
        {"section through the hole", testSectionThroughTheHole},
        {"hatching", testHatching},
        {"scales", testScales},
        {"default drawing", testDefaultDrawing},
        {"dimensions follow the model", testDimensionsFollowTheModel},
    });
}
