#include "sketch/Geometry.h"

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
