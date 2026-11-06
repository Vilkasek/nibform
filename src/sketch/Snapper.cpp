#include "sketch/Snapper.h"

#include <cmath>

SnapResult Snapper::snap(const Sketch& sketch, const gp_Pnt2d& raw, double pointTolerance,
                         double alignTolerance, const std::vector<gp_Pnt2d>& anchors) const
{
    SnapResult result;
    result.pos = raw;

    if (auto id = sketch.findPoint(raw, pointTolerance)) {
        result.kind = SnapResult::Kind::Point;
        result.pointId = *id;
        result.pos = sketch.pos(*id);
        return result;
    }

    const gp_Pnt2d origin(0.0, 0.0);
    if (raw.Distance(origin) <= pointTolerance) {
        result.kind = SnapResult::Kind::Origin;
        result.pos = origin;
        return result;
    }

    double bestMid = pointTolerance;
    for (const SketchEntity& e : sketch.entities()) {
        if (e.kind != EntityKind::Line)
            continue;
        const gp_Pnt2d a = sketch.pos(e.a);
        const gp_Pnt2d b = sketch.pos(e.b);
        const gp_Pnt2d mid((a.X() + b.X()) / 2.0, (a.Y() + b.Y()) / 2.0);
        const double d = raw.Distance(mid);
        if (d <= bestMid) {
            bestMid = d;
            result.kind = SnapResult::Kind::Midpoint;
            result.pos = mid;
        }
    }
    if (result.kind == SnapResult::Kind::Midpoint)
        return result;

    std::vector<gp_Pnt2d> others{origin};
    for (const SketchPoint& p : sketch.points())
        others.push_back(p.pos);

    // Nearest reference sharing the cursor's X (axis 0) or Y (axis 1), within tolerance.
    auto nearest = [&](const std::vector<gp_Pnt2d>& refs, int axis) -> const gp_Pnt2d* {
        const gp_Pnt2d* best = nullptr;
        double bestDist = alignTolerance;
        for (const gp_Pnt2d& r : refs) {
            const double d = std::abs(axis == 0 ? raw.X() - r.X() : raw.Y() - r.Y());
            if (d <= bestDist) {
                bestDist = d;
                best = &r;
            }
        }
        return best;
    };

    // Each axis snaps independently, so a point can line up with two different references.
    // The tool's anchors win over closer unrelated points, so drawing from a start point
    // gives exact horizontals and verticals.
    const gp_Pnt2d* refX = nearest(anchors, 0); // shares X with the cursor -> vertical guide
    if (!refX)
        refX = nearest(others, 0);
    const gp_Pnt2d* refY = nearest(anchors, 1); // shares Y with the cursor -> horizontal guide
    if (!refY)
        refY = nearest(others, 1);
    if (!refX && !refY)
        return result;

    result.kind = SnapResult::Kind::Aligned;
    result.pos = gp_Pnt2d(refX ? refX->X() : raw.X(), refY ? refY->Y() : raw.Y());
    if (refX)
        result.guides.push_back({*refX, result.pos});
    if (refY)
        result.guides.push_back({*refY, result.pos});
    return result;
}
