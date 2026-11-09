// Freehand strokes (M8): recognizing rough lines, polylines, rectangles, circles, arcs
// and scribbles, and the clean constrained geometry they turn into.

#include "Check.h"

#include "sketch/Freehand.h"
#include "sketch/Solver.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTol = 1.0; // ~10 px at this zoom

// A hand's wobble: deterministic, about `amount` across.
struct Wobble
{
    unsigned state = 12345;
    double amount = 0.4;
    gp_Pnt2d operator()(const gp_Pnt2d& p)
    {
        auto next = [this] {
            state = state * 1103515245u + 12345u;
            return ((state >> 16) & 0x7fff) / 32767.0 - 0.5;
        };
        const double dx = next() * amount, dy = next() * amount;
        return gp_Pnt2d(p.X() + dx, p.Y() + dy);
    }
};

// Points along the polyline through `corners`, `perSide` per side, wobbling.
std::vector<gp_Pnt2d> strokeThrough(const std::vector<gp_Pnt2d>& corners, int perSide, Wobble wobble = {})
{
    std::vector<gp_Pnt2d> out;
    for (std::size_t i = 0; i + 1 < corners.size(); ++i) {
        for (int k = 0; k < perSide; ++k) {
            const double t = static_cast<double>(k) / perSide;
            out.push_back(wobble(gp_Pnt2d(corners[i].X() + (corners[i + 1].X() - corners[i].X()) * t,
                                          corners[i].Y() + (corners[i + 1].Y() - corners[i].Y()) * t)));
        }
    }
    out.push_back(wobble(corners.back()));
    return out;
}

// Around a circle from `from` for `sweep` radians (negative: clockwise), slightly egg-shaped.
std::vector<gp_Pnt2d> strokeAround(const gp_Pnt2d& c, double r, double from, double sweep, Wobble wobble = {})
{
    std::vector<gp_Pnt2d> out;
    const int n = 80;
    for (int k = 0; k <= n; ++k) {
        const double a = from + sweep * k / n;
        const double rr = r * (1.0 + 0.03 * std::sin(2.0 * a));
        out.push_back(wobble(gp_Pnt2d(c.X() + rr * std::cos(a), c.Y() + rr * std::sin(a))));
    }
    return out;
}

int count(const Sketch& s, EntityKind kind)
{
    return static_cast<int>(std::count_if(s.entities().begin(), s.entities().end(),
                                          [kind](const SketchEntity& e) { return e.kind == kind; }));
}

int count(const Sketch& s, ConstraintKind kind)
{
    return static_cast<int>(std::count_if(s.constraints().begin(), s.constraints().end(),
                                          [kind](const SketchConstraint& c) { return c.kind == kind; }));
}

void testLine()
{
    const StrokeShape shape = recognizeStroke(strokeThrough({{0, 0}, {40, 25}}, 40), kTol);
    CHECK(shape.kind == StrokeShape::Kind::Line);
    CHECK(shape.points.size() == 2);
    CHECK(near(shape.points[0], gp_Pnt2d(0, 0), 0.5));
    CHECK(near(shape.points[1], gp_Pnt2d(40, 25), 0.5));

    // A tap is nothing.
    CHECK(recognizeStroke({{0, 0}, {0.1, 0.1}}, kTol).kind == StrokeShape::Kind::None);
}

void testNearlyLevelLineBecomesHorizontal()
{
    Sketch s;
    const StrokeShape shape = recognizeStroke(strokeThrough({{0, 0}, {50, 2.5}}, 50), kTol);
    CHECK(shape.kind == StrokeShape::Kind::Line);
    const auto made = addStrokeShape(s, shape, kTol);
    CHECK(made.size() == 1);
    const SketchEntity* line = s.entity(made[0]);
    CHECK(near(s.pos(line->a).Y(), s.pos(line->b).Y()));
    CHECK(count(s, ConstraintKind::Horizontal) == 1);
}

void testLineJoinsExistingPoints()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({30, 0});
    s.addLine(a, b);
    // Starts a little off the end of the existing line and goes up.
    const StrokeShape shape = recognizeStroke(strokeThrough({{30.6, 0.4}, {30.8, 20}}, 30), kTol);
    CHECK(shape.kind == StrokeShape::Kind::Line);
    const auto made = addStrokeShape(s, shape, kTol);
    CHECK(made.size() == 1);
    const SketchEntity* line = s.entity(made[0]);
    CHECK(line->a == b);
    CHECK(near(s.pos(line->b).X(), 30.0)); // straightened from the existing point
    CHECK(count(s, ConstraintKind::Vertical) == 1);
}

void testAxisAlignedRectangle()
{
    Wobble w;
    w.amount = 0.8;
    const auto stroke = strokeThrough({{0, 0}, {40, 1}, {41, 21}, {1, 20}, {0, 0.5}}, 30, w);
    const StrokeShape shape = recognizeStroke(stroke, kTol);
    CHECK(shape.kind == StrokeShape::Kind::Rectangle);
    CHECK(shape.points.size() == 4);

    Sketch s;
    const auto made = addStrokeShape(s, shape, kTol);
    CHECK(made.size() == 4);
    CHECK(count(s, ConstraintKind::Horizontal) == 2);
    CHECK(count(s, ConstraintKind::Vertical) == 2);
    CHECK(s.points().size() == 4); // a closed loop
    const SolveResult r = solve(s);
    CHECK(r.ok);
    CHECK(r.dof == 4);
}

void testTurnedRectangleKeepsItsRightAngles()
{
    const double a = 30.0 * kPi / 180.0;
    auto at = [&](double u, double v) {
        return gp_Pnt2d(u * std::cos(a) - v * std::sin(a), u * std::sin(a) + v * std::cos(a));
    };
    const auto stroke = strokeThrough({at(0, 0), at(40, 0), at(40, 20), at(0, 20), at(0, 0)}, 30);
    const StrokeShape shape = recognizeStroke(stroke, kTol);
    CHECK(shape.kind == StrokeShape::Kind::Rectangle);

    Sketch s;
    addStrokeShape(s, shape, kTol);
    CHECK(count(s, ConstraintKind::Perpendicular) == 3);
    CHECK(count(s, ConstraintKind::Horizontal) == 0);
    const SolveResult r = solve(s);
    CHECK(r.ok);
    CHECK(r.dof == 5); // where, how big both ways, and turned how far
}

void testTriangleAndOpenPolyline()
{
    const StrokeShape triangle = recognizeStroke(strokeThrough({{0, 0}, {40, 0}, {20, 30}, {0, 0}}, 30), kTol);
    CHECK(triangle.kind == StrokeShape::Kind::Polyline);
    CHECK(triangle.closed);
    CHECK(triangle.points.size() == 3);

    // An L: two sides, three corners, not closed.
    const StrokeShape ell = recognizeStroke(strokeThrough({{0, 30}, {0, 0}, {25, 0}}, 30), kTol);
    CHECK(ell.kind == StrokeShape::Kind::Polyline);
    CHECK(!ell.closed);
    CHECK(ell.points.size() == 3);
    Sketch s;
    CHECK(addStrokeShape(s, ell, kTol).size() == 2);
    CHECK(count(s, ConstraintKind::Vertical) == 1);
    CHECK(count(s, ConstraintKind::Horizontal) == 1);
}

void testCircle()
{
    // Drawn clockwise, overshooting past where it started.
    const StrokeShape shape = recognizeStroke(strokeAround({10, 5}, 15, 0.3, -2.0 * kPi - 0.4), kTol);
    CHECK(shape.kind == StrokeShape::Kind::Circle);
    CHECK(near(shape.center, gp_Pnt2d(10, 5), 0.6));
    CHECK(near(shape.radius, 15.0, 0.6));

    Sketch s;
    const PointId c = s.addPoint({10.3, 5.2}); // the center joins a point already there
    const auto made = addStrokeShape(s, shape, kTol);
    CHECK(made.size() == 1);
    CHECK(count(s, EntityKind::Circle) == 1);
    CHECK(s.entity(made[0])->c == c);
}

void testArc()
{
    // A quarter turn counter-clockwise, and one clockwise.
    const StrokeShape ccw = recognizeStroke(strokeAround({0, 0}, 20, 0.0, kPi / 2.0), kTol);
    CHECK(ccw.kind == StrokeShape::Kind::Arc);
    CHECK(near(ccw.points[0], gp_Pnt2d(20, 0), 1.0));
    CHECK(near(ccw.points[1], gp_Pnt2d(0, 20), 1.0));

    const StrokeShape cw = recognizeStroke(strokeAround({0, 0}, 20, kPi / 2.0, -kPi / 2.0), kTol);
    CHECK(cw.kind == StrokeShape::Kind::Arc);
    CHECK(near(cw.points[0], gp_Pnt2d(20, 0), 1.0)); // still counter-clockwise start to end

    // Between the ends of two existing lines: both ends join, the radius follows.
    Sketch s;
    const PointId a = s.addPoint({20, 0});
    const PointId b = s.addPoint({0, 20});
    const auto made = addStrokeShape(s, ccw, kTol);
    CHECK(made.size() == 1);
    const SketchEntity* arc = s.entity(made[0]);
    CHECK(arc->kind == EntityKind::Arc);
    CHECK(arc->a == a);
    CHECK(arc->b == b);
    CHECK(near(s.pos(arc->c).Distance(s.pos(a)), s.pos(arc->c).Distance(s.pos(b)), 1e-9));
}

void testScribble()
{
    std::vector<gp_Pnt2d> zigzag;
    for (int i = 0; i < 6; ++i)
        zigzag.push_back(gp_Pnt2d(i % 2 ? 20.0 : 0.0, i * 1.5));
    const StrokeShape shape = recognizeStroke(strokeThrough(zigzag, 20), kTol);
    CHECK(shape.kind == StrokeShape::Kind::Scribble);

    Sketch s;
    const EntityId under = s.addLine(s.addPoint({10, -5}), s.addPoint({10, 15}));
    const EntityId away = s.addLine(s.addPoint({50, -5}), s.addPoint({50, 15}));
    const EntityId circle = s.addCircle(s.addPoint({23.5, 4}), 4.0); // touched at its left
    const auto crossed = entitiesCrossed(s, shape.points, kTol);
    CHECK(crossed.count(under) == 1);
    CHECK(crossed.count(circle) == 1);
    CHECK(crossed.count(away) == 0);
    CHECK(addStrokeShape(s, shape, kTol).empty()); // a scribble adds nothing
}

void testRoundSquareIsNotACircle()
{
    // A square with slightly rounded corners stays a rectangle; a wobbly circle stays a circle.
    const StrokeShape square = recognizeStroke(strokeThrough({{0, 0}, {30, 0}, {30, 30}, {0, 30}, {0, 0}}, 25), kTol);
    CHECK(square.kind == StrokeShape::Kind::Rectangle);
    Wobble w;
    w.amount = 1.0;
    const StrokeShape circle = recognizeStroke(strokeAround({0, 0}, 25, 1.0, 2.0 * kPi, w), kTol);
    CHECK(circle.kind == StrokeShape::Kind::Circle);
}

void testUnclearStrokeIsNothing()
{
    // A spiral is neither round nor straight.
    std::vector<gp_Pnt2d> spiral;
    for (int k = 0; k <= 200; ++k) {
        const double a = k * 4.0 * kPi / 200;
        const double r = 5.0 + 3.0 * a;
        spiral.push_back(gp_Pnt2d(r * std::cos(a), r * std::sin(a)));
    }
    CHECK(recognizeStroke(spiral, kTol).kind == StrokeShape::Kind::None);
}

} // namespace

int main()
{
    return runTests({
        {"line", testLine},
        {"nearly level line becomes horizontal", testNearlyLevelLineBecomesHorizontal},
        {"line joins existing points", testLineJoinsExistingPoints},
        {"axis-aligned rectangle", testAxisAlignedRectangle},
        {"turned rectangle keeps its right angles", testTurnedRectangleKeepsItsRightAngles},
        {"triangle and open polyline", testTriangleAndOpenPolyline},
        {"circle", testCircle},
        {"arc", testArc},
        {"scribble", testScribble},
        {"round square is not a circle", testRoundSquareIsNotACircle},
        {"unclear stroke is nothing", testUnclearStrokeIsNothing},
    });
}
