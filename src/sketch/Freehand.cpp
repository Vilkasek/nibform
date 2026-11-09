#include "sketch/Freehand.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// Drawing samples, with repeats (a pen resting) dropped.
std::vector<gp_Pnt2d> cleaned(const std::vector<gp_Pnt2d>& stroke)
{
    std::vector<gp_Pnt2d> out;
    for (const gp_Pnt2d& p : stroke) {
        if (out.empty() || out.back().Distance(p) > 1e-9)
            out.push_back(p);
    }
    return out;
}

double pathLength(const std::vector<gp_Pnt2d>& pts)
{
    double length = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        length += pts[i - 1].Distance(pts[i]);
    return length;
}

double diagonal(const std::vector<gp_Pnt2d>& pts)
{
    double x0 = pts.front().X(), x1 = x0, y0 = pts.front().Y(), y1 = y0;
    for (const gp_Pnt2d& p : pts) {
        x0 = std::min(x0, p.X());
        x1 = std::max(x1, p.X());
        y0 = std::min(y0, p.Y());
        y1 = std::max(y1, p.Y());
    }
    return std::hypot(x1 - x0, y1 - y0);
}

// `segments` + 1 points evenly spaced along the path (so fast and slow parts of the
// stroke weigh the same), keeping both ends.
std::vector<gp_Pnt2d> resampled(const std::vector<gp_Pnt2d>& pts, int segments)
{
    const double step = pathLength(pts) / segments;
    std::vector<gp_Pnt2d> out{pts.front()};
    double carried = 0.0; // distance walked since the last output point
    for (std::size_t i = 1; i < pts.size(); ++i) {
        gp_Pnt2d a = pts[i - 1];
        const gp_Pnt2d b = pts[i];
        double left = a.Distance(b);
        while (carried + left >= step && static_cast<int>(out.size()) < segments) {
            const double t = (step - carried) / left;
            a = gp_Pnt2d(a.X() + (b.X() - a.X()) * t, a.Y() + (b.Y() - a.Y()) * t);
            out.push_back(a);
            left = a.Distance(b);
            carried = 0.0;
        }
        carried += left;
    }
    out.push_back(pts.back());
    return out;
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

// Douglas-Peucker: indices of the points kept between `first` and `last` (exclusive).
void simplify(const std::vector<gp_Pnt2d>& pts, std::size_t first, std::size_t last, double eps,
              std::vector<std::size_t>& keep)
{
    double worst = 0.0;
    std::size_t at = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = segmentDistance(pts[i], pts[first], pts[last]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (worst <= eps)
        return;
    simplify(pts, first, at, eps, keep);
    keep.push_back(at);
    simplify(pts, at, last, eps, keep);
}

// How sharply the path turns at `b` (0 straight on, pi straight back).
double turn(const gp_Pnt2d& a, const gp_Pnt2d& b, const gp_Pnt2d& c)
{
    const gp_Vec2d u(a, b), v(b, c);
    if (u.Magnitude() < 1e-12 || v.Magnitude() < 1e-12)
        return 0.0;
    return std::abs(u.Angle(v));
}

// Drops corners that barely turn (a wobble on a straight side), the gentlest first.
void dropFlatCorners(std::vector<gp_Pnt2d>& corners, bool closed, double minTurn)
{
    while (corners.size() > (closed ? 3u : 2u)) {
        const std::size_t n = corners.size();
        double flattest = minTurn;
        std::optional<std::size_t> at;
        for (std::size_t i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i) {
            const double t = turn(corners[(i + n - 1) % n], corners[i], corners[(i + 1) % n]);
            if (t < flattest) {
                flattest = t;
                at = i;
            }
        }
        if (!at)
            return;
        corners.erase(corners.begin() + static_cast<std::ptrdiff_t>(*at));
    }
}

// Corners of the polyline that follows `pts` within `eps`.
std::vector<gp_Pnt2d> corners(const std::vector<gp_Pnt2d>& pts, bool closed, double eps)
{
    std::vector<std::size_t> keep;
    if (closed) {
        // Split the loop at the point farthest from its start, then simplify both halves.
        std::size_t far = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) {
            if (pts[i].Distance(pts[0]) > pts[far].Distance(pts[0]))
                far = i;
        }
        std::vector<gp_Pnt2d> loop = pts;
        loop.push_back(pts.front());
        keep.push_back(0);
        simplify(loop, 0, far, eps, keep);
        keep.push_back(far);
        simplify(loop, far, loop.size() - 1, eps, keep);
    } else {
        keep.push_back(0);
        simplify(pts, 0, pts.size() - 1, eps, keep);
        keep.push_back(pts.size() - 1);
    }
    std::vector<gp_Pnt2d> out;
    for (std::size_t i : keep)
        out.push_back(pts[i]);
    dropFlatCorners(out, closed, 35.0 * kDeg);
    return out;
}

// Farthest any stroke point lies from the polyline through `corners`.
double deviation(const std::vector<gp_Pnt2d>& pts, const std::vector<gp_Pnt2d>& corners, bool closed)
{
    const std::size_t edges = closed ? corners.size() : corners.size() - 1;
    double worst = 0.0;
    for (const gp_Pnt2d& p : pts) {
        double best = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < edges; ++i)
            best = std::min(best, segmentDistance(p, corners[i], corners[(i + 1) % corners.size()]));
        worst = std::max(worst, best);
    }
    return worst;
}

double meanEdge(const std::vector<gp_Pnt2d>& corners, bool closed)
{
    const std::size_t edges = closed ? corners.size() : corners.size() - 1;
    double sum = 0.0;
    for (std::size_t i = 0; i < edges; ++i)
        sum += corners[i].Distance(corners[(i + 1) % corners.size()]);
    return edges ? sum / edges : 0.0;
}

struct CircleFit
{
    gp_Pnt2d center;
    double radius = 0.0;
    double relRms = std::numeric_limits<double>::max(); // RMS of the radial error / radius
    double sweep = 0.0;                                  // signed angle covered, radians
};

// Algebraic (Kasa) least-squares circle, centered on the points' mean for stability.
CircleFit fitCircle(const std::vector<gp_Pnt2d>& pts)
{
    CircleFit fit;
    double mx = 0.0, my = 0.0;
    for (const gp_Pnt2d& p : pts) {
        mx += p.X();
        my += p.Y();
    }
    mx /= pts.size();
    my /= pts.size();
    Eigen::MatrixXd a(pts.size(), 3);
    Eigen::VectorXd b(pts.size());
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const double x = pts[i].X() - mx, y = pts[i].Y() - my;
        a.row(static_cast<Eigen::Index>(i)) << x, y, 1.0;
        b(static_cast<Eigen::Index>(i)) = -(x * x + y * y);
    }
    const Eigen::Vector3d s = a.colPivHouseholderQr().solve(b);
    const double cx = -s(0) / 2.0, cy = -s(1) / 2.0;
    const double r2 = cx * cx + cy * cy - s(2);
    if (!std::isfinite(r2) || r2 <= 0.0)
        return fit;
    fit.center = gp_Pnt2d(cx + mx, cy + my);
    fit.radius = std::sqrt(r2);

    double sq = 0.0;
    for (const gp_Pnt2d& p : pts) {
        const double e = p.Distance(fit.center) - fit.radius;
        sq += e * e;
    }
    fit.relRms = std::sqrt(sq / pts.size()) / fit.radius;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const gp_Vec2d u(fit.center, pts[i - 1]), v(fit.center, pts[i]);
        if (u.Magnitude() > 1e-12 && v.Magnitude() > 1e-12)
            fit.sweep += u.Angle(v);
    }
    return fit;
}

// Back and forth: the path doubles back on itself several times.
bool isScribble(const std::vector<gp_Pnt2d>& pts, double length, double diag)
{
    if (length < 2.5 * diag)
        return false;
    int reversals = 0;
    for (std::size_t i = 2; i + 2 < pts.size(); ++i) {
        const gp_Vec2d before(pts[i - 2], pts[i]), after(pts[i], pts[i + 2]);
        if (before.Magnitude() < 1e-12 || after.Magnitude() < 1e-12)
            continue;
        if (std::abs(before.Angle(after)) > 140.0 * kDeg) {
            ++reversals;
            i += 3; // one reversal, however many samples it is spread over
        }
    }
    return reversals >= 3;
}

// Four corners turning about a right angle each.
bool rightAngled(const std::vector<gp_Pnt2d>& c)
{
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(turn(c[(i + 3) % 4], c[i], c[(i + 1) % 4]) - kPi / 2.0) > 22.0 * kDeg)
            return false;
    }
    return true;
}

// The rectangle that best fits four rough corners: sides along the mean direction of
// the drawn ones (straightened to the axes when within a few degrees).
std::vector<gp_Pnt2d> squareUp(const std::vector<gp_Pnt2d>& c)
{
    double sx = 0.0, sy = 0.0; // mean of the side directions, modulo a right angle
    for (std::size_t i = 0; i < 4; ++i) {
        const gp_Vec2d side(c[i], c[(i + 1) % 4]);
        const double angle = std::atan2(side.Y(), side.X());
        sx += side.Magnitude() * std::cos(4.0 * angle);
        sy += side.Magnitude() * std::sin(4.0 * angle);
    }
    double angle = std::atan2(sy, sx) / 4.0;
    if (std::abs(angle) < 8.0 * kDeg)
        angle = 0.0;
    const gp_Vec2d u(std::cos(angle), std::sin(angle)), v(-std::sin(angle), std::cos(angle));
    std::vector<double> us, vs;
    for (const gp_Pnt2d& p : c) {
        us.push_back(gp_Vec2d(p.XY()).Dot(u));
        vs.push_back(gp_Vec2d(p.XY()).Dot(v));
    }
    std::sort(us.begin(), us.end());
    std::sort(vs.begin(), vs.end());
    const double u0 = (us[0] + us[1]) / 2.0, u1 = (us[2] + us[3]) / 2.0;
    const double v0 = (vs[0] + vs[1]) / 2.0, v1 = (vs[2] + vs[3]) / 2.0;
    auto at = [&](double a, double b) { return gp_Pnt2d((u * a + v * b).XY()); };
    return {at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)};
}

} // namespace

StrokeShape recognizeStroke(const std::vector<gp_Pnt2d>& stroke, double tolerance)
{
    StrokeShape shape;
    const std::vector<gp_Pnt2d> pts = cleaned(stroke);
    if (pts.size() < 2)
        return shape;
    const double length = pathLength(pts);
    const double diag = diagonal(pts);
    if (length < tolerance)
        return shape; // a tap, not a stroke
    const std::vector<gp_Pnt2d> even = resampled(pts, 96);

    if (isScribble(even, length, diag)) {
        shape.kind = StrokeShape::Kind::Scribble;
        shape.points = pts;
        return shape;
    }

    // Straight: nothing strays far from the chord, and the stroke doesn't double back.
    const gp_Pnt2d first = pts.front(), last = pts.back();
    const double chord = first.Distance(last);
    double offChord = 0.0;
    for (const gp_Pnt2d& p : pts)
        offChord = std::max(offChord, segmentDistance(p, first, last));
    if (chord >= 0.8 * length && offChord <= std::max(tolerance, 0.07 * chord)) {
        shape.kind = StrokeShape::Kind::Line;
        shape.points = {first, last};
        return shape;
    }

    const double eps = std::max(tolerance, 0.05 * diag);
    // Closed: it comes back to where it started (any overshoot past that is cut off).
    std::size_t closeAt = 0;
    double gap = std::numeric_limits<double>::max();
    double walked = 0.0;
    for (std::size_t i = 1; i < even.size(); ++i) {
        walked += even[i - 1].Distance(even[i]);
        if (walked > 0.5 * length && even[i].Distance(even[0]) < gap) {
            gap = even[i].Distance(even[0]);
            closeAt = i;
        }
    }
    const bool closed = gap <= std::max(1.5 * tolerance, 0.12 * length);

    if (closed) {
        const std::vector<gp_Pnt2d> loop(even.begin(), even.begin() + static_cast<std::ptrdiff_t>(closeAt) + 1);
        const CircleFit circle = fitCircle(loop);
        const std::vector<gp_Pnt2d> poly = corners(loop, true, eps);
        const bool circleOk = circle.relRms < 0.12 && std::abs(circle.sweep) > 1.5 * kPi;
        // Many corners on something roundish are a wobbly circle, not a polygon.
        const std::size_t most = circleOk ? 4 : 8;
        const bool polyOk = poly.size() >= 3 && poly.size() <= most
                         && deviation(loop, poly, true) <= std::max(0.5 * tolerance, 0.08 * meanEdge(poly, true));
        if (circleOk && (circle.relRms < 0.05 || !polyOk)) {
            shape.kind = StrokeShape::Kind::Circle;
            shape.center = circle.center;
            shape.radius = circle.radius;
            return shape;
        }
        if (!polyOk)
            return shape;
        if (poly.size() == 4 && rightAngled(poly)) {
            shape.kind = StrokeShape::Kind::Rectangle;
            shape.points = squareUp(poly);
        } else {
            shape.kind = StrokeShape::Kind::Polyline;
            shape.points = poly;
        }
        shape.closed = true;
        return shape;
    }

    const CircleFit circle = fitCircle(even);
    const double sweep = std::abs(circle.sweep);
    const bool arcOk = circle.relRms < 0.06 && sweep >= 35.0 * kDeg && sweep <= 340.0 * kDeg;
    const std::vector<gp_Pnt2d> poly = corners(even, false, eps);
    const bool polyOk = poly.size() >= 3 && poly.size() <= 9
                     && deviation(even, poly, false) <= std::max(0.5 * tolerance, 0.08 * meanEdge(poly, false));
    if (arcOk && (!polyOk || circle.relRms < 0.03)) {
        auto onCircle = [&](const gp_Pnt2d& p) {
            gp_Vec2d d(circle.center, p);
            if (d.Magnitude() < 1e-12)
                d = gp_Vec2d(1.0, 0.0);
            return gp_Pnt2d((circle.center.XY() + d.Normalized().XY() * circle.radius));
        };
        shape.kind = StrokeShape::Kind::Arc;
        shape.center = circle.center;
        shape.radius = circle.radius;
        shape.points = circle.sweep > 0.0 ? std::vector{onCircle(first), onCircle(last)}
                                          : std::vector{onCircle(last), onCircle(first)};
        return shape;
    }
    if (polyOk) {
        shape.kind = StrokeShape::Kind::Polyline;
        shape.points = poly;
    }
    return shape;
}

std::vector<EntityId> addStrokeShape(Sketch& sketch, const StrokeShape& shape, double snap)
{
    std::vector<EntityId> made;
    auto existing = [&](const gp_Pnt2d& p) -> std::optional<PointId> { return sketch.findPoint(p, snap); };

    switch (shape.kind) {
    case StrokeShape::Kind::None:
    case StrokeShape::Kind::Scribble:
        return made;

    case StrokeShape::Kind::Circle: {
        const PointId c = existing(shape.center).value_or(kNoId);
        made.push_back(sketch.addCircle(c != kNoId ? c : sketch.addPoint(shape.center), shape.radius));
        return made;
    }

    case StrokeShape::Kind::Arc: {
        gp_Pnt2d center = shape.center;
        gp_Pnt2d start = shape.points[0], end = shape.points[1];
        const auto s = existing(start), e = existing(end);
        if (s && e && *s == *e)
            return made;
        if (s)
            start = sketch.pos(*s);
        if (e)
            end = sketch.pos(*e);
        if (s && e) {
            // Both ends given: the center moves along their bisector, as little as it can.
            const gp_Pnt2d mid((start.XY() + end.XY()) / 2.0);
            const gp_Vec2d chord(start, end);
            if (chord.Magnitude() < 1e-12)
                return made;
            const gp_Vec2d normal(-chord.Y() / chord.Magnitude(), chord.X() / chord.Magnitude());
            center = gp_Pnt2d(mid.XY() + normal.XY() * gp_Vec2d(mid, center).Dot(normal));
        } else if (s || e) {
            // One end given: it sets the radius, the other end slides onto it.
            const double r = (s ? start : end).Distance(center);
            gp_Pnt2d& loose = s ? end : start;
            const gp_Vec2d d(center, loose);
            if (d.Magnitude() < 1e-12)
                return made;
            loose = gp_Pnt2d(center.XY() + d.Normalized().XY() * r);
        }
        const PointId c = existing(center).value_or(kNoId);
        const PointId cid = c != kNoId ? c : sketch.addPoint(center);
        const PointId sid = s ? *s : sketch.addPoint(start);
        const PointId eid = e ? *e : sketch.addPoint(end);
        made.push_back(sketch.addArc(cid, sid, eid));
        return made;
    }

    case StrokeShape::Kind::Line:
    case StrokeShape::Kind::Polyline:
    case StrokeShape::Kind::Rectangle:
        break;
    }

    // Straight sides: corners join nearby points, sides close to level / upright are
    // made exactly so, moving only corners that aren't already given.
    std::vector<gp_Pnt2d> verts = shape.points;
    const std::size_t n = verts.size();
    const bool closed = shape.closed || shape.kind == StrokeShape::Kind::Rectangle;
    std::vector<std::optional<PointId>> ids(n);
    std::vector<bool> lockX(n, false), lockY(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        ids[i] = existing(verts[i]);
        if (ids[i]) {
            verts[i] = sketch.pos(*ids[i]);
            lockX[i] = lockY[i] = true;
        }
    }

    enum class Axis { None, Horizontal, Vertical };
    struct Side
    {
        std::size_t a, b;
        Axis axis = Axis::None;
    };
    std::vector<Side> sides;
    const double slope = std::tan(7.0 * kDeg);
    for (std::size_t i = 0; i + 1 < n || (closed && i < n); ++i) {
        Side side{i, (i + 1) % n};
        const gp_Vec2d d(verts[side.a], verts[side.b]);
        if (std::abs(d.Y()) <= slope * std::abs(d.X()))
            side.axis = Axis::Horizontal;
        else if (std::abs(d.X()) <= slope * std::abs(d.Y()))
            side.axis = Axis::Vertical;
        sides.push_back(side);
    }
    auto level = [&](Side& side, bool horizontal) {
        std::vector<bool>& lock = horizontal ? lockY : lockX;
        auto coord = [&](std::size_t i) -> double { return horizontal ? verts[i].Y() : verts[i].X(); };
        auto set = [&](std::size_t i, double v) {
            if (horizontal)
                verts[i].SetY(v);
            else
                verts[i].SetX(v);
            lock[i] = true;
        };
        if (lock[side.a] && lock[side.b]) {
            if (coord(side.a) != coord(side.b))
                side.axis = Axis::None; // both ends given and not in line: leave it slanted
        } else if (lock[side.a]) {
            set(side.b, coord(side.a));
        } else if (lock[side.b]) {
            set(side.a, coord(side.b));
        } else {
            const double mid = (coord(side.a) + coord(side.b)) / 2.0;
            set(side.a, mid);
            set(side.b, mid);
        }
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (Side& side : sides) {
            if (side.axis != Axis::None)
                level(side, side.axis == Axis::Horizontal);
        }
    }

    std::vector<PointId> pid(n);
    for (std::size_t i = 0; i < n; ++i)
        pid[i] = ids[i] ? *ids[i] : sketch.addPoint(verts[i]);
    std::vector<EntityId> lines;
    for (const Side& side : sides) {
        const PointId a = pid[side.a], b = pid[side.b];
        if (a == b || sketch.pos(a).Distance(sketch.pos(b)) < 1e-9) {
            lines.push_back(kNoId);
            continue;
        }
        const EntityId line = sketch.addLine(a, b);
        made.push_back(line);
        lines.push_back(line);
        const gp_Pnt2d pa = sketch.pos(a), pb = sketch.pos(b);
        SketchConstraint c;
        c.p1 = a;
        c.p2 = b;
        c.e1 = line;
        if (side.axis == Axis::Horizontal && pa.Y() == pb.Y()) {
            c.kind = ConstraintKind::Horizontal;
            sketch.addConstraint(c);
        } else if (side.axis == Axis::Vertical && pa.X() == pb.X()) {
            c.kind = ConstraintKind::Vertical;
            sketch.addConstraint(c);
        }
    }

    // A turned rectangle keeps its right angles (three of them fix the fourth).
    const bool turned = std::none_of(sides.begin(), sides.end(), [](const Side& s) { return s.axis != Axis::None; });
    if (shape.kind == StrokeShape::Kind::Rectangle && turned && lines.size() == 4
        && std::none_of(lines.begin(), lines.end(), [](EntityId id) { return id == kNoId; })) {
        for (std::size_t i = 0; i < 3; ++i) {
            SketchConstraint c;
            c.kind = ConstraintKind::Perpendicular;
            c.e1 = lines[i];
            c.e2 = lines[i + 1];
            sketch.addConstraint(c);
        }
    }
    return made;
}

std::set<EntityId> entitiesCrossed(const Sketch& sketch, const std::vector<gp_Pnt2d>& stroke, double tolerance)
{
    std::set<EntityId> crossed;
    std::vector<gp_Pnt2d> pts = cleaned(stroke);
    if (pts.empty())
        return crossed;
    // Dense enough that a fast stroke can't jump over a curve between two samples.
    if (pts.size() > 1) {
        const int segments = std::clamp(static_cast<int>(pathLength(pts) / (0.5 * tolerance)), 1, 4000);
        pts = resampled(pts, segments);
    }
    for (const SketchEntity& e : sketch.entities()) {
        for (const gp_Pnt2d& p : pts) {
            if (sketch.distanceTo(e, p) <= tolerance) {
                crossed.insert(e.id);
                break;
            }
        }
    }
    return crossed;
}
