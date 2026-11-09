#include "sketch/DrawList.h"

#include <algorithm>
#include <cmath>

void appendArc(std::vector<Segment2d>& out, const gp_Pnt2d& center, double radius,
               double startAngle, double sweep)
{
    // ~96 segments per full turn keeps circles smooth at any zoom we care about.
    const int n = std::max(4, static_cast<int>(std::ceil(std::abs(sweep) / (2.0 * M_PI) * 96.0)));
    gp_Pnt2d prev(center.X() + radius * std::cos(startAngle), center.Y() + radius * std::sin(startAngle));
    for (int i = 1; i <= n; ++i) {
        const double t = startAngle + sweep * i / n;
        const gp_Pnt2d next(center.X() + radius * std::cos(t), center.Y() + radius * std::sin(t));
        out.push_back({prev, next});
        prev = next;
    }
}

DrawList tessellate(const Sketch& sketch, const std::set<EntityId>& accent, const std::set<EntityId>& defined,
                    const std::set<PointId>& accentPoints)
{
    DrawList list;
    for (const SketchEntity& e : sketch.entities()) {
        std::vector<Segment2d>& out = accent.count(e.id)    ? list.accentSegments
                                    : e.construction      ? list.constructionSegments
                                    : defined.count(e.id) ? list.definedSegments
                                                          : list.segments;
        const std::vector<gp_Pnt2d> pts = sketch.polyline(e);
        for (std::size_t i = 1; i < pts.size(); ++i)
            out.push_back({pts[i - 1], pts[i]});
    }
    for (const SketchPoint& p : sketch.points())
        (accentPoints.count(p.id) ? list.accentPoints : list.points).push_back(p.pos);
    return list;
}
