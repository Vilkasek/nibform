#include "sketch/SketchTools.h"

#include "sketch/Offset.h"
#include "sketch/Trim.h"

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
        return QStringLiteral("Przytnij: dotknij fragmentu, by go usunąć  ·  przeciągnij przez kilka naraz");
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
            return QStringLiteral("Odsunięcie: dotknij krzywej (weźmie cały połączony łańcuch)");
        if (m_failed)
            return QStringLiteral("Odsunięcie: za duże – krzywe by się wywinęły  ·  Esc anuluje");
        if (m_editing)
            return QStringLiteral("Odsunięcie: wpisz odległość  ·  Enter zatwierdza, Esc anuluje");
        return QStringLiteral("Odsunięcie: %1  ·  dotknij, by wpisać dokładną wartość  ·  Esc anuluje")
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
            QStringLiteral("Odsunięcie [mm]"), std::round(std::abs(m_frozen) * 100.0) / 100.0, 1e-3, 1e6,
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
    case ToolKind::Select:
        break;
    }
    return nullptr;
}
