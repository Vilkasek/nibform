#include "sketch/SketchTools.h"

#include "sketch/Offset.h"
#include "sketch/SketchOps.h"
#include "sketch/Trim.h"

#include <QCoreApplication>

#include <cmath>

namespace {

constexpr double kMinSize = 1e-6;

QString mm(double v)
{
    return QString::number(v, 'f', 2) + QStringLiteral(" mm");
}

void appendPolyline(std::vector<Segment2d>& out, const std::vector<gp_Pnt2d>& pts)
{
    for (std::size_t i = 1; i < pts.size(); ++i)
        out.push_back({pts[i - 1], pts[i]});
}

// The given curves of `sketch` as segments.
void appendEntities(std::vector<Segment2d>& out, const Sketch& sketch, const std::set<EntityId>& ids)
{
    for (EntityId id : ids) {
        if (const SketchEntity* e = sketch.entity(id))
            appendPolyline(out, sketch.polyline(*e));
    }
}

void appendEntities(std::vector<Segment2d>& out, const Sketch& sketch, const std::vector<EntityId>& ids)
{
    appendEntities(out, sketch, std::set<EntityId>(ids.begin(), ids.end()));
}

// Unit vector a quarter turn counter-clockwise from `d`.
gp_Vec2d leftOf(const gp_Vec2d& d)
{
    const double len = d.Magnitude();
    return len > 0.0 ? gp_Vec2d(-d.Y() / len, d.X() / len) : gp_Vec2d(0.0, 1.0);
}

// Polyline: each segment starts where the previous one ended, until the user
// lands on an existing point (closing a loop) or cancels.
class LineTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (!m_start) {
            m_start = s;
            m_pressStarted = true;
            return;
        }
        finish(s);
    }

    void release(const SnapResult& s, bool dragged) override
    {
        // A drag draws a single stroke; tap-tap keeps chaining segments.
        if (m_pressStarted && dragged) {
            finish(s);
            m_start.reset();
        }
        m_pressStarted = false;
    }

    bool cancel() override
    {
        const bool active = m_start.has_value();
        m_start.reset();
        m_pressStarted = false;
        return active;
    }

    void preview(DrawList& out) const override
    {
        if (m_start)
            out.segments.push_back({m_start->pos, m_cursor});
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        return m_start ? std::vector<gp_Pnt2d>{m_start->pos} : std::vector<gp_Pnt2d>{};
    }

    QString hint() const override
    {
        if (!m_start)
            return QCoreApplication::translate("SketchTool", "Line: pick the start");
        const double dx = m_cursor.X() - m_start->pos.X();
        const double dy = m_cursor.Y() - m_start->pos.Y();
        const double angle = std::atan2(dy, dx) * 180.0 / M_PI;
        return QCoreApplication::translate("SketchTool", "Line: %1, angle %2°  ·  Esc finishes")
            .arg(mm(std::hypot(dx, dy)))
            .arg(angle, 0, 'f', 1);
    }

private:
    void finish(const SnapResult& end)
    {
        if (end.pos.Distance(m_start->pos) < kMinSize)
            return;

        const SnapResult start = *m_start;
        PointId endId = kNoId;
        m_host.commit([&](Sketch& sketch) {
            const PointId a = resolveConstrained(sketch, start);
            endId = resolveConstrained(sketch, end);
            constrainAxis(sketch, a, endId, sketch.addLine(a, endId));
        });

        if (end.pointId != kNoId) {
            m_start.reset(); // closed onto existing geometry
        } else {
            m_start = end;
            m_start->pointId = endId;
        }
        m_pressStarted = false;
    }

    std::optional<SnapResult> m_start;
    bool m_pressStarted = false;
};

// Two opposite corners; also used as the base for the other two-click tools.
class TwoPointTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (!m_first) {
            m_first = s;
            m_pressStarted = true;
            return;
        }
        finishAndReset(s);
    }

    void release(const SnapResult& s, bool dragged) override
    {
        if (m_pressStarted && dragged)
            finishAndReset(s);
        m_pressStarted = false;
    }

    bool cancel() override
    {
        const bool active = m_first.has_value();
        m_first.reset();
        m_pressStarted = false;
        return active;
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        return m_first ? std::vector<gp_Pnt2d>{m_first->pos} : std::vector<gp_Pnt2d>{};
    }

protected:
    virtual void finish(const SnapResult& first, const SnapResult& second) = 0;

    std::optional<SnapResult> m_first;

private:
    void finishAndReset(const SnapResult& s)
    {
        const SnapResult first = *m_first;
        m_first.reset();
        m_pressStarted = false;
        finish(first, s);
    }

    bool m_pressStarted = false;
};

class RectangleTool : public TwoPointTool
{
public:
    using TwoPointTool::TwoPointTool;

    void preview(DrawList& out) const override
    {
        if (!m_first)
            return;
        const gp_Pnt2d a = m_first->pos;
        const gp_Pnt2d c = m_cursor;
        const gp_Pnt2d b(c.X(), a.Y());
        const gp_Pnt2d d(a.X(), c.Y());
        out.segments.insert(out.segments.end(), {{a, b}, {b, c}, {c, d}, {d, a}});
    }

    QString hint() const override
    {
        if (!m_first)
            return QCoreApplication::translate("SketchTool", "Rectangle: pick the first corner");
        return QCoreApplication::translate("SketchTool", "Rectangle: %1 × %2")
            .arg(mm(std::abs(m_cursor.X() - m_first->pos.X())))
            .arg(mm(std::abs(m_cursor.Y() - m_first->pos.Y())));
    }

protected:
    void finish(const SnapResult& first, const SnapResult& second) override
    {
        const gp_Pnt2d a = first.pos;
        const gp_Pnt2d c = second.pos;
        if (std::abs(c.X() - a.X()) < kMinSize || std::abs(c.Y() - a.Y()) < kMinSize)
            return;

        m_host.commit([&](Sketch& sketch) {
            const PointId p1 = resolveConstrained(sketch, first);
            const PointId p2 = sketch.pointAt(gp_Pnt2d(c.X(), a.Y()));
            const PointId p3 = resolveConstrained(sketch, second);
            const PointId p4 = sketch.pointAt(gp_Pnt2d(a.X(), c.Y()));
            // Corners are built aligned, so every side gets its horizontal / vertical.
            constrainAxis(sketch, p1, p2, sketch.addLine(p1, p2));
            constrainAxis(sketch, p2, p3, sketch.addLine(p2, p3));
            constrainAxis(sketch, p3, p4, sketch.addLine(p3, p4));
            constrainAxis(sketch, p4, p1, sketch.addLine(p4, p1));
        });
    }
};

class CircleTool : public TwoPointTool
{
public:
    using TwoPointTool::TwoPointTool;

    void preview(DrawList& out) const override
    {
        if (!m_first)
            return;
        appendArc(out.segments, m_first->pos, m_first->pos.Distance(m_cursor), 0.0, 2.0 * M_PI);
        out.accentPoints.push_back(m_first->pos);
    }

    QString hint() const override
    {
        if (!m_first)
            return QCoreApplication::translate("SketchTool", "Circle: pick the center");
        const double r = m_first->pos.Distance(m_cursor);
        return QCoreApplication::translate("SketchTool", "Circle: radius %1, diameter %2").arg(mm(r), mm(2.0 * r));
    }

protected:
    void finish(const SnapResult& center, const SnapResult& rim) override
    {
        const double r = center.pos.Distance(rim.pos);
        if (r < kMinSize)
            return;
        m_host.commit([&](Sketch& sketch) { sketch.addCircle(resolveConstrained(sketch, center), r); });
    }
};

// Three-point arc: start, end, then a point the arc passes through.
class ArcTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (!m_start) {
            m_start = s;
            m_pressStarted = true;
        } else if (!m_end) {
            setEnd(s);
        } else {
            finish(s);
        }
    }

    void release(const SnapResult& s, bool dragged) override
    {
        if (m_pressStarted && dragged && !m_end)
            setEnd(s);
        m_pressStarted = false;
    }

    bool cancel() override
    {
        const bool active = m_start.has_value();
        m_start.reset();
        m_end.reset();
        m_pressStarted = false;
        return active;
    }

    void preview(DrawList& out) const override
    {
        if (!m_start)
            return;
        if (!m_end) {
            out.segments.push_back({m_start->pos, m_cursor});
            return;
        }
        auto center = circumcenter(m_start->pos, m_end->pos, m_cursor);
        if (!center) {
            out.segments.push_back({m_start->pos, m_end->pos});
            return;
        }
        const bool ccw = isOnCcwArc(*center, m_start->pos, m_end->pos, m_cursor);
        const gp_Pnt2d from = ccw ? m_start->pos : m_end->pos;
        const gp_Pnt2d to = ccw ? m_end->pos : m_start->pos;
        const double a0 = std::atan2(from.Y() - center->Y(), from.X() - center->X());
        double sweep = std::atan2(to.Y() - center->Y(), to.X() - center->X()) - a0;
        if (sweep <= 0.0)
            sweep += 2.0 * M_PI;
        appendArc(out.segments, *center, center->Distance(from), a0, sweep);
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        std::vector<gp_Pnt2d> a;
        if (m_start)
            a.push_back(m_start->pos);
        if (m_end)
            a.push_back(m_end->pos);
        return a;
    }

    QString hint() const override
    {
        if (!m_start)
            return QCoreApplication::translate("SketchTool", "Arc: pick the start");
        if (!m_end)
            return QCoreApplication::translate("SketchTool", "Arc: pick the end");
        if (auto c = circumcenter(m_start->pos, m_end->pos, m_cursor))
            return QCoreApplication::translate("SketchTool", "Arc: radius %1").arg(mm(c->Distance(m_start->pos)));
        return QCoreApplication::translate("SketchTool", "Arc: pick a point on the arc");
    }

private:
    void setEnd(const SnapResult& s)
    {
        if (s.pos.Distance(m_start->pos) < kMinSize)
            return;
        m_end = s;
    }

    void finish(const SnapResult& through)
    {
        auto center = circumcenter(m_start->pos, m_end->pos, through.pos);
        if (!center)
            return;

        const SnapResult start = *m_start;
        const SnapResult end = *m_end;
        const bool ccw = isOnCcwArc(*center, start.pos, end.pos, through.pos);
        m_host.commit([&](Sketch& sketch) {
            const PointId c = sketch.pointAt(*center);
            const PointId a = resolveConstrained(sketch, start);
            const PointId b = resolveConstrained(sketch, end);
            if (ccw)
                sketch.addArc(c, a, b);
            else
                sketch.addArc(c, b, a);
        });
        cancel();
    }

    std::optional<SnapResult> m_start;
    std::optional<SnapResult> m_end;
    bool m_pressStarted = false;
};

// Fusion-style trim: the piece of curve between its nearest crossings is highlighted
// under the pointer; a tap removes it, a stroke removes every piece it passes over.
class TrimTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        m_down = true;
        m_stroke = false;
        trimAt(s.pos);
    }

    void release(const SnapResult&, bool) override
    {
        m_down = false;
        m_stroke = false;
    }

    void hover(const SnapResult& s) override
    {
        SketchTool::hover(s);
        if (m_down)
            trimAt(s.pos);
    }

    bool cancel() override
    {
        m_down = false;
        m_stroke = false;
        return false; // nothing to abandon: every trim is already done
    }

    void preview(DrawList& out) const override
    {
        if (auto piece = findTrimPiece(m_host.sketch(), m_cursor, m_host.pickTolerance()))
            out.accentSegments = trimPieceSegments(m_host.sketch(), *piece);
    }

    std::vector<gp_Pnt2d> anchors() const override { return {}; }
    bool snaps() const override { return false; }

    QString hint() const override
    {
        return QCoreApplication::translate("SketchTool", "Trim: tap a piece to remove it  ·  drag across several at once");
    }

private:
    void trimAt(const gp_Pnt2d& pos)
    {
        const auto piece = findTrimPiece(m_host.sketch(), pos, m_host.pickTolerance());
        if (!piece)
            return;
        m_host.commit([&](Sketch& sketch) { trim(sketch, *piece); }, m_stroke);
        m_stroke = true;
    }

    bool m_down = false;
    bool m_stroke = false; // a trim already happened in this press-drag
};

// Offset: tap a curve to take its whole connected chain, then the copy follows the
// pointer (the side and rough distance); the next tap asks for the exact distance.
class OffsetTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (m_editing)
            return;
        if (m_chain) {
            m_confirm = true;
            return;
        }
        if (auto id = m_host.sketch().hitTest(s.pos, m_host.pickTolerance()))
            m_chain = chainFrom(m_host.sketch(), *id);
        m_picked = m_chain.has_value();
        m_failed = false;
    }

    void release(const SnapResult&, bool dragged) override
    {
        const bool finishNow = !m_editing && (m_confirm || (m_picked && dragged));
        m_confirm = m_picked = false;
        if (finishNow)
            finish();
    }

    bool cancel() override
    {
        const bool active = m_chain.has_value();
        m_chain.reset();
        m_confirm = m_picked = m_failed = m_editing = false;
        return active;
    }

    void preview(DrawList& out) const override
    {
        const Sketch& sketch = m_host.sketch();
        if (!m_chain) {
            // Show which chain a tap would take.
            if (auto id = sketch.hitTest(m_cursor, m_host.pickTolerance())) {
                if (auto chain = chainFrom(sketch, *id))
                    out.accentSegments = tessellate(sketch, ids(*chain)).accentSegments;
            }
            return;
        }
        out.accentSegments = tessellate(sketch, ids(*m_chain)).accentSegments;
        Sketch probe = sketch;
        const std::vector<EntityId> created = offsetChain(probe, *m_chain, distance());
        out.segments = tessellate(probe, {created.begin(), created.end()}).accentSegments;
    }

    std::vector<gp_Pnt2d> anchors() const override { return {}; }
    bool snaps() const override { return false; }

    QString hint() const override
    {
        if (!m_chain)
            return QCoreApplication::translate("SketchTool", "Offset: tap a curve (takes its whole connected chain)");
        if (m_failed)
            return QCoreApplication::translate("SketchTool", "Offset: too far, the curves would fold over  ·  Esc cancels");
        if (m_editing)
            return QCoreApplication::translate("SketchTool", "Offset: type the distance  ·  Enter accepts, Esc cancels");
        return QCoreApplication::translate("SketchTool", "Offset: %1  ·  tap to type an exact value  ·  Esc cancels")
            .arg(mm(std::abs(distance())));
    }

private:
    static std::set<EntityId> ids(const Chain& chain)
    {
        std::set<EntityId> out;
        for (const ChainLink& link : chain.links)
            out.insert(link.entity);
        return out;
    }

    // Signed: positive to the left of the chain, as offsetChain() expects.
    // Frozen while the exact value is being typed.
    double distance() const { return m_editing ? m_frozen : chainSide(m_host.sketch(), *m_chain, m_cursor); }

    void finish()
    {
        m_frozen = distance();
        m_editing = true;
        const double side = m_frozen < 0.0 ? -1.0 : 1.0;
        m_host.askValue(
            [] { return QCoreApplication::translate("SketchTool", "Offset [mm]"); }, std::round(std::abs(m_frozen) * 100.0) / 100.0, 1e-3, 1e6,
            [this, side](double value) {
                m_editing = false;
                apply(side * value);
            },
            [this] { m_editing = false; });
    }

    void apply(double distance)
    {
        Sketch probe = m_host.sketch();
        if (offsetChain(probe, *m_chain, distance).empty()) {
            m_failed = true;
            return;
        }
        const Chain chain = *m_chain;
        m_host.commit([&](Sketch& sketch) { offsetChain(sketch, chain, distance); });
        cancel();
    }

    std::optional<Chain> m_chain;
    bool m_picked = false;  // this press took the chain (a drag from here finishes it)
    bool m_confirm = false; // a second press with a chain taken
    bool m_editing = false; // the exact value is being typed
    bool m_failed = false;
    double m_frozen = 0.0;
};


// Regular polygon: center, then a corner; the number of sides is asked last (and
// remembered for the next one).
class PolygonTool : public TwoPointTool
{
public:
    using TwoPointTool::TwoPointTool;

    bool cancel() override
    {
        const bool pending = m_pending.has_value();
        m_pending.reset();
        return TwoPointTool::cancel() || pending;
    }

    void preview(DrawList& out) const override
    {
        if (m_pending)
            outline(out, m_pending->first.pos, m_pending->second.pos);
        else if (m_first)
            outline(out, m_first->pos, m_cursor);
    }

    QString hint() const override
    {
        if (m_pending)
            return QCoreApplication::translate("SketchTool", "Polygon: type the number of sides  ·  Enter accepts, Esc cancels");
        if (!m_first)
            return QCoreApplication::translate("SketchTool", "Polygon: pick the center");
        return QCoreApplication::translate("SketchTool", "Polygon: %1 sides, radius %2  ·  pick a corner").arg(s_sides).arg(mm(m_first->pos.Distance(m_cursor)));
    }

protected:
    void finish(const SnapResult& center, const SnapResult& corner) override
    {
        if (center.pos.Distance(corner.pos) < kMinSize)
            return;
        m_pending = {center, corner};
        m_host.askValue([] { return QCoreApplication::translate("SketchTool", "Sides"); }, s_sides, 3, 64,
                        [this](double sides) {
                            s_sides = static_cast<int>(std::lround(sides));
                            apply();
                        },
                        [this] { m_pending.reset(); });
    }

private:
    static void outline(DrawList& out, const gp_Pnt2d& center, const gp_Pnt2d& corner)
    {
        const gp_Vec2d r(center, corner);
        if (r.Magnitude() < kMinSize)
            return;
        std::vector<gp_Pnt2d> pts;
        for (int k = 0; k <= s_sides; ++k)
            pts.push_back(center.Translated(r.Rotated(2.0 * M_PI * k / s_sides)));
        appendPolyline(out.segments, pts);
        out.accentPoints.push_back(center);
    }

    void apply()
    {
        if (!m_pending)
            return;
        const auto [center, corner] = *m_pending;
        m_pending.reset();
        m_host.commit([&](Sketch& sketch) {
            const PointId c = resolveConstrained(sketch, center);
            const PointId p = resolveConstrained(sketch, corner);
            constrainAxis(sketch, c, p, kNoId); // a corner straight above / beside the center
            addPolygon(sketch, c, p, s_sides);
        });
    }

    inline static int s_sides = 6;
    std::optional<std::pair<SnapResult, SnapResult>> m_pending; // waiting for the number of sides
};

// Three steps: two points, then a third that sets the size across (slot width,
// ellipse's other axis). A drag from the first point sets the second.
class ThreeStepTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (!m_first) {
            m_first = s;
            m_pressStarted = true;
        } else if (!m_second) {
            setSecond(s);
        } else {
            const SnapResult first = *m_first, second = *m_second;
            if (finish(first, second, s))
                cancel();
        }
    }

    void release(const SnapResult& s, bool dragged) override
    {
        if (m_pressStarted && dragged && !m_second)
            setSecond(s);
        m_pressStarted = false;
    }

    bool cancel() override
    {
        const bool active = m_first.has_value();
        m_first.reset();
        m_second.reset();
        m_pressStarted = false;
        return active;
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        std::vector<gp_Pnt2d> a;
        if (m_first)
            a.push_back(m_first->pos);
        if (m_second)
            a.push_back(m_second->pos);
        return a;
    }

protected:
    // Distance of `p` from the line through the first two points.
    double across(const gp_Pnt2d& p) const
    {
        const gp_Vec2d d(m_first->pos, m_second->pos);
        return std::abs(leftOf(d).Dot(gp_Vec2d(m_first->pos, p)));
    }
    // False keeps the tool where it is (the size was too small).
    virtual bool finish(const SnapResult& first, const SnapResult& second, const SnapResult& third) = 0;

    std::optional<SnapResult> m_first;
    std::optional<SnapResult> m_second;

private:
    void setSecond(const SnapResult& s)
    {
        if (s.pos.Distance(m_first->pos) >= kMinSize)
            m_second = s;
    }

    bool m_pressStarted = false;
};

// Slot: the centers of its two round ends, then its width.
class SlotTool : public ThreeStepTool
{
public:
    using ThreeStepTool::ThreeStepTool;

    void preview(DrawList& out) const override
    {
        if (!m_first)
            return;
        if (!m_second) {
            out.segments.push_back({m_first->pos, m_cursor});
            return;
        }
        const double half = across(m_cursor);
        const gp_Pnt2d c1 = m_first->pos, c2 = m_second->pos;
        out.constructionSegments.push_back({c1, c2});
        if (half < kMinSize)
            return;
        const gp_Vec2d n = leftOf(gp_Vec2d(c1, c2)) * half;
        out.segments.push_back({c1.Translated(n), c2.Translated(n)});
        out.segments.push_back({c2.Translated(-n), c1.Translated(-n)});
        const double up = std::atan2(n.Y(), n.X());
        appendArc(out.segments, c2, half, up - M_PI, M_PI);
        appendArc(out.segments, c1, half, up, M_PI);
    }

    QString hint() const override
    {
        if (!m_first)
            return QCoreApplication::translate("SketchTool", "Slot: pick the center of one end");
        if (!m_second)
            return QCoreApplication::translate("SketchTool", "Slot: pick the center of the other end  ·  %1").arg(mm(m_first->pos.Distance(m_cursor)));
        return QCoreApplication::translate("SketchTool", "Slot: pick the width  ·  %1").arg(mm(2.0 * across(m_cursor)));
    }

protected:
    bool finish(const SnapResult& first, const SnapResult& second, const SnapResult& third) override
    {
        const double width = 2.0 * across(third.pos);
        if (width < kMinSize)
            return false;
        m_host.commit([&](Sketch& sketch) {
            const PointId c1 = resolveConstrained(sketch, first);
            const PointId c2 = resolveConstrained(sketch, second);
            const EntityId centerline = addSlot(sketch, c1, c2, width);
            if (centerline != kNoId)
                constrainAxis(sketch, c1, c2, centerline);
        });
        return true;
    }
};

// Ellipse: center, the end of one axis, then how far the other one reaches.
class EllipseTool : public ThreeStepTool
{
public:
    using ThreeStepTool::ThreeStepTool;

    void preview(DrawList& out) const override
    {
        if (!m_first)
            return;
        out.accentPoints.push_back(m_first->pos);
        if (!m_second) {
            out.segments.push_back({m_first->pos, m_cursor});
            return;
        }
        const EllipseAxes axes = axesTo(m_cursor);
        std::vector<gp_Pnt2d> pts;
        for (int i = 0; i <= 96; ++i)
            pts.push_back(axes.at(2.0 * M_PI * i / 96));
        appendPolyline(out.segments, pts);
        out.constructionSegments.push_back({axes.center, axes.center.Translated(axes.u)});
        out.constructionSegments.push_back({axes.center, axes.center.Translated(axes.v)});
    }

    QString hint() const override
    {
        if (!m_first)
            return QCoreApplication::translate("SketchTool", "Ellipse: pick the center");
        if (!m_second)
            return QCoreApplication::translate("SketchTool", "Ellipse: pick the end of an axis  ·  %1").arg(mm(m_first->pos.Distance(m_cursor)));
        return QCoreApplication::translate("SketchTool", "Ellipse: pick the other axis  ·  %1 × %2")
            .arg(mm(2.0 * m_first->pos.Distance(m_second->pos)), mm(2.0 * across(m_cursor)));
    }

protected:
    bool finish(const SnapResult& first, const SnapResult& second, const SnapResult& third) override
    {
        const EllipseAxes axes = axesTo(third.pos);
        if (axes.v.Magnitude() < kMinSize)
            return false;
        m_host.commit([&](Sketch& sketch) {
            const PointId c = resolveConstrained(sketch, first);
            const PointId a = resolveConstrained(sketch, second);
            constrainAxis(sketch, c, a, kNoId);
            sketch.addEllipse(c, a, sketch.pointAt(axes.center.Translated(axes.v)));
        });
        return true;
    }

private:
    EllipseAxes axesTo(const gp_Pnt2d& p) const
    {
        const gp_Vec2d u(m_first->pos, m_second->pos);
        return {m_first->pos, u, leftOf(u) * across(p)};
    }
};

// Spline through tapped points. Tapping the first point again closes it, landing on
// other geometry (or Esc / Enter) ends it.
class SplineTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        add(s);
        m_pressStarted = !m_points.empty(); // (not when this tap finished the curve)
    }

    void release(const SnapResult& s, bool dragged) override
    {
        // A drag adds the point where it ends too.
        if (m_pressStarted && dragged)
            add(s);
        m_pressStarted = false;
    }

    // Esc drops the curve in progress (it is called on undo too, when the picked points
    // may be gone); Enter finishes it.
    bool cancel() override
    {
        const bool active = !m_points.empty();
        m_points.clear();
        m_pressStarted = false;
        return active;
    }

    bool confirm() override
    {
        if (m_points.size() < 2)
            return cancel();
        finish(false);
        m_points.clear();
        m_pressStarted = false;
        return true;
    }

    void preview(DrawList& out) const override
    {
        if (m_points.empty())
            return;
        std::vector<gp_Pnt2d> pts;
        for (const SnapResult& p : m_points) {
            pts.push_back(p.pos);
            out.accentPoints.push_back(p.pos);
        }
        const bool closing = closes(m_cursor, kNoId);
        if (!closing)
            pts.push_back(m_cursor);
        appendPolyline(out.segments, sampleSpline(pts, closing));
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        return m_points.empty() ? std::vector<gp_Pnt2d>{} : std::vector<gp_Pnt2d>{m_points.back().pos};
    }

    QString hint() const override
    {
        if (m_points.empty())
            return QCoreApplication::translate("SketchTool", "Spline: tap the points it passes through");
        if (closes(m_cursor, kNoId))
            return QCoreApplication::translate("SketchTool", "Spline: tap to close it");
        return QCoreApplication::translate("SketchTool", "Spline: %1 points  ·  tap the first one to close it  ·  Enter finishes, Esc cancels").arg(m_points.size());
    }

private:
    bool closes(const gp_Pnt2d& p, PointId snapped) const
    {
        if (m_points.size() < 3)
            return false;
        const SnapResult& first = m_points.front();
        return (snapped != kNoId && snapped == first.pointId) || p.Distance(first.pos) <= m_host.pickTolerance();
    }

    void add(const SnapResult& s)
    {
        if (!m_points.empty() && s.pos.Distance(m_points.back().pos) < kMinSize)
            return;
        if (closes(s.pos, s.pointId)) {
            finish(true);
            m_points.clear();
            return;
        }
        m_points.push_back(s);
        // Landing on existing geometry ends the curve there.
        if (m_points.size() >= 2 && s.pointId != kNoId) {
            finish(false);
            m_points.clear();
        }
    }

    void finish(bool closed)
    {
        const std::vector<SnapResult> points = m_points;
        m_host.commit([&](Sketch& sketch) {
            std::vector<PointId> ids;
            for (const SnapResult& p : points) {
                const PointId id = resolveConstrained(sketch, p);
                if (ids.empty() || ids.back() != id)
                    ids.push_back(id);
            }
            if (closed && ids.size() > 1 && ids.front() == ids.back())
                ids.pop_back();
            if (ids.size() >= (closed ? 3u : 2u))
                sketch.addSpline(ids, closed);
        });
    }

    std::vector<SnapResult> m_points;
    bool m_pressStarted = false;
};

// Sketch fillet / chamfer: tap the corner where two lines meet, then type the radius
// (distance). The result is previewed under the pointer with the last value.
class CornerTool : public SketchTool
{
public:
    CornerTool(SketchToolHost& host, bool chamfer)
        : SketchTool(host)
        , m_chamfer(chamfer)
    {
    }

    void press(const SnapResult& s) override
    {
        if (m_editing)
            return;
        m_failed = false;
        const auto corner = cornerAt(s.pos);
        if (!corner) {
            m_missed = true;
            return;
        }
        m_missed = false;
        m_corner = *corner;
        m_editing = true;
        const double max = limit(*corner);
        m_host.askValue([chamfer = m_chamfer] { return chamfer ? QCoreApplication::translate("SketchTool", "Chamfer [mm]") : QCoreApplication::translate("SketchTool", "Radius [mm]"); },
                        std::min(last(), std::round(max * 0.9 * 100.0) / 100.0), 1e-3, max,
                        [this](double value) {
                            m_editing = false;
                            apply(value);
                        },
                        [this] {
                            m_editing = false;
                            m_corner = kNoId;
                        });
    }

    void release(const SnapResult&, bool) override {}

    bool cancel() override
    {
        const bool active = m_corner != kNoId;
        m_corner = kNoId;
        m_editing = m_failed = m_missed = false;
        return active;
    }

    void preview(DrawList& out) const override
    {
        const auto corner = m_corner != kNoId ? std::optional(m_corner) : cornerAt(m_cursor);
        if (!corner)
            return;
        Sketch probe = m_host.sketch();
        EntityId made = kNoId;
        const double value = std::min(last(), limit(*corner) * 0.999);
        const bool ok = m_chamfer ? chamferCorner(probe, *corner, value, &made) : filletCorner(probe, *corner, value, &made);
        if (ok)
            appendEntities(out.segments, probe, std::set<EntityId>{made});
        out.accentPoints.push_back(m_host.sketch().pos(*corner));
    }

    std::vector<gp_Pnt2d> anchors() const override { return {}; }
    bool snaps() const override { return false; }

    QString hint() const override
    {
        if (m_failed)
            return m_chamfer ? QCoreApplication::translate("SketchTool", "Chamfer: too big for these lines") : QCoreApplication::translate("SketchTool", "Fillet: too big for these lines");
        if (m_editing)
            return m_chamfer ? QCoreApplication::translate("SketchTool", "Chamfer: type the distance  ·  Enter accepts, Esc cancels")
                             : QCoreApplication::translate("SketchTool", "Fillet: type the radius  ·  Enter accepts, Esc cancels");
        if (m_missed)
            return m_chamfer ? QCoreApplication::translate("SketchTool", "Chamfer: tap a corner where two lines meet")
                             : QCoreApplication::translate("SketchTool", "Fillet: tap a corner where two lines meet");
        return m_chamfer ? QCoreApplication::translate("SketchTool", "Chamfer: tap the corner of two lines") : QCoreApplication::translate("SketchTool", "Fillet: tap the corner of two lines");
    }

private:
    std::optional<PointId> cornerAt(const gp_Pnt2d& p) const
    {
        const Sketch& sketch = m_host.sketch();
        const auto point = sketch.findPoint(p, m_host.pickTolerance() * 1.5);
        if (!point || !cornerLines(sketch, *point))
            return std::nullopt;
        return point;
    }

    double limit(PointId corner) const
    {
        return m_chamfer ? maxChamferDistance(m_host.sketch(), corner) : maxFilletRadius(m_host.sketch(), corner);
    }

    double last() const { return m_chamfer ? s_distance : s_radius; }

    void apply(double value)
    {
        const PointId corner = m_corner;
        m_corner = kNoId;
        Sketch probe = m_host.sketch();
        if (!(m_chamfer ? chamferCorner(probe, corner, value) : filletCorner(probe, corner, value))) {
            m_failed = true;
            return;
        }
        (m_chamfer ? s_distance : s_radius) = value;
        m_host.commit([&](Sketch& sketch) {
            if (m_chamfer)
                chamferCorner(sketch, corner, value);
            else
                filletCorner(sketch, corner, value);
        });
    }

    inline static double s_radius = 5.0;
    inline static double s_distance = 2.0;
    const bool m_chamfer;
    PointId m_corner = kNoId;
    bool m_editing = false;
    bool m_failed = false;
    bool m_missed = false;
};

// Mirror, linear and circular pattern: work on the curves selected before the tool was
// picked, or tapped first (Enter goes on), then ask where to.
class CopyTool : public SketchTool
{
public:
    enum class Kind { Mirror, Linear, Circular };

    CopyTool(SketchToolHost& host, Kind kind)
        : SketchTool(host)
        , m_kind(kind)
    {
    }

    void begin(const SketchSelection& selection) override
    {
        m_objects = selection.entities;
        m_step = m_objects.empty() ? Step::Objects : Step::Place;
    }

    void press(const SnapResult& s) override
    {
        if (m_editing)
            return;
        const Sketch& sketch = m_host.sketch();
        if (m_step == Step::Objects) {
            if (auto id = sketch.hitTest(s.pos, m_host.pickTolerance())) {
                if (!m_objects.erase(*id))
                    m_objects.insert(*id);
            }
            return;
        }
        switch (m_kind) {
        case Kind::Mirror: {
            const auto axis = axisAt(s.pos);
            if (!axis)
                return;
            const std::set<EntityId> objects = m_objects;
            m_host.commit([&](Sketch& sk) { mirrorEntities(sk, objects, *axis); });
            reset();
            return;
        }
        case Kind::Linear:
            if (!m_base) {
                m_base = s.pos;
                return;
            }
            if (s.pos.Distance(*m_base) < kMinSize)
                return;
            m_target = s.pos;
            askCount();
            return;
        case Kind::Circular:
            m_target = s.pos;
            askCount();
            return;
        }
    }

    void release(const SnapResult&, bool) override {}

    bool confirm() override
    {
        if (m_step == Step::Objects && !m_objects.empty()) {
            m_step = Step::Place;
            return true;
        }
        return cancel();
    }

    bool cancel() override
    {
        if (m_editing)
            return false; // the field is closing: it cancels itself
        if (m_step == Step::Place && m_base) {
            m_base.reset();
            return true;
        }
        if (m_step == Step::Place) {
            m_step = Step::Objects;
            return true;
        }
        if (!m_objects.empty()) {
            m_objects.clear();
            return true;
        }
        return false;
    }

    void preview(DrawList& out) const override
    {
        const Sketch& sketch = m_host.sketch();
        appendEntities(out.accentSegments, sketch, m_objects);
        if (m_step == Step::Objects) {
            if (auto id = sketch.hitTest(m_cursor, m_host.pickTolerance()); id && !m_objects.count(*id))
                appendEntities(out.segments, sketch, std::set<EntityId>{*id});
            return;
        }
        Sketch probe = sketch;
        std::vector<EntityId> copies;
        switch (m_kind) {
        case Kind::Mirror:
            if (const auto axis = axisAt(m_cursor)) {
                copies = mirrorEntities(probe, m_objects, *axis);
                appendEntities(out.accentSegments, sketch, std::set<EntityId>{*axis});
            }
            break;
        case Kind::Linear: {
            const gp_Pnt2d target = m_editing ? m_target : m_cursor;
            if (m_base) {
                out.segments.push_back({*m_base, target});
                copies = linearPattern(probe, m_objects, gp_Vec2d(*m_base, target), s_linear);
            }
            break;
        }
        case Kind::Circular: {
            const gp_Pnt2d center = m_editing ? m_target : m_cursor;
            out.accentPoints.push_back(center);
            copies = circularPattern(probe, m_objects, center, s_circular);
            break;
        }
        }
        appendEntities(out.segments, probe, copies);
    }

    std::vector<gp_Pnt2d> anchors() const override
    {
        return m_base ? std::vector<gp_Pnt2d>{*m_base} : std::vector<gp_Pnt2d>{};
    }
    bool snaps() const override { return m_step == Step::Place && m_kind != Kind::Mirror; }

    QString hint() const override
    {
        const QString name = m_kind == Kind::Mirror   ? QCoreApplication::translate("SketchTool", "Mirror")
                           : m_kind == Kind::Linear ? QCoreApplication::translate("SketchTool", "Linear pattern")
                                                    : QCoreApplication::translate("SketchTool", "Circular pattern");
        if (m_step == Step::Objects) {
            if (m_objects.empty())
                return QCoreApplication::translate("SketchTool", "%1: tap the curves to copy").arg(name);
            return QCoreApplication::translate("SketchTool", "%1: %2 curves  ·  tap more, or Enter to go on").arg(name).arg(m_objects.size());
        }
        if (m_editing)
            return QCoreApplication::translate("SketchTool", "%1: type how many in all (with the original)  ·  Enter accepts").arg(name);
        switch (m_kind) {
        case Kind::Mirror:
            return QCoreApplication::translate("SketchTool", "Mirror: tap the line to mirror about");
        case Kind::Linear:
            return m_base ? QCoreApplication::translate("SketchTool", "Linear pattern: pick where the first copy goes  ·  %1").arg(mm(m_base->Distance(m_cursor)))
                          : QCoreApplication::translate("SketchTool", "Linear pattern: pick a point to measure the step from");
        case Kind::Circular:
            return QCoreApplication::translate("SketchTool", "Circular pattern: pick the center");
        }
        return {};
    }

private:
    enum class Step { Objects, Place };

    std::optional<EntityId> axisAt(const gp_Pnt2d& p) const
    {
        const Sketch& sketch = m_host.sketch();
        const auto id = sketch.hitTest(p, m_host.pickTolerance());
        if (!id || sketch.entity(*id)->kind != EntityKind::Line)
            return std::nullopt;
        return id;
    }

    void askCount()
    {
        m_editing = true;
        int& count = m_kind == Kind::Linear ? s_linear : s_circular;
        m_host.askValue([] { return QCoreApplication::translate("SketchTool", "Count"); }, count, 2, 360,
                        [this, &count](double value) {
                            m_editing = false;
                            count = static_cast<int>(std::lround(value));
                            apply();
                        },
                        [this] { m_editing = false; });
    }

    void apply()
    {
        const std::set<EntityId> objects = m_objects;
        const gp_Pnt2d target = m_target;
        const std::optional<gp_Pnt2d> base = m_base;
        m_host.commit([&](Sketch& sketch) {
            if (m_kind == Kind::Linear && base)
                linearPattern(sketch, objects, gp_Vec2d(*base, target), s_linear);
            else if (m_kind == Kind::Circular)
                circularPattern(sketch, objects, target, s_circular);
        });
        reset();
    }

    void reset()
    {
        m_objects.clear();
        m_base.reset();
        m_step = Step::Objects;
    }

    inline static int s_linear = 3;
    inline static int s_circular = 6;
    const Kind m_kind;
    Step m_step = Step::Objects;
    std::set<EntityId> m_objects;
    std::optional<gp_Pnt2d> m_base; // linear: where the step is measured from
    gp_Pnt2d m_target;              // linear: where the first copy goes; circular: the center
    bool m_editing = false;
};

// The reference image: drag it to move it; tap two points on it a known distance apart
// and type that distance to scale it.
class ImageTool : public SketchTool
{
public:
    using SketchTool::SketchTool;

    void press(const SnapResult& s) override
    {
        if (m_editing)
            return;
        m_down = true;
        m_moving = m_moved = false;
        m_pressAt = s.pos;
        const auto& image = m_host.sketch().image();
        m_canMove = image && inside(*image, s.pos);
        if (image)
            m_cornerAtPress = image->corner;
    }

    void hover(const SnapResult& s) override
    {
        SketchTool::hover(s);
        if (!m_down || !m_canMove)
            return;
        if (!m_moving && s.pos.Distance(m_pressAt) > m_host.pickTolerance())
            m_moving = true;
        if (!m_moving)
            return;
        const gp_Vec2d delta(m_pressAt, s.pos);
        const gp_Pnt2d corner = m_cornerAtPress.Translated(delta);
        m_host.commit(
            [&](Sketch& sketch) {
                if (auto image = sketch.image()) {
                    image->corner = corner;
                    sketch.setImage(image);
                }
            },
            m_moved); // the whole drag is one undo step
        m_moved = true;
    }

    void release(const SnapResult& s, bool dragged) override
    {
        const bool moved = m_moving;
        m_down = m_moving = m_moved = false;
        if (moved || m_editing || !m_host.sketch().image())
            return;
        if (!m_first) {
            m_first = s.pos;
            return;
        }
        const double measured = m_first->Distance(s.pos);
        if (measured < kMinSize)
            return;
        m_second = s.pos;
        m_editing = true;
        m_host.askValue([] { return QCoreApplication::translate("SketchTool", "Distance [mm]"); }, std::round(measured * 100.0) / 100.0, 1e-3, 1e6,
                        [this, measured](double value) {
                            m_editing = false;
                            scale(value / measured);
                        },
                        [this] {
                            m_editing = false;
                            m_first.reset();
                        });
    }

    bool cancel() override
    {
        const bool active = m_first.has_value();
        m_first.reset();
        m_down = m_moving = m_moved = m_editing = false;
        return active;
    }

    void preview(DrawList& out) const override
    {
        const auto& image = m_host.sketch().image();
        if (!image)
            return;
        const gp_Pnt2d a = image->corner;
        const gp_Pnt2d b(a.X() + image->width, a.Y());
        const gp_Pnt2d c(a.X() + image->width, a.Y() + image->height());
        const gp_Pnt2d d(a.X(), a.Y() + image->height());
        out.constructionSegments.insert(out.constructionSegments.end(), {{a, b}, {b, c}, {c, d}, {d, a}});
        if (m_first) {
            const gp_Pnt2d to = m_editing ? m_second : m_cursor;
            out.accentSegments.push_back({*m_first, to});
            out.accentPoints.push_back(*m_first);
            out.accentPoints.push_back(to);
        }
    }

    std::vector<gp_Pnt2d> anchors() const override { return {}; }
    bool snaps() const override { return false; }

    QString hint() const override
    {
        if (!m_host.sketch().image())
            return QCoreApplication::translate("SketchTool", "Image: insert a picture first (Image → Insert Image…)");
        if (m_editing)
            return QCoreApplication::translate("SketchTool", "Image: type the real distance between the two points  ·  Enter accepts");
        if (m_first)
            return QCoreApplication::translate("SketchTool", "Image: tap the second point  ·  %1 now").arg(mm(m_first->Distance(m_cursor)));
        return QCoreApplication::translate("SketchTool", "Image: drag it to move it  ·  tap two points a known distance apart to scale it");
    }

private:
    static bool inside(const ReferenceImage& image, const gp_Pnt2d& p)
    {
        const gp_Pnt2d& c = image.corner;
        return p.X() >= c.X() && p.Y() >= c.Y() && p.X() <= c.X() + image.width && p.Y() <= c.Y() + image.height();
    }

    void scale(double factor)
    {
        const gp_Pnt2d about = *m_first;
        m_first.reset();
        m_host.commit([&](Sketch& sketch) {
            if (auto image = sketch.image()) {
                image->corner = about.Translated(gp_Vec2d(about, image->corner) * factor);
                image->width *= factor;
                sketch.setImage(image);
            }
        });
    }

    bool m_down = false;
    bool m_canMove = false;
    bool m_moving = false;
    bool m_moved = false; // the drag has made its undo step
    bool m_editing = false;
    gp_Pnt2d m_pressAt;
    gp_Pnt2d m_cornerAtPress;
    std::optional<gp_Pnt2d> m_first;
    gp_Pnt2d m_second;
};
} // namespace

PointId SketchTool::resolveConstrained(Sketch& sketch, const SnapResult& s)
{
    const PointId id = resolve(sketch, s);
    SketchConstraint c;
    c.p1 = id;
    switch (s.kind) {
    case SnapResult::Kind::Origin:
        c.kind = ConstraintKind::Fixed;
        c.target = gp_Pnt2d(0.0, 0.0);
        sketch.addConstraint(c);
        break;
    case SnapResult::Kind::Midpoint:
        c.kind = ConstraintKind::Midpoint;
        c.e1 = s.entity;
        if (sketch.entity(s.entity))
            sketch.addConstraint(c);
        break;
    default:
        break;
    }
    return id;
}

void SketchTool::constrainAxis(Sketch& sketch, PointId a, PointId b, EntityId line)
{
    const gp_Pnt2d pa = sketch.pos(a);
    const gp_Pnt2d pb = sketch.pos(b);
    SketchConstraint c;
    c.p1 = a;
    c.p2 = b;
    c.e1 = line;
    if (pa.Y() == pb.Y())
        c.kind = ConstraintKind::Horizontal;
    else if (pa.X() == pb.X())
        c.kind = ConstraintKind::Vertical;
    else
        return;
    sketch.addConstraint(c);
}

std::unique_ptr<SketchTool> makeTool(ToolKind kind, SketchToolHost& host)
{
    switch (kind) {
    case ToolKind::Line:
        return std::make_unique<LineTool>(host);
    case ToolKind::Rectangle:
        return std::make_unique<RectangleTool>(host);
    case ToolKind::Circle:
        return std::make_unique<CircleTool>(host);
    case ToolKind::Arc:
        return std::make_unique<ArcTool>(host);
    case ToolKind::Trim:
        return std::make_unique<TrimTool>(host);
    case ToolKind::Offset:
        return std::make_unique<OffsetTool>(host);
    case ToolKind::Polygon:
        return std::make_unique<PolygonTool>(host);
    case ToolKind::Slot:
        return std::make_unique<SlotTool>(host);
    case ToolKind::Ellipse:
        return std::make_unique<EllipseTool>(host);
    case ToolKind::Spline:
        return std::make_unique<SplineTool>(host);
    case ToolKind::SketchFillet:
        return std::make_unique<CornerTool>(host, false);
    case ToolKind::SketchChamfer:
        return std::make_unique<CornerTool>(host, true);
    case ToolKind::Mirror:
        return std::make_unique<CopyTool>(host, CopyTool::Kind::Mirror);
    case ToolKind::LinearPattern:
        return std::make_unique<CopyTool>(host, CopyTool::Kind::Linear);
    case ToolKind::CircularPattern:
        return std::make_unique<CopyTool>(host, CopyTool::Kind::Circular);
    case ToolKind::Image:
        return std::make_unique<ImageTool>(host);
    case ToolKind::Project:
    case ToolKind::Freehand:
    case ToolKind::Select:
    case ToolKind::Dimension:
    case ToolKind::PushPull:
    case ToolKind::Fillet:
    case ToolKind::Chamfer:
    case ToolKind::Shell:
        break;
    default:
        break; // solid tools: the editor's
    }
    return nullptr;
}
