#include "viewer/Glyphs.h"

#include <QPainter>

void drawConstraintGlyph(QPainter& p, ConstraintKind kind, const QRectF& r, const QColor& ink)
{
    const QPointF c = r.center();
    const double s = r.width() * 0.28;
    auto at = [&](double x, double y) { return c + QPointF(x * s, y * s); };
    QPen pen(ink, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case ConstraintKind::Horizontal:
        p.drawLine(at(-1, 0), at(1, 0));
        break;
    case ConstraintKind::Vertical:
        p.drawLine(at(0, -1), at(0, 1));
        break;
    case ConstraintKind::Parallel:
        p.drawLine(at(-0.8, 1), at(-0.1, -1));
        p.drawLine(at(0.1, 1), at(0.8, -1));
        break;
    case ConstraintKind::Perpendicular:
        p.drawLine(at(-1, 0.9), at(1, 0.9));
        p.drawLine(at(0, 0.9), at(0, -1));
        break;
    case ConstraintKind::Equal:
        p.drawLine(at(-1, -0.4), at(1, -0.4));
        p.drawLine(at(-1, 0.4), at(1, 0.4));
        break;
    case ConstraintKind::Tangent:
        p.drawEllipse(at(0, -0.3), 0.65 * s, 0.65 * s);
        p.drawLine(at(-1, 0.35), at(1, 0.35));
        break;
    case ConstraintKind::Concentric:
        p.drawEllipse(c, s, s);
        p.drawEllipse(c, 0.45 * s, 0.45 * s);
        break;
    case ConstraintKind::PointOnEntity:
        p.drawArc(QRectF(at(-1, -0.2), at(1, 1.8)), 30 * 16, 120 * 16);
        p.setBrush(ink);
        p.drawEllipse(at(0, -0.2), 0.3 * s, 0.3 * s);
        break;
    case ConstraintKind::Midpoint:
        p.drawLine(at(-1, 0), at(1, 0));
        p.drawLine(at(-1, -0.5), at(-1, 0.5));
        p.drawLine(at(1, -0.5), at(1, 0.5));
        p.setBrush(ink);
        p.drawEllipse(c, 0.3 * s, 0.3 * s);
        break;
    case ConstraintKind::Fixed:
        p.drawArc(QRectF(at(-0.5, -1), at(0.5, 0.2)), 0, 180 * 16);
        p.setBrush(ink);
        p.drawRoundedRect(QRectF(at(-0.8, -0.4), at(0.8, 0.9)), 1.5, 1.5);
        break;
    case ConstraintKind::Symmetric: {
        QPen dashed = pen;
        dashed.setDashPattern({1.5, 1.5});
        p.setPen(dashed);
        p.drawLine(at(0, -1.1), at(0, 1.1));
        p.setPen(pen);
        p.drawLine(at(-1, -0.6), at(-0.35, 0));
        p.drawLine(at(-0.35, 0), at(-1, 0.6));
        p.drawLine(at(1, -0.6), at(0.35, 0));
        p.drawLine(at(0.35, 0), at(1, 0.6));
        break;
    }
    default:
        break;
    }
}
