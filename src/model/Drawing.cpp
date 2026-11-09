#include "model/Drawing.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.14159265358979323846;

Bnd_Box boxOf(const std::vector<TopoDS_Shape>& bodies)
{
    Bnd_Box box;
    for (const TopoDS_Shape& b : bodies)
        BRepBndLib::Add(b, box);
    return box;
}

// Points along an edge (as it lies, in 3D).
Polyline2d sampleEdge(const TopoDS_Edge& edge, const gp_Ax2* into)
{
    Polyline2d out;
    if (BRep_Tool::Degenerated(edge))
        return out;
    BRepAdaptor_Curve curve(edge);
    auto add = [&](const gp_Pnt& p) {
        if (into)
            out.emplace_back(gp_Vec(into->Location(), p).Dot(gp_Vec(into->XDirection())),
                             gp_Vec(into->Location(), p).Dot(gp_Vec(into->YDirection())));
        else
            out.emplace_back(p.X(), p.Y());
    };
    if (curve.GetType() == GeomAbs_Line) {
        add(curve.Value(curve.FirstParameter()));
        add(curve.Value(curve.LastParameter()));
    } else {
        GCPnts_QuasiUniformDeflection points(curve, 0.01);
        if (points.IsDone() && points.NbPoints() >= 2) {
            for (int i = 1; i <= points.NbPoints(); ++i)
                add(points.Value(i));
        } else {
            add(curve.Value(curve.FirstParameter()));
            add(curve.Value(curve.LastParameter()));
        }
    }
    if (edge.Orientation() == TopAbs_REVERSED)
        std::reverse(out.begin(), out.end());
    return out;
}

void addEdges(std::vector<Polyline2d>& out, const TopoDS_Shape& compound)
{
    if (compound.IsNull())
        return;
    BRepLib::BuildCurves3d(compound);
    for (TopExp_Explorer it(compound, TopAbs_EDGE); it.More(); it.Next()) {
        Polyline2d line = sampleEdge(TopoDS::Edge(it.Current()), nullptr);
        if (line.size() >= 2)
            out.push_back(std::move(line));
    }
}

double segmentDistance(const gp_Pnt2d& p, const gp_Pnt2d& a, const gp_Pnt2d& b)
{
    const gp_Vec2d ab(a, b);
    const double len2 = ab.SquareMagnitude();
    if (len2 < 1e-24)
        return p.Distance(a);
    const double t = std::clamp(gp_Vec2d(a, p).Dot(ab) / len2, 0.0, 1.0);
    return p.Distance(gp_Pnt2d(a.X() + ab.X() * t, a.Y() + ab.Y() * t));
}

// Hidden edges that lie right under visible ones (a box's back edges seen through its
// front ones) aren't drawn: only where nothing visible covers them.
void dropCovered(std::vector<Polyline2d>& hidden, const std::vector<Polyline2d>& visible)
{
    constexpr double tolerance = 1e-4;
    auto covered = [&](const gp_Pnt2d& p) {
        for (const Polyline2d& line : visible) {
            for (std::size_t i = 1; i < line.size(); ++i) {
                if (segmentDistance(p, line[i - 1], line[i]) < tolerance)
                    return true;
            }
        }
        return false;
    };
    std::erase_if(hidden, [&](const Polyline2d& line) {
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (!covered(line[i]))
                return false;
            if (i + 1 < line.size() && !covered(gp_Pnt2d((line[i].XY() + line[i + 1].XY()) / 2.0)))
                return false;
        }
        return true;
    });
}

void grow(gp_Pnt2d& min, gp_Pnt2d& max, const gp_Pnt2d& p)
{
    min.SetCoord(std::min(min.X(), p.X()), std::min(min.Y(), p.Y()));
    max.SetCoord(std::max(max.X(), p.X()), std::max(max.Y(), p.Y()));
}

} // namespace

gp_Ax2 viewAxes(ViewKind kind)
{
    const gp_Pnt o = gp::Origin();
    switch (kind) {
    case ViewKind::Front:
        return gp_Ax2(o, -gp::DY(), gp::DX());
    case ViewKind::Back:
        return gp_Ax2(o, gp::DY(), -gp::DX());
    case ViewKind::Top:
        return gp_Ax2(o, gp::DZ(), gp::DX());
    case ViewKind::Bottom:
        return gp_Ax2(o, -gp::DZ(), gp::DX());
    case ViewKind::Right:
        return gp_Ax2(o, gp::DX(), gp::DY());
    case ViewKind::Left:
        return gp_Ax2(o, -gp::DX(), -gp::DY());
    case ViewKind::Iso:
        return gp_Ax2(o, gp_Dir(1, -1, 1), gp_Dir(1, 1, 0));
    case ViewKind::Section:
        break;
    }
    return gp_Ax2(o, -gp::DY(), gp::DX());
}

gp_Pnt2d projectPoint(ViewKind kind, const gp_Pnt& p)
{
    const gp_Ax2 axes = viewAxes(kind);
    const gp_Vec v(axes.Location(), p);
    return gp_Pnt2d(v.Dot(gp_Vec(axes.XDirection())), v.Dot(gp_Vec(axes.YDirection())));
}

ViewGeometry projectView(const std::vector<TopoDS_Shape>& bodies, const SheetView& view)
{
    ViewGeometry g;
    const ViewKind kind = view.kind == ViewKind::Section ? view.base : view.kind;
    const gp_Ax2 axes = viewAxes(kind);
    std::vector<TopoDS_Shape> shapes = bodies;

    // A section: everything between the cutting plane and the viewer is taken away.
    gp_Pln cutPlane;
    if (view.kind == ViewKind::Section) {
        const Bnd_Box box = boxOf(bodies);
        if (box.IsVoid())
            return g;
        const gp_Pnt middle((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0);
        const gp_Dir n = axes.Direction();
        const gp_Pnt at = middle.Translated(gp_Vec(n) * view.offset);
        cutPlane = gp_Pln(at, n);
        const double reach = std::sqrt(box.SquareExtent()) + 1.0;
        const TopoDS_Face plane = BRepBuilderAPI_MakeFace(cutPlane, -reach * 2, reach * 2, -reach * 2, reach * 2).Face();
        const TopoDS_Shape away = BRepPrimAPI_MakeHalfSpace(plane, at.Translated(gp_Vec(n) * reach)).Solid();
        shapes.clear();
        for (const TopoDS_Shape& b : bodies) {
            BRepAlgoAPI_Cut cut(b, away);
            if (cut.IsDone() && !cut.Shape().IsNull())
                shapes.push_back(cut.Shape());
        }
    }
    if (shapes.empty())
        return g;

    Handle(HLRBRep_Algo) hlr = new HLRBRep_Algo();
    for (const TopoDS_Shape& s : shapes)
        hlr->Add(s);
    hlr->Projector(HLRAlgo_Projector(axes));
    hlr->Update();
    hlr->Hide();
    HLRBRep_HLRToShape lines(hlr);
    addEdges(g.visible, lines.VCompound());
    addEdges(g.visible, lines.OutLineVCompound());
    if (view.hidden && kind != ViewKind::Iso) {
        addEdges(g.hidden, lines.HCompound());
        addEdges(g.hidden, lines.OutLineHCompound());
        dropCovered(g.hidden, g.visible);
    }

    // Section faces: on the cutting plane, facing the viewer; outlined and hatched.
    if (view.kind == ViewKind::Section) {
        std::vector<Polyline2d> loops;
        for (const TopoDS_Shape& s : shapes) {
            for (TopExp_Explorer f(s, TopAbs_FACE); f.More(); f.Next()) {
                const TopoDS_Face face = TopoDS::Face(f.Current());
                GProp_GProps props;
                BRepGProp::SurfaceProperties(face, props);
                if (cutPlane.Distance(props.CentreOfMass()) > 1e-6)
                    continue;
                for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
                    Polyline2d loop;
                    for (BRepTools_WireExplorer e(TopoDS::Wire(w.Current()), face); e.More(); e.Next()) {
                        const Polyline2d piece = sampleEdge(e.Current(), &axes);
                        for (const gp_Pnt2d& p : piece) {
                            if (loop.empty() || loop.back().Distance(p) > 1e-9)
                                loop.push_back(p);
                        }
                    }
                    if (loop.size() >= 3)
                        loops.push_back(loop);
                }
            }
        }
        for (Polyline2d loop : loops) {
            loop.push_back(loop.front());
            g.cut.push_back(loop);
        }
        g.hatch = hatchLoops(loops, 2.5 / std::max(view.scale, 1e-6));
    }

    // What dimensions attach to: corners and round edges' centers.
    for (const TopoDS_Shape& s : shapes) {
        TopTools_IndexedMapOfShape vertices, edges;
        TopExp::MapShapes(s, TopAbs_VERTEX, vertices);
        TopExp::MapShapes(s, TopAbs_EDGE, edges);
        for (int i = 1; i <= vertices.Extent(); ++i) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
            g.corners.push_back({p, projectPoint(kind, p)});
        }
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(edges(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            const BRepAdaptor_Curve curve(edge);
            if (curve.GetType() != GeomAbs_Circle)
                continue;
            const gp_Circ circle = curve.Circle();
            ViewGeometry::Circle c;
            c.center = circle.Location();
            c.at = projectPoint(kind, c.center);
            c.radius = circle.Radius();
            c.full = std::abs(curve.LastParameter() - curve.FirstParameter() - 2.0 * kPi) < 1e-6;
            c.round = std::abs(circle.Axis().Direction().Dot(axes.Direction())) > 0.999;
            const bool known = std::any_of(g.circles.begin(), g.circles.end(), [&](const ViewGeometry::Circle& o) {
                return o.center.Distance(c.center) < 1e-6 && std::abs(o.radius - c.radius) < 1e-6;
            });
            if (!known)
                g.circles.push_back(c);
        }
    }

    g.min = gp_Pnt2d(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    g.max = gp_Pnt2d(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
    for (const auto* set : {&g.visible, &g.hidden, &g.cut}) {
        for (const Polyline2d& line : *set) {
            for (const gp_Pnt2d& p : line)
                grow(g.min, g.max, p);
        }
    }
    if (g.empty()) {
        g.min = g.max = gp_Pnt2d(0, 0);
    }
    return g;
}

std::vector<std::pair<gp_Pnt2d, gp_Pnt2d>> hatchLoops(const std::vector<Polyline2d>& loops, double spacing,
                                                       double angleDeg)
{
    std::vector<std::pair<gp_Pnt2d, gp_Pnt2d>> out;
    if (loops.empty() || spacing <= 0.0)
        return out;
    // Turned so the hatch runs along X, scanned row by row, turned back.
    const double a = angleDeg * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    auto turn = [&](const gp_Pnt2d& p) { return gp_Pnt2d(c * p.X() + s * p.Y(), -s * p.X() + c * p.Y()); };
    auto back = [&](double x, double y) { return gp_Pnt2d(c * x - s * y, s * x + c * y); };
    std::vector<Polyline2d> turned;
    double y0 = std::numeric_limits<double>::max(), y1 = -y0;
    for (const Polyline2d& loop : loops) {
        Polyline2d t;
        for (const gp_Pnt2d& p : loop) {
            t.push_back(turn(p));
            y0 = std::min(y0, t.back().Y());
            y1 = std::max(y1, t.back().Y());
        }
        turned.push_back(t);
    }
    if ((y1 - y0) / spacing > 5000)
        return out; // far too dense to be of use
    for (double y = std::floor(y0 / spacing) * spacing + spacing / 2.0; y < y1; y += spacing) {
        std::vector<double> xs;
        for (const Polyline2d& loop : turned) {
            const std::size_t n = loop.size();
            for (std::size_t i = 0; i < n; ++i) {
                const gp_Pnt2d& p = loop[i];
                const gp_Pnt2d& q = loop[(i + 1) % n];
                if ((p.Y() <= y) == (q.Y() <= y))
                    continue; // doesn't cross this row (half-open: each crossing counted once)
                xs.push_back(p.X() + (y - p.Y()) * (q.X() - p.X()) / (q.Y() - p.Y()));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2)
            out.emplace_back(back(xs[i], y), back(xs[i + 1], y));
    }
    return out;
}

gp_Pnt2d toSheet(const SheetView& view, const ViewGeometry& geometry, const gp_Pnt2d& p)
{
    const gp_Pnt2d m = geometry.middle();
    return gp_Pnt2d(view.at.X() + (p.X() - m.X()) * view.scale, view.at.Y() + (p.Y() - m.Y()) * view.scale);
}

gp_Pnt2d fromSheet(const SheetView& view, const ViewGeometry& geometry, const gp_Pnt2d& p)
{
    const gp_Pnt2d m = geometry.middle();
    return gp_Pnt2d(m.X() + (p.X() - view.at.X()) / view.scale, m.Y() + (p.Y() - view.at.Y()) / view.scale);
}

double fittingScale(double width, double height, double roomWidth, double roomHeight)
{
    static const double kScales[] = {10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01};
    for (double s : kScales) {
        if (width * s <= roomWidth && height * s <= roomHeight)
            return s;
    }
    return 0.01;
}

std::string scaleText(double scale)
{
    if (scale >= 1.0)
        return std::to_string(static_cast<int>(std::lround(scale))) + ":1";
    return "1:" + std::to_string(static_cast<int>(std::lround(1.0 / scale)));
}

Drawing defaultDrawing(const std::vector<TopoDS_Shape>& bodies, const std::string& title)
{
    Drawing d;
    d.title = title;
    const Bnd_Box box = boxOf(bodies);
    if (box.IsVoid())
        return d;
    // Each view's extent, from the corners of the bodies' box.
    auto extent = [&](ViewKind kind) {
        gp_Pnt2d min(1e100, 1e100), max(-1e100, -1e100);
        const gp_Pnt lo = box.CornerMin(), hi = box.CornerMax();
        for (int i = 0; i < 8; ++i) {
            const gp_Pnt p(i & 1 ? hi.X() : lo.X(), i & 2 ? hi.Y() : lo.Y(), i & 4 ? hi.Z() : lo.Z());
            grow(min, max, projectPoint(kind, p));
        }
        return std::pair(max.X() - min.X(), max.Y() - min.Y());
    };
    const auto front = extent(ViewKind::Front), top = extent(ViewKind::Top), left = extent(ViewKind::Left),
               iso = extent(ViewKind::Iso);
    // First-angle: the top view under the front one, the left view to its right; the
    // isometric one after it. Gaps leave room for dimensions; the title block sits low right.
    constexpr double gap = 18.0, leftMargin = 20.0, margin = 10.0;
    auto layout = [&](double w, double h) {
        const double s = fittingScale(front.first + left.first + iso.first, std::max(front.second + top.second, iso.second),
                                      w - leftMargin - margin - 4.0 * gap, h - 2.0 * margin - 3.0 * gap);
        return s;
    };
    double scale = layout(297.0, 210.0);
    if (scale < 0.5) {
        d.width = 420.0;
        d.height = 297.0;
        scale = layout(d.width, d.height);
    }
    const double rowY = d.height - margin - gap - front.second * scale / 2.0;
    const double frontX = leftMargin + gap + front.first * scale / 2.0;
    const double leftX = frontX + (front.first + left.first) * scale / 2.0 + gap;
    const double isoX = leftX + (left.first + iso.first) * scale / 2.0 + gap;
    const double topY = rowY - (front.second + top.second) * scale / 2.0 - gap;
    auto add = [&](ViewKind kind, double x, double y) {
        SheetView v;
        v.kind = kind;
        v.at = gp_Pnt2d(x, y);
        v.scale = scale;
        v.hidden = kind != ViewKind::Iso;
        d.views.push_back(v);
    };
    add(ViewKind::Front, frontX, rowY);
    add(ViewKind::Top, frontX, topY);
    add(ViewKind::Left, leftX, rowY);
    add(ViewKind::Iso, isoX, rowY);
    return d;
}

bool resolveDimension(const SheetDimension& dimension, const ViewGeometry& g, gp_Pnt2d& a, gp_Pnt2d& b, double& radius,
                      SheetDimension* followed)
{
    using Kind = SheetDimension::Kind;
    if (dimension.kind == Kind::Radius || dimension.kind == Kind::Diameter) {
        const ViewGeometry::Circle* best = nullptr;
        double bestScore = std::numeric_limits<double>::max();
        for (const ViewGeometry::Circle& c : g.circles) {
            const double score = c.center.Distance(dimension.a) + std::abs(c.radius - dimension.radius);
            if (score < bestScore) {
                bestScore = score;
                best = &c;
            }
        }
        if (!best || bestScore > std::max(2.0, dimension.radius))
            return false;
        a = b = best->at;
        radius = best->radius;
        if (followed) {
            followed->a = followed->b = best->center;
            followed->radius = best->radius;
        }
        return true;
    }
    // Corners and centers alike; each end to the nearest one.
    const double reach = std::max(2.0, 0.25 * dimension.a.Distance(dimension.b));
    auto nearest = [&](const gp_Pnt& p, gp_Pnt& model, gp_Pnt2d& at) {
        double best = std::numeric_limits<double>::max();
        for (const ViewGeometry::Anchor& c : g.corners) {
            if (c.model.Distance(p) < best) {
                best = c.model.Distance(p);
                model = c.model;
                at = c.at;
            }
        }
        for (const ViewGeometry::Circle& c : g.circles) {
            if (c.center.Distance(p) < best) {
                best = c.center.Distance(p);
                model = c.center;
                at = c.at;
            }
        }
        return best <= reach;
    };
    gp_Pnt ma, mb;
    if (!nearest(dimension.a, ma, a) || !nearest(dimension.b, mb, b))
        return false;
    radius = 0.0;
    if (followed) {
        followed->a = ma;
        followed->b = mb;
    }
    return true;
}

double dimensionValue(const SheetDimension& dimension, const gp_Pnt2d& a, const gp_Pnt2d& b, double radius)
{
    switch (dimension.kind) {
    case SheetDimension::Kind::Distance:
        return a.Distance(b);
    case SheetDimension::Kind::Horizontal:
        return std::abs(b.X() - a.X());
    case SheetDimension::Kind::Vertical:
        return std::abs(b.Y() - a.Y());
    case SheetDimension::Kind::Radius:
        return radius;
    case SheetDimension::Kind::Diameter:
        return 2.0 * radius;
    }
    return 0.0;
}
