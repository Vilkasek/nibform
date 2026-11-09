// Sketch toolkit (M9): polygon, slot, ellipse, spline, sketch fillet / chamfer, mirror,
// patterns, horizontal / vertical dimensions, symmetric, projecting body edges.

#include "Check.h"

#include "model/Profiles.h"
#include "sketch/ConstraintCommands.h"
#include "sketch/Geometry.h"
#include "sketch/SketchOps.h"
#include "sketch/Solver.h"
#include "sketch/Trim.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>

namespace {

SketchConstraint relation(ConstraintKind kind, PointId p1, PointId p2, EntityId e1, EntityId e2 = kNoId)
{
    SketchConstraint c;
    c.kind = kind;
    c.p1 = p1;
    c.p2 = p2;
    c.e1 = e1;
    c.e2 = e2;
    return c;
}

int count(const Sketch& s, EntityKind kind)
{
    return static_cast<int>(std::count_if(s.entities().begin(), s.entities().end(),
                                          [kind](const SketchEntity& e) { return e.kind == kind; }));
}

int countConstraints(const Sketch& s, ConstraintKind kind)
{
    return static_cast<int>(std::count_if(s.constraints().begin(), s.constraints().end(),
                                          [kind](const SketchConstraint& c) { return c.kind == kind; }));
}

// An axis-aligned rectangle with horizontal / vertical sides; corners in order
// (0,0) (w,0) (w,h) (0,h).
struct Rect
{
    PointId p[4];
    EntityId side[4]; // bottom, right, top, left
};

Rect rectangle(Sketch& s, double w, double h)
{
    Rect r;
    const gp_Pnt2d at[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    for (int i = 0; i < 4; ++i)
        r.p[i] = s.addPoint(at[i]);
    for (int i = 0; i < 4; ++i) {
        r.side[i] = s.addLine(r.p[i], r.p[(i + 1) % 4]);
        s.addConstraint(relation(i % 2 == 0 ? ConstraintKind::Horizontal : ConstraintKind::Vertical, r.p[i],
                                 r.p[(i + 1) % 4], r.side[i]));
    }
    return r;
}

void testPolygon()
{
    Sketch s;
    const PointId c = s.addPoint({0, 0});
    const PointId first = s.addPoint({10, 0});
    const std::vector<EntityId> sides = addPolygon(s, c, first, 6);
    CHECK(sides.size() == 6);
    CHECK(count(s, EntityKind::Line) == 6);
    CHECK(count(s, EntityKind::Circle) == 1);
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Line)
            CHECK(near(s.pos(e.a).Distance(s.pos(e.b)), 10.0, 1e-9)); // a hexagon's side is its radius
        else
            CHECK(e.construction);
    }
    const SolveResult r = solve(s);
    CHECK(r.ok);
    CHECK(r.dof == 4); // position, size, rotation
    CHECK(findRegions(s).size() == 1);

    // Pulling a corner keeps it regular.
    CHECK(solve(s, {{first, gp_Pnt2d(14, 3)}}).ok);
    double side = -1.0;
    for (EntityId id : sides) {
        const SketchEntity& e = *s.entity(id);
        const double len = s.pos(e.a).Distance(s.pos(e.b));
        if (side < 0.0)
            side = len;
        CHECK(near(len, side, 1e-7));
    }
    CHECK(near(s.pos(first), gp_Pnt2d(14, 3), 1e-7));
    CHECK(addPolygon(s, c, c, 5).empty()); // no radius

    // Rounding a corner by the first side keeps the other sides equal among themselves.
    CHECK(filletCorner(s, first, 1.0));
    CHECK(countConstraints(s, ConstraintKind::Equal) == 3); // the four untouched sides
    CHECK(solve(s).ok);
}

void testSlot()
{
    Sketch s;
    const PointId c1 = s.addPoint({0, 0});
    const PointId c2 = s.addPoint({20, 0});
    const EntityId centerline = addSlot(s, c1, c2, 6.0);
    CHECK(centerline != kNoId);
    CHECK(s.entity(centerline)->construction);
    CHECK(count(s, EntityKind::Line) == 3);
    CHECK(count(s, EntityKind::Arc) == 2);
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Arc)
            CHECK(near(e.radius, 3.0, 1e-9));
    }
    const SolveResult r = solve(s);
    CHECK(r.ok);
    CHECK(r.dof == 5); // both centers and the width
    const auto regions = findRegions(s);
    CHECK(regions.size() == 1);

    // Moving one center drags the outline along, keeping it a slot: equal ends, both
    // sides tangent to them.
    CHECK(solve(s, {{c2, gp_Pnt2d(20, 10)}}).ok);
    std::vector<double> radii;
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Arc)
            radii.push_back(s.pos(e.c).Distance(s.pos(e.a)));
    }
    CHECK(radii.size() == 2 && near(radii[0], radii[1], 1e-6));
    for (const SketchEntity& e : s.entities()) {
        if (e.kind == EntityKind::Line && !e.construction) {
            CHECK(near(std::abs(signedDistanceToLine(s, e, s.pos(c1))), radii[0], 1e-6));
            CHECK(near(std::abs(signedDistanceToLine(s, e, s.pos(c2))), radii[0], 1e-6));
        }
    }
}

void testEllipse()
{
    Sketch s;
    const PointId c = s.addPoint({0, 0});
    const PointId a = s.addPoint({10, 0});
    const PointId b = s.addPoint({0, 4});
    const EntityId e = s.addEllipse(c, a, b);
    CHECK(s.hitTest({10.2, 0}, 0.5) == e);
    CHECK(s.hitTest({0, -4.1}, 0.5) == e);
    CHECK(!s.hitTest({5, 0}, 0.5));
    CHECK(findRegions(s).size() == 1);

    // Dragging an axis end turns the ellipse; the other axis stays square to it.
    CHECK(solve(s, {{a, gp_Pnt2d(6, 6)}}).ok);
    const gp_Vec2d u(s.pos(c), s.pos(a)), v(s.pos(c), s.pos(b));
    CHECK(std::abs(u.Dot(v)) < 1e-6);
    CHECK(analyze(s).dof == 5);

    // Fix pins all of it.
    SketchSelection pick;
    pick.entities = {e};
    CHECK(applyCommand(ConstraintCommand::Fix, s, pick, 1.0).applied);
    CHECK(analyze(s).dof == 0);
    // Relations it doesn't support are refused.
    CHECK(!applyCommand(ConstraintCommand::Equal, s, pick, 1.0).applied);
}

void testSpline()
{
    Sketch s;
    std::vector<PointId> pts{s.addPoint({0, 0}), s.addPoint({10, 5}), s.addPoint({20, 0}), s.addPoint({30, 5})};
    const EntityId spline = s.addSpline(pts, false);
    CHECK(s.entity(spline)->a == pts.front());
    CHECK(s.entity(spline)->b == pts.back());
    const std::vector<gp_Pnt2d> line = s.polyline(*s.entity(spline));
    CHECK(near(line.front(), gp_Pnt2d(0, 0), 1e-9));
    CHECK(near(line.back(), gp_Pnt2d(30, 5), 1e-9));
    for (PointId p : pts)
        CHECK(s.distanceTo(*s.entity(spline), s.pos(p)) < 1e-3); // passes through its points
    CHECK(s.entitiesAt(pts[1]) == std::vector<EntityId>{spline});

    // Dragging a through point reshapes it (no constraints hold it).
    CHECK(solve(s, {{pts[1], gp_Pnt2d(10, 9)}}).ok);
    CHECK(near(s.pos(pts[1]), gp_Pnt2d(10, 9), 1e-9));
    CHECK(s.distanceTo(*s.entity(spline), {10, 9}) < 0.05); // (to the drawn polyline)
    CHECK(analyze(s).dof == 8);

    // Through points can go while two are left; removing the curve drops them all.
    CHECK(s.removeThroughPoint(spline, pts[1]));
    CHECK(!s.point(pts[1]));
    CHECK(s.entity(spline)->through.size() == 3);
    CHECK(s.removeThroughPoint(spline, pts[3]));
    CHECK(s.entity(spline)->b == pts[2]);
    CHECK(!s.removeThroughPoint(spline, pts[0]));
    s.removeEntities({spline});
    CHECK(s.points().empty());

    // Closed: a region of its own.
    Sketch loop;
    std::vector<PointId> ring;
    for (int k = 0; k < 5; ++k)
        ring.push_back(loop.addPoint({10 * std::cos(2 * M_PI * k / 5), 10 * std::sin(2 * M_PI * k / 5)}));
    const EntityId closed = loop.addSpline(ring, true);
    CHECK(loop.entity(closed)->a == kNoId);
    CHECK(findRegions(loop).size() == 1);
    CHECK(!loop.mergePoints(ring[0], ring[1])); // neighbours can't become one
}

void testTrimAgainstSpline()
{
    // A line crossing a spline is cut there; the spline itself goes whole.
    Sketch s;
    const EntityId spline = s.addSpline({s.addPoint({0, 0}), s.addPoint({10, 4}), s.addPoint({20, 0})}, false);
    const EntityId line = s.addLine(s.addPoint({10, -5}), s.addPoint({10, 10}));
    const auto piece = findTrimPiece(s, {10, 8}, 0.5);
    CHECK(piece && piece->entity == line && !piece->whole);
    CHECK(trim(s, *piece));
    const SketchEntity& kept = *s.entity(line);
    CHECK(near(std::max(s.pos(kept.a).Y(), s.pos(kept.b).Y()), s.pos(s.entity(spline)->through[1]).Y(), 0.5));
    CHECK(solve(s).ok); // the new end isn't pinned to the spline (not modelled)

    const std::vector<gp_Pnt2d> along = s.polyline(*s.entity(spline));
    const auto whole = findTrimPiece(s, along[along.size() / 4], 0.5);
    CHECK(whole && whole->entity == spline && whole->whole);
    CHECK(trim(s, *whole));
    CHECK(!s.entity(spline));
}

void testFillet()
{
    Sketch s;
    const Rect r = rectangle(s, 20, 10);
    CHECK(solve(s).ok);
    const int dofBefore = analyze(s).dof;
    CHECK(cornerLines(s, r.p[2]).has_value());
    CHECK(near(maxFilletRadius(s, r.p[2]), 10.0 - 1e-3, 1e-6));
    CHECK(!filletCorner(s, r.p[2], 12.0)); // too big: no change
    CHECK(s.point(r.p[2]));

    EntityId arc = kNoId;
    CHECK(filletCorner(s, r.p[2], 3.0, &arc));
    CHECK(!s.point(r.p[2])); // the sharp corner is gone
    const SketchEntity& round = *s.entity(arc);
    CHECK(near(s.pos(round.c), gp_Pnt2d(17, 7), 1e-9));
    CHECK(countConstraints(s, ConstraintKind::Tangent) == 2);
    CHECK(countConstraints(s, ConstraintKind::Radius) == 1);
    CHECK(countConstraints(s, ConstraintKind::Horizontal) == 2); // carried over to the new ends
    CHECK(countConstraints(s, ConstraintKind::Vertical) == 2);
    const SolveResult after = solve(s);
    CHECK(after.ok);
    CHECK(after.dof == dofBefore); // the radius took what the new points added
    CHECK(near(s.pos(s.entity(r.side[1])->b), gp_Pnt2d(20, 7), 1e-9));
    CHECK(near(s.pos(s.entity(r.side[2])->a), gp_Pnt2d(17, 10), 1e-9));
    CHECK(findRegions(s).size() == 1);

    // Changing the radius keeps it tangent to both lines.
    for (const SketchConstraint& c : s.constraints()) {
        if (c.kind == ConstraintKind::Radius)
            s.constraint(c.id)->value = 5.0;
    }
    CHECK(solve(s).ok);
    const gp_Pnt2d center = s.pos(s.entity(arc)->c);
    CHECK(near(s.pos(s.entity(arc)->a).Distance(center), 5.0, 1e-6));
    for (EntityId line : {r.side[1], r.side[2]})
        CHECK(near(std::abs(signedDistanceToLine(s, *s.entity(line), center)), 5.0, 1e-6));

    // Lines in line with each other have no corner.
    Sketch straight;
    const PointId mid = straight.addPoint({5, 0});
    straight.addLine(straight.addPoint({0, 0}), mid);
    straight.addLine(mid, straight.addPoint({10, 0}));
    CHECK(!cornerLines(straight, mid));
}

void testFilletKeepsCornerAngle()
{
    // An angle dimensioned at the corner survives it being rounded.
    Sketch s;
    const PointId o = s.addPoint({0, 0});
    const EntityId l1 = s.addLine(o, s.addPoint({10, 0}));
    const EntityId l2 = s.addLine(o, s.addPoint({0, 10}));
    SketchConstraint angle = relation(ConstraintKind::Angle, o, kNoId, l1, l2);
    angle.value = measure(s, angle);
    s.addConstraint(angle);
    CHECK(filletCorner(s, o, 2.0));
    CHECK(countConstraints(s, ConstraintKind::Angle) == 1);
    CHECK(solve(s).ok);
    for (const SketchConstraint& c : s.constraints()) {
        if (c.kind == ConstraintKind::Angle)
            CHECK(near(std::abs(measure(s, c)), M_PI / 2.0, 1e-6));
    }
}

void testChamfer()
{
    Sketch s;
    const Rect r = rectangle(s, 20, 10);
    EntityId bevel = kNoId;
    CHECK(chamferCorner(s, r.p[0], 2.0, &bevel));
    const SketchEntity& b = *s.entity(bevel);
    CHECK(near(s.pos(b.a).Distance(s.pos(b.b)), 2.0 * std::sqrt(2.0), 1e-9));
    CHECK(count(s, EntityKind::Line) == 5);
    CHECK(solve(s).ok);
    CHECK(findRegions(s).size() == 1);
    CHECK(!chamferCorner(s, r.p[1], 25.0));
}

void testMirror()
{
    // Half of a shape left of the Y axis, mirrored onto the right.
    Sketch s;
    const PointId top = s.addPoint({0, 10}), bottom = s.addPoint({0, 0});
    const EntityId axis = s.addLine(bottom, top);
    s.setConstruction(axis, true);
    for (PointId p : {bottom, top}) {
        SketchConstraint pin = relation(ConstraintKind::Fixed, p, kNoId, kNoId);
        pin.target = s.pos(p);
        s.addConstraint(pin);
    }
    const PointId corner = s.addPoint({-8, 0}), upper = s.addPoint({-5, 10});
    const EntityId base = s.addLine(bottom, corner);
    const EntityId side = s.addLine(corner, upper);
    const EntityId lid = s.addLine(upper, top);
    const EntityId hole = s.addCircle(s.addPoint({-4, 5}), 1.5);

    const auto copies = mirrorEntities(s, {base, side, lid, hole, axis}, axis);
    CHECK(copies.size() == 4); // the axis isn't copied
    CHECK(s.findPoint({8, 0}, 1e-9).has_value());
    CHECK(s.findPoint({5, 10}, 1e-9).has_value());
    CHECK(s.findPoint({4, 5}, 1e-9).has_value());
    CHECK(countConstraints(s, ConstraintKind::Symmetric) == 3); // corner, upper, hole center
    CHECK(countConstraints(s, ConstraintKind::Equal) == 1);     // the hole's size
    CHECK(solve(s).ok);
    CHECK(findRegions(s).size() == 3); // the outline (with two holes), and each hole

    // The copy follows the original.
    CHECK(solve(s, {{corner, gp_Pnt2d(-12, 0)}}).ok);
    CHECK(s.findPoint({12, 0}, 1e-6).has_value());
    for (SketchConstraint c : s.constraints()) {
        if (c.kind == ConstraintKind::Equal) {
            s.setRadius(hole, 2.5);
            CHECK(solve(s).ok);
            CHECK(near(s.entity(c.e2)->radius, s.entity(c.e1)->radius, 1e-6));
        }
    }

    // Arcs stay counter-clockwise: the mirrored arc bulges the mirrored way.
    Sketch arcs;
    const EntityId line = arcs.addLine(arcs.addPoint({0, -10}), arcs.addPoint({0, 10}));
    const EntityId arc = arcs.addArc(arcs.addPoint({-5, 0}), arcs.addPoint({-3, 0}), arcs.addPoint({-5, 2}));
    const auto mirrored = mirrorEntities(arcs, {arc}, line);
    CHECK(mirrored.size() == 1);
    const SketchEntity& m = *arcs.entity(mirrored[0]);
    CHECK(near(arcs.pos(m.c), gp_Pnt2d(5, 0), 1e-9));
    CHECK(near(arcs.pos(m.a), gp_Pnt2d(5, 2), 1e-9));
    CHECK(near(arcs.pos(m.b), gp_Pnt2d(3, 0), 1e-9));
}

void testLinearPattern()
{
    Sketch s;
    const EntityId hole = s.addCircle(s.addPoint({0, 0}), 2.0);
    SketchConstraint size = relation(ConstraintKind::Diameter, kNoId, kNoId, hole);
    size.value = 4.0;
    s.addConstraint(size);
    const auto copies = linearPattern(s, {hole}, gp_Vec2d(10, 0), 4);
    CHECK(copies.size() == 3);
    for (int k = 1; k <= 3; ++k)
        CHECK(s.findPoint({10.0 * k, 0}, 1e-9).has_value());
    CHECK(countConstraints(s, ConstraintKind::Equal) == 3);
    CHECK(countConstraints(s, ConstraintKind::Diameter) == 1); // dimensions stay with the original

    // Changing the original's size changes them all.
    for (const SketchConstraint& c : s.constraints()) {
        if (c.kind == ConstraintKind::Diameter)
            s.constraint(c.id)->value = 6.0;
    }
    CHECK(solve(s).ok);
    for (EntityId id : copies)
        CHECK(near(s.entity(id)->radius, 3.0, 1e-6));

    // A rectangle's relations come along.
    Sketch boxes;
    const Rect r = rectangle(boxes, 4, 3);
    const auto rects = linearPattern(boxes, {r.side[0], r.side[1], r.side[2], r.side[3]}, gp_Vec2d(0, 5), 3);
    CHECK(rects.size() == 8);
    CHECK(countConstraints(boxes, ConstraintKind::Horizontal) == 6);
    CHECK(countConstraints(boxes, ConstraintKind::Vertical) == 6);
    CHECK(findRegions(boxes).size() == 3);
}

void testCircularPattern()
{
    Sketch s;
    const PointId p = s.addPoint({10, 0});
    const EntityId hole = s.addCircle(p, 1.0);
    const PointId a = s.addPoint({8, 2}), b = s.addPoint({12, 2});
    const EntityId bar = s.addLine(a, b);
    s.addConstraint(relation(ConstraintKind::Horizontal, a, b, bar));
    const auto copies = circularPattern(s, {hole, bar}, gp_Pnt2d(0, 0), 6);
    CHECK(copies.size() == 10);
    for (int k = 0; k < 6; ++k) {
        const double t = 2.0 * M_PI * k / 6;
        CHECK(s.findPoint({10 * std::cos(t), 10 * std::sin(t)}, 1e-9).has_value());
    }
    // Turned copies of a horizontal line aren't horizontal: the relation stays behind.
    CHECK(countConstraints(s, ConstraintKind::Horizontal) == 1);
    CHECK(solve(s).ok);
}

void testHorizontalVerticalDimensions()
{
    Sketch s;
    const PointId a = s.addPoint({0, 0});
    const PointId b = s.addPoint({7, 3});
    const EntityId slanted = s.addLine(a, b);
    SketchSelection line;
    line.entities = {slanted};
    const CommandResult h = applyCommand(ConstraintCommand::HorizontalDimension, s, line, 2.0);
    CHECK(h.applied && h.dimension != kNoId);
    CHECK(near(s.constraint(h.dimension)->value, 7.0));
    CHECK(isDimension(s.constraint(h.dimension)->kind));

    SketchSelection points;
    points.points = {a, b};
    const CommandResult v = applyCommand(ConstraintCommand::VerticalDimension, s, points, 2.0);
    CHECK(v.applied);
    CHECK(near(std::abs(s.constraint(v.dimension)->value), 3.0));

    s.constraint(h.dimension)->value = 12.0;
    s.constraint(v.dimension)->value = measure(s, *s.constraint(v.dimension)) > 0 ? 5.0 : -5.0;
    CHECK(solve(s).ok);
    CHECK(near(std::abs(s.pos(b).X() - s.pos(a).X()), 12.0, 1e-7));
    CHECK(near(std::abs(s.pos(b).Y() - s.pos(a).Y()), 5.0, 1e-7));

    // Trimming a line drops what measured it.
    Sketch t;
    const EntityId bar = t.addLine(t.addPoint({0, 0}), t.addPoint({10, 0}));
    t.addLine(t.addPoint({4, -5}), t.addPoint({4, 5}));
    SketchSelection pick;
    pick.entities = {bar};
    CHECK(applyCommand(ConstraintCommand::HorizontalDimension, t, pick, 2.0).applied);
    const auto piece = findTrimPiece(t, {8, 0}, 0.5);
    CHECK(piece && piece->entity == bar);
    CHECK(trim(t, *piece));
    CHECK(countConstraints(t, ConstraintKind::HorizontalDistance) == 0);
}

void testSymmetricCommand()
{
    Sketch s;
    const EntityId axis = s.addLine(s.addPoint({0, -10}), s.addPoint({0, 10}));
    SketchSelection axisOnly;
    axisOnly.entities = {axis};
    CHECK(applyCommand(ConstraintCommand::Fix, s, axisOnly, 1.0).applied); // keep it where it is
    const PointId l = s.addPoint({-3, 1});
    const PointId r = s.addPoint({5, 2});
    s.addLine(l, r);
    SketchSelection pick;
    pick.entities = {axis};
    pick.points = {l, r};
    CHECK(applyCommand(ConstraintCommand::Symmetric, s, pick, 1.0).applied);
    CHECK(solve(s).ok);
    CHECK(near(s.pos(l).X(), -s.pos(r).X(), 1e-7));
    CHECK(near(s.pos(l).Y(), s.pos(r).Y(), 1e-7));

    // The axis' own points can't be mirrored about it.
    SketchSelection own;
    own.entities = {axis};
    own.points = {l, s.entity(axis)->a};
    CHECK(!applyCommand(ConstraintCommand::Symmetric, s, own, 1.0).applied);

    // Merging the two points makes it meaningless: it goes.
    Sketch merged = s;
    const PointId p = merged.addPoint({-1, 7}), q = merged.addPoint({1, 7});
    merged.addLine(p, merged.addPoint({-1, 9}));
    merged.addLine(q, merged.addPoint({1, 9}));
    SketchConstraint sym = relation(ConstraintKind::Symmetric, p, q, axis);
    merged.addConstraint(sym);
    CHECK(countConstraints(merged, ConstraintKind::Symmetric) == 2);
    CHECK(merged.mergePoints(p, q));
    CHECK(countConstraints(merged, ConstraintKind::Symmetric) == 1);
}

void testProjectEdges()
{
    // A cylinder standing on the ground, radius 5, from z 0 to 10.
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 5.0, 10.0).Shape();
    TopoDS_Face top;
    for (TopExp_Explorer f(cylinder, TopAbs_FACE); f.More(); f.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(f.Current()));
        if (surface.GetType() == GeomAbs_Plane && surface.Plane().Location().Z() > 5.0)
            top = TopoDS::Face(f.Current());
    }
    CHECK(!top.IsNull());

    // Square on: the circle stays a circle, pinned.
    Sketch ground(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ(), gp::DX()));
    const auto added = projectEdges(ground, top, false);
    CHECK(added.size() == 1);
    CHECK(count(ground, EntityKind::Circle) == 1);
    CHECK(!ground.entities().front().construction);
    CHECK(analyze(ground).dof == 0);
    CHECK(findRegions(ground).size() == 1);
    CHECK(projectEdges(ground, top, false).empty()); // already there

    // At a slant: an ellipse, its minor axis shortened by the cosine.
    const gp_Dir tilted(0.0, -std::sin(M_PI / 3), std::cos(M_PI / 3)); // 60° from the ground
    Sketch slanted(gp_Ax3(gp_Pnt(0, 0, 0), tilted, gp::DX()));
    projectEdges(slanted, top, true);
    CHECK(count(slanted, EntityKind::Ellipse) == 1);
    for (const SketchEntity& e : slanted.entities()) {
        if (e.kind != EntityKind::Ellipse)
            continue;
        const EllipseAxes axes = ellipseAxes(slanted, e);
        const double big = std::max(axes.u.Magnitude(), axes.v.Magnitude());
        const double small = std::min(axes.u.Magnitude(), axes.v.Magnitude());
        CHECK(near(big, 5.0, 1e-6));
        CHECK(near(small, 5.0 * std::cos(M_PI / 3), 1e-6));
        CHECK(e.construction);
    }

    // Edge-on (a front sketch): the whole cylinder is a rectangle of lines.
    Sketch front(gp_Ax3(gp_Pnt(0, 0, 0), -gp::DY(), gp::DX()));
    projectEdges(front, cylinder, false);
    CHECK(count(front, EntityKind::Line) >= 2);
    CHECK(count(front, EntityKind::Circle) == 0);
    for (const SketchEntity& e : front.entities()) {
        for (PointId p : e.pointIds()) {
            const gp_Pnt2d q = front.pos(p);
            CHECK(std::abs(q.X()) <= 5.0 + 1e-6 && q.Y() >= -1e-6 && q.Y() <= 10.0 + 1e-6);
        }
    }

    // A box seen from the top projects to its outline, once.
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(1, 1, 2), 4, 3, 5).Shape();
    Sketch above(gp_Ax3(gp_Pnt(0, 0, 0), gp::DZ(), gp::DX()));
    projectEdges(above, box, false);
    CHECK(count(above, EntityKind::Line) == 4); // the vertical edges are seen end-on
    CHECK(findRegions(above).size() == 1);
}

} // namespace

int main()
{
    return runTests({
        {"polygon", testPolygon},
        {"slot", testSlot},
        {"ellipse", testEllipse},
        {"spline", testSpline},
        {"trim against a spline", testTrimAgainstSpline},
        {"fillet", testFillet},
        {"fillet keeps the corner angle", testFilletKeepsCornerAngle},
        {"chamfer", testChamfer},
        {"mirror", testMirror},
        {"linear pattern", testLinearPattern},
        {"circular pattern", testCircularPattern},
        {"horizontal / vertical dimensions", testHorizontalVerticalDimensions},
        {"symmetric command", testSymmetricCommand},
        {"project edges", testProjectEdges},
    });
}
