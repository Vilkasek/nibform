#include "sketch/SketchTools.h"

#include <cmath>

namespace {

constexpr double kMinSize = 1e-6;

QString mm(double v)
{
    return QString::number(v, 'f', 2) + QStringLiteral(" mm");
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
            return QStringLiteral("Linia: wskaż początek");
        const double dx = m_cursor.X() - m_start->pos.X();
        const double dy = m_cursor.Y() - m_start->pos.Y();
        const double angle = std::atan2(dy, dx) * 180.0 / M_PI;
        return QStringLiteral("Linia: %1, kąt %2°  ·  Esc kończy")
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
            const PointId a = resolve(sketch, start);
            endId = resolve(sketch, end);
            sketch.addLine(a, endId);
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
            return QStringLiteral("Prostokąt: wskaż pierwszy narożnik");
        return QStringLiteral("Prostokąt: %1 × %2")
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
            const PointId p1 = resolve(sketch, first);
            const PointId p2 = sketch.pointAt(gp_Pnt2d(c.X(), a.Y()));
            const PointId p3 = resolve(sketch, second);
            const PointId p4 = sketch.pointAt(gp_Pnt2d(a.X(), c.Y()));
            sketch.addLine(p1, p2);
            sketch.addLine(p2, p3);
            sketch.addLine(p3, p4);
            sketch.addLine(p4, p1);
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
            return QStringLiteral("Okrąg: wskaż środek");
        const double r = m_first->pos.Distance(m_cursor);
        return QStringLiteral("Okrąg: promień %1, średnica %2").arg(mm(r), mm(2.0 * r));
    }

protected:
    void finish(const SnapResult& center, const SnapResult& rim) override
    {
        const double r = center.pos.Distance(rim.pos);
        if (r < kMinSize)
            return;
        m_host.commit([&](Sketch& sketch) { sketch.addCircle(resolve(sketch, center), r); });
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
            return QStringLiteral("Łuk: wskaż początek");
        if (!m_end)
            return QStringLiteral("Łuk: wskaż koniec");
        if (auto c = circumcenter(m_start->pos, m_end->pos, m_cursor))
            return QStringLiteral("Łuk: promień %1").arg(mm(c->Distance(m_start->pos)));
        return QStringLiteral("Łuk: wskaż punkt na łuku");
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
            const PointId a = resolve(sketch, start);
            const PointId b = resolve(sketch, end);
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

} // namespace

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
    case ToolKind::Select:
        break;
    }
    return nullptr;
}
