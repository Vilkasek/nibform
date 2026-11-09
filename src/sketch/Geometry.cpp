#include "sketch/Geometry.h"

#include <GCE2d_MakeArcOfCircle.hxx>
#include <GCE2d_MakeSegment.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_Interpolate.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_HArray1OfPnt2d.hxx>
#include <gp_Ax22d.hxx>
#include <gp_Circ2d.hxx>
#include <gp_Elips2d.hxx>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kEps = 1e-6; // sketch units: how far apart "touching" circles may be

} // namespace

std::optional<gp_Pnt2d> intersectLines(const gp_Pnt2d& p1, const gp_Vec2d& d1, const gp_Pnt2d& p2,
                                       const gp_Vec2d& d2)
{
    const double den = d1.Crossed(d2);
    if (std::abs(den) <= 1e-12 * d1.Magnitude() * d2.Magnitude())
        return std::nullopt;
    return p1.Translated(d1 * (gp_Vec2d(p1, p2).Crossed(d2) / den));
}

std::vector<gp_Pnt2d> intersectLineCircle(const gp_Pnt2d& p, const gp_Vec2d& d, const gp_Pnt2d& center,
                                          double radius)
{
    std::vector<gp_Pnt2d> out;
    const gp_Vec2d f(center, p);
    const double a = d.SquareMagnitude(), b = 2.0 * f.Dot(d), c = f.SquareMagnitude() - radius * radius;
    const double disc = b * b - 4.0 * a * c;
    if (a <= 0.0 || disc < -kEps * a)
        return out;
    const double root = std::sqrt(std::max(0.0, disc));
    out.push_back(p.Translated(d * ((-b - root) / (2.0 * a))));
    if (root > 0.0)
        out.push_back(p.Translated(d * ((-b + root) / (2.0 * a))));
    return out;
}

std::vector<gp_Pnt2d> intersectCircles(const gp_Pnt2d& c1, double r1, const gp_Pnt2d& c2, double r2)
{
    std::vector<gp_Pnt2d> out;
    const double d = c1.Distance(c2);
    if (d < kEps || d > r1 + r2 + kEps || d < std::abs(r1 - r2) - kEps)
        return out;
    const double along = (r1 * r1 - r2 * r2 + d * d) / (2.0 * d);
    const double h = std::sqrt(std::max(0.0, r1 * r1 - along * along));
    const gp_Vec2d u = gp_Vec2d(c1, c2) / d;
    const gp_Pnt2d mid = c1.Translated(u * along);
    const gp_Vec2d n(-u.Y(), u.X());
    out.push_back(mid.Translated(n * h));
    if (h > 0.0)
        out.push_back(mid.Translated(n * -h));
    return out;
}

namespace {

// Degree-1 B-spline through `pts` (closed: back to the first).
Handle(Geom2d_BSplineCurve) polylineSpline(const std::vector<gp_Pnt2d>& pts, bool closed)
{
    const int n = static_cast<int>(pts.size());
    if (closed && n >= 3) {
        TColgp_Array1OfPnt2d poles(1, n);
        TColStd_Array1OfReal knots(1, n + 1);
        TColStd_Array1OfInteger mults(1, n + 1);
        for (int i = 0; i < n; ++i)
            poles.SetValue(i + 1, pts[static_cast<std::size_t>(i)]);
        for (int i = 0; i <= n; ++i) {
            knots.SetValue(i + 1, i);
            mults.SetValue(i + 1, 1);
        }
        return new Geom2d_BSplineCurve(poles, knots, mults, 1, true);
    }
    TColgp_Array1OfPnt2d poles(1, n);
    TColStd_Array1OfReal knots(1, n);
    TColStd_Array1OfInteger mults(1, n);
    for (int i = 0; i < n; ++i) {
        poles.SetValue(i + 1, pts[static_cast<std::size_t>(i)]);
        knots.SetValue(i + 1, i);
        mults.SetValue(i + 1, i == 0 || i == n - 1 ? 2 : 1);
    }
    return new Geom2d_BSplineCurve(poles, knots, mults, 1);
}

} // namespace

Handle(Geom2d_BSplineCurve) interpolateSpline(const std::vector<gp_Pnt2d>& points, bool closed)
{
    std::vector<gp_Pnt2d> pts;
    for (const gp_Pnt2d& p : points) {
        if (pts.empty() || pts.back().Distance(p) > 1e-9)
            pts.push_back(p);
    }
    if (closed && pts.size() > 1 && pts.front().Distance(pts.back()) <= 1e-9)
        pts.pop_back();
    if (pts.size() < 2)
        return nullptr;
    if (pts.size() == 2 || (closed && pts.size() < 3))
        return polylineSpline(pts, false);
    try {
        Handle(TColgp_HArray1OfPnt2d) poles = new TColgp_HArray1OfPnt2d(1, static_cast<int>(pts.size()));
        for (std::size_t i = 0; i < pts.size(); ++i)
            poles->SetValue(static_cast<int>(i) + 1, pts[i]);
        Geom2dAPI_Interpolate interpolate(poles, closed, 1e-9);
        interpolate.Perform();
        if (interpolate.IsDone())
            return interpolate.Curve();
    } catch (const Standard_Failure&) {
    }
    return polylineSpline(pts, closed);
}

Handle(Geom2d_Curve) curve2d(const Sketch& sketch, const SketchEntity& e)
{
    try {
        switch (e.kind) {
        case EntityKind::Line: {
            const gp_Pnt2d a = sketch.pos(e.a), b = sketch.pos(e.b);
            if (a.Distance(b) < 1e-9)
                return nullptr;
            return GCE2d_MakeSegment(a, b).Value();
        }
        case EntityKind::Circle:
            if (e.radius <= 0.0)
                return nullptr;
            return new Geom2d_Circle(gp_Circ2d(gp_Ax2d(sketch.pos(e.c), gp::DX2d()), e.radius));
        case EntityKind::Arc: {
            const gp_Pnt2d c = sketch.pos(e.c);
            const double r = c.Distance(sketch.pos(e.a));
            if (r <= 0.0)
                return nullptr;
            return GCE2d_MakeArcOfCircle(gp_Circ2d(gp_Ax2d(c, gp::DX2d()), r), sketch.pos(e.a), sketch.pos(e.b), true)
                .Value();
        }
        case EntityKind::Ellipse: {
            const EllipseAxes axes = ellipseAxes(sketch, e);
            const bool aMajor = axes.u.Magnitude() >= axes.v.Magnitude();
            const gp_Vec2d major = aMajor ? axes.u : axes.v;
            const double minor = (aMajor ? axes.v : axes.u).Magnitude();
            if (minor < 1e-9)
                return nullptr;
            return new Geom2d_Ellipse(gp_Elips2d(gp_Ax22d(axes.center, gp_Dir2d(major)), major.Magnitude(), minor));
        }
        case EntityKind::Spline: {
            std::vector<gp_Pnt2d> pts;
            for (PointId p : e.through)
                pts.push_back(sketch.pos(p));
            return interpolateSpline(pts, e.closed);
        }
        }
    } catch (const Standard_Failure&) {
    }
    return nullptr;
}

std::vector<gp_Pnt2d> intersectCurves(const Sketch& sketch, const SketchEntity& a, const SketchEntity& b)
{
    std::vector<gp_Pnt2d> out;
    const Handle(Geom2d_Curve) ca = curve2d(sketch, a), cb = curve2d(sketch, b);
    if (ca.IsNull() || cb.IsNull())
        return out;
    try {
        Geom2dAPI_InterCurveCurve inter(ca, cb, kEps);
        for (int i = 1; i <= inter.NbPoints(); ++i)
            out.push_back(inter.Point(i));
    } catch (const Standard_Failure&) {
    }
    return out;
}
