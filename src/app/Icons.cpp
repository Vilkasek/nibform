#include "app/Icons.h"

#include "viewer/Glyphs.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QPixmap>

#include <functional>
#include <tuple>

namespace {

constexpr int kSize = 24; // logical pixels; drawn on a 24×24 grid

QPixmap render(const std::function<void(QPainter&)>& draw)
{
    const double dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(kSize, kSize) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p);
    return pixmap;
}

QIcon both(const std::function<void(QPainter&, const QColor&, const QColor&)>& draw, const QColor& ink,
           const QColor& accent)
{
    QColor faded = ink;
    faded.setAlphaF(0.35);
    QIcon icon;
    icon.addPixmap(render([&](QPainter& p) { draw(p, ink, accent); }), QIcon::Normal);
    icon.addPixmap(render([&](QPainter& p) { draw(p, faded, faded); }), QIcon::Disabled);
    return icon;
}

QPen pen(const QColor& c, double width = 1.8)
{
    QPen p(c, width);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

void dot(QPainter& p, const QPointF& at, const QColor& c, double r = 2.0)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(at, r, r);
    p.setBrush(Qt::NoBrush);
}

void arrowHead(QPainter& p, const QPointF& tip, const QPointF& from, const QColor& c)
{
    const QPointF d = (tip - from) / QLineF(from, tip).length();
    const QPointF n(-d.y(), d.x());
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(tip - d * 5.0 + n * 3.5);
    head.lineTo(tip - d * 5.0 - n * 3.5);
    head.closeSubpath();
    p.fillPath(head, c);
}

// Front square, top and right faces; `fill` picks one face to highlight (0: none,
// 1: top, 2: front, 3: right).
void cube(QPainter& p, const QColor& ink, const QColor& accent, int fill)
{
    const QPolygonF front({{4, 9}, {15, 9}, {15, 20}, {4, 20}});
    const QPolygonF top({{4, 9}, {9, 4}, {20, 4}, {15, 9}});
    const QPolygonF right({{15, 9}, {20, 4}, {20, 15}, {15, 20}});
    QColor soft = accent;
    soft.setAlphaF(0.55);
    p.setPen(Qt::NoPen);
    p.setBrush(soft);
    if (fill == 1)
        p.drawPolygon(top);
    if (fill == 2)
        p.drawPolygon(front);
    if (fill == 3)
        p.drawPolygon(right);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen(ink, 1.5));
    p.drawPolygon(front);
    p.drawPolygon(top);
    p.drawPolygon(right);
}

void draw(QPainter& p, Icon icon, const QColor& ink, const QColor& accent)
{
    p.setPen(pen(ink));
    switch (icon) {
    case Icon::Select: {
        const QPolygonF cursor({{7, 3}, {7, 19}, {11, 15}, {14, 21}, {16.5, 20}, {13.5, 14}, {19, 14}});
        QColor soft = accent;
        soft.setAlphaF(0.35);
        p.setBrush(soft);
        p.drawPolygon(cursor);
        break;
    }
    case Icon::Line:
        p.drawLine(QPointF(5, 19), QPointF(19, 5));
        dot(p, {5, 19}, accent, 2.4);
        dot(p, {19, 5}, accent, 2.4);
        break;
    case Icon::Rectangle:
        p.drawRect(QRectF(4.5, 7, 15, 10));
        dot(p, {4.5, 7}, accent);
        dot(p, {19.5, 17}, accent);
        break;
    case Icon::Circle:
        p.drawEllipse(QPointF(12, 12), 7.5, 7.5);
        dot(p, {12, 12}, accent);
        break;
    case Icon::Arc: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4, 6, 16, 16), 180);
        arc.arcTo(QRectF(4, 6, 16, 16), 180, -180);
        p.drawPath(arc);
        dot(p, {4, 14}, accent, 2.4);
        dot(p, {20, 14}, accent, 2.4);
        break;
    }
    case Icon::Trim: {
        p.drawLine(QPointF(14, 3), QPointF(14, 21));
        p.drawLine(QPointF(3, 12), QPointF(14, 12));
        QPen dashed = pen(accent);
        dashed.setDashPattern({1.5, 2.0});
        p.setPen(dashed);
        p.drawLine(QPointF(14, 12), QPointF(21, 12));
        p.setPen(pen(accent, 1.6));
        p.drawLine(QPointF(16.5, 15), QPointF(20.5, 19));
        p.drawLine(QPointF(20.5, 15), QPointF(16.5, 19));
        break;
    }
    case Icon::Offset: {
        QPainterPath inner;
        inner.moveTo(7, 20);
        inner.lineTo(7, 12);
        inner.quadTo(7, 7, 12, 7);
        inner.lineTo(20, 7);
        p.drawPath(inner);
        QPainterPath outer;
        outer.moveTo(3, 20);
        outer.lineTo(3, 12);
        outer.quadTo(3, 3, 12, 3);
        outer.lineTo(20, 3);
        QPen dashed = pen(accent);
        dashed.setDashPattern({2.0, 1.8});
        p.setPen(dashed);
        p.drawPath(outer);
        break;
    }
    case Icon::NewSketch:
    case Icon::EditSketch: {
        p.drawPolygon(QPolygonF({{2, 19}, {14, 19}, {21, 13}, {9, 13}}));
        p.setPen(pen(accent, 2.0));
        if (icon == Icon::NewSketch) {
            p.drawLine(QPointF(17, 2.5), QPointF(17, 9.5));
            p.drawLine(QPointF(13.5, 6), QPointF(20.5, 6));
        } else {
            p.drawLine(QPointF(9, 16), QPointF(19, 4)); // a pencil stroke on the plane
            dot(p, {9, 16}, accent, 1.6);
        }
        break;
    }
    case Icon::CloseSketch:
        p.setPen(pen(accent, 2.6));
        p.drawPolyline(QPolygonF({{5, 12.5}, {10, 17.5}, {19.5, 6.5}}));
        break;
    case Icon::Extrude: {
        p.drawPolygon(QPolygonF({{3, 18}, {13, 18}, {21, 14}, {11, 14}}));
        p.setPen(pen(accent, 2.0));
        p.drawLine(QPointF(12, 15), QPointF(12, 5));
        arrowHead(p, {12, 2}, {12, 6}, accent);
        break;
    }
    case Icon::Fillet:
    case Icon::Chamfer: {
        p.drawLine(QPointF(5, 21), QPointF(5, 12));
        p.drawLine(QPointF(12, 5), QPointF(21, 5));
        p.setPen(pen(accent, 2.2));
        if (icon == Icon::Fillet) {
            QPainterPath round;
            round.moveTo(5, 12);
            round.quadTo(5, 5, 12, 5);
            p.drawPath(round);
        } else {
            p.drawLine(QPointF(5, 12), QPointF(12, 5));
        }
        // The corner that goes away.
        QPen ghost = pen(ink, 1.0);
        ghost.setDashPattern({1.5, 2.0});
        p.setPen(ghost);
        p.drawPolyline(QPolygonF({{5, 12}, {5, 5}, {12, 5}}));
        break;
    }
    case Icon::Fit:
        for (const auto& [x, y, dx, dy] : {std::tuple(4, 4, 1, 1), std::tuple(20, 4, -1, 1),
                                           std::tuple(20, 20, -1, -1), std::tuple(4, 20, 1, -1)}) {
            p.drawLine(QPointF(x, y), QPointF(x + 5 * dx, y));
            p.drawLine(QPointF(x, y), QPointF(x, y + 5 * dy));
        }
        p.drawRect(QRectF(9, 9, 6, 6));
        break;
    case Icon::Iso:
        cube(p, ink, accent, 0);
        break;
    case Icon::Top:
        cube(p, ink, accent, 1);
        break;
    case Icon::Front:
        cube(p, ink, accent, 2);
        break;
    case Icon::Right:
        cube(p, ink, accent, 3);
        break;
    case Icon::Dimension:
        p.drawLine(QPointF(4, 7), QPointF(4, 19));
        p.drawLine(QPointF(20, 7), QPointF(20, 19));
        p.setPen(pen(accent));
        p.drawLine(QPointF(6, 13), QPointF(18, 13));
        arrowHead(p, {4.5, 13}, {10, 13}, accent);
        arrowHead(p, {19.5, 13}, {14, 13}, accent);
        break;
    case Icon::Coincident:
        p.drawEllipse(QPointF(8.5, 12), 4.5, 4.5);
        p.drawEllipse(QPointF(15.5, 12), 4.5, 4.5);
        dot(p, {12, 12}, accent, 2.2);
        break;
    }
}

} // namespace

QIcon makeIcon(Icon icon, const QColor& ink, const QColor& accent)
{
    return both([icon](QPainter& p, const QColor& i, const QColor& a) { draw(p, icon, i, a); }, ink, accent);
}

QIcon makeConstraintIcon(ConstraintKind kind, const QColor& ink)
{
    return both(
        [kind](QPainter& p, const QColor& i, const QColor&) {
            drawConstraintGlyph(p, kind, QRectF(2, 2, kSize - 4, kSize - 4), i);
        },
        ink, ink);
}
