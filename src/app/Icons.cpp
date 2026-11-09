#include "app/Icons.h"

#include "viewer/Glyphs.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QLineF>
#include <QPixmap>

#include <cmath>
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
    case Icon::Shell: {
        // An open box: outer walls, the hollow inside in the accent color.
        p.drawPolyline(QPolygonF({{4, 7}, {4, 20}, {20, 20}, {20, 7}}));
        QPen inner = pen(accent, 1.6);
        inner.setDashPattern({2.0, 1.6});
        p.setPen(inner);
        p.drawPolyline(QPolygonF({{7.5, 7}, {7.5, 16.5}, {16.5, 16.5}, {16.5, 7}}));
        p.setPen(pen(ink, 1.2));
        p.drawLine(QPointF(4, 7), QPointF(7.5, 7));
        p.drawLine(QPointF(16.5, 7), QPointF(20, 7));
        break;
    }
    case Icon::Construction: {
        QPen dashed = pen(accent);
        dashed.setDashPattern({2.2, 2.0});
        p.setPen(dashed);
        p.drawLine(QPointF(4, 19), QPointF(20, 5));
        dot(p, {4, 19}, ink, 2.0);
        dot(p, {20, 5}, ink, 2.0);
        break;
    }
    case Icon::Undo:
    case Icon::Redo: {
        // A curved arrow turning back (undo) or forward (redo).
        const bool back = icon == Icon::Undo;
        QPainterPath arc;
        arc.moveTo(back ? 19 : 5, 18);
        arc.cubicTo(back ? 19 : 5, 9, back ? 13 : 11, 7, back ? 6 : 18, 9);
        p.drawPath(arc);
        p.setPen(pen(accent, 2.0));
        arrowHead(p, {back ? 4.0 : 20.0, 9.5}, {back ? 10.0 : 14.0, 8.0}, accent);
        break;
    }
    case Icon::Delete:
        p.drawPolyline(QPolygonF({{6, 7}, {7.5, 21}, {16.5, 21}, {18, 7}}));
        p.drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
        p.drawLine(QPointF(10, 6.5), QPointF(10.5, 3.5));
        p.drawLine(QPointF(14, 6.5), QPointF(13.5, 3.5));
        p.drawLine(QPointF(10.5, 3.5), QPointF(13.5, 3.5));
        p.setPen(pen(accent, 1.6));
        p.drawLine(QPointF(10, 10), QPointF(10.5, 18));
        p.drawLine(QPointF(14, 10), QPointF(13.5, 18));
        break;
    case Icon::Cancel:
        p.setPen(pen(accent, 2.4));
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    case Icon::Done:
        p.setPen(pen(accent, 2.6));
        p.drawPolyline(QPolygonF({{5, 12.5}, {10, 17.5}, {19.5, 6.5}}));
        break;
    case Icon::Coincident:
        p.drawEllipse(QPointF(8.5, 12), 4.5, 4.5);
        p.drawEllipse(QPointF(15.5, 12), 4.5, 4.5);
        dot(p, {12, 12}, accent, 2.2);
        break;
    case Icon::Polygon: {
        QPolygonF hexagon;
        for (int k = 0; k < 6; ++k)
            hexagon << QPointF(12 + 8.5 * std::cos(M_PI / 3 * k), 12 + 8.5 * std::sin(M_PI / 3 * k));
        p.drawPolygon(hexagon);
        dot(p, {12, 12}, accent);
        dot(p, hexagon[0], accent);
        break;
    }
    case Icon::Slot: {
        QPainterPath slot;
        slot.addRoundedRect(QRectF(3, 8, 18, 8), 4, 4);
        p.drawPath(slot);
        dot(p, {7, 12}, accent);
        dot(p, {17, 12}, accent);
        break;
    }
    case Icon::Ellipse:
        p.drawEllipse(QPointF(12, 12), 9, 5.5);
        dot(p, {12, 12}, accent);
        dot(p, {21, 12}, accent);
        break;
    case Icon::Spline: {
        QPainterPath curve;
        curve.moveTo(3, 17);
        curve.cubicTo(7, 3, 13, 21, 21, 7);
        p.drawPath(curve);
        dot(p, {3, 17}, accent);
        dot(p, {12, 12}, accent);
        dot(p, {21, 7}, accent);
        break;
    }
    case Icon::SketchFillet:
    case Icon::SketchChamfer: {
        p.drawLine(QPointF(4, 20), QPointF(4, 12));
        p.drawLine(QPointF(12, 4), QPointF(20, 4));
        p.setPen(pen(accent, 2.2));
        if (icon == Icon::SketchFillet) {
            QPainterPath round;
            round.moveTo(4, 12);
            round.arcTo(QRectF(4, 4, 16, 16), 180, -90);
            p.drawPath(round);
        } else {
            p.drawLine(QPointF(4, 12), QPointF(12, 4));
        }
        break;
    }
    case Icon::Mirror: {
        QPen axis = pen(ink, 1.3);
        axis.setDashPattern({2.5, 2});
        p.setPen(axis);
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.setPen(pen(ink));
        p.drawPolygon(QPolygonF({{9, 6}, {9, 18}, {3, 18}}));
        p.setPen(pen(accent));
        p.drawPolygon(QPolygonF({{15, 6}, {15, 18}, {21, 18}}));
        break;
    }
    case Icon::LinearPattern:
        p.drawEllipse(QPointF(5.5, 12), 3, 3);
        p.setPen(pen(accent));
        p.drawEllipse(QPointF(12, 12), 3, 3);
        p.drawEllipse(QPointF(18.5, 12), 3, 3);
        break;
    case Icon::CircularPattern: {
        p.setPen(pen(ink, 1.2));
        p.drawEllipse(QPointF(12, 12), 7, 7);
        for (int k = 0; k < 6; ++k) {
            const QPointF at(12 + 7 * std::cos(M_PI / 3 * k), 12 + 7 * std::sin(M_PI / 3 * k));
            p.setPen(Qt::NoPen);
            p.setBrush(k == 0 ? ink : accent);
            p.drawEllipse(at, 2.4, 2.4);
        }
        p.setBrush(Qt::NoBrush);
        break;
    }
    case Icon::Project: {
        cube(p, ink, accent, 0);
        p.setPen(pen(accent, 2.0));
        p.drawLine(QPointF(4, 22), QPointF(15, 22));
        QPen guide = pen(accent, 1.0);
        guide.setDashPattern({2, 2});
        p.setPen(guide);
        p.drawLine(QPointF(4, 20), QPointF(4, 22));
        p.drawLine(QPointF(15, 20), QPointF(15, 22));
        break;
    }
    case Icon::Image: {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        p.setPen(pen(accent));
        p.drawPolyline(QPolygonF({{5, 17}, {10, 11}, {13, 14}, {16, 10}, {19, 15}}));
        dot(p, {16.5, 8.5}, accent, 1.6);
        break;
    }
    case Icon::HorizontalDimension:
    case Icon::VerticalDimension: {
        p.save();
        if (icon == Icon::VerticalDimension) {
            p.translate(12, 12);
            p.rotate(-90);
            p.translate(-12, -12);
        }
        p.setPen(pen(ink, 1.4));
        p.drawLine(QPointF(4, 19), QPointF(4, 6));
        p.drawLine(QPointF(20, 19), QPointF(20, 6));
        p.setPen(pen(accent, 1.6));
        p.drawLine(QPointF(5, 9), QPointF(19, 9));
        arrowHead(p, {5, 9}, {19, 9}, accent);
        arrowHead(p, {19, 9}, {5, 9}, accent);
        dot(p, {4, 19}, ink);
        dot(p, {20, 15}, ink);
        p.restore();
        break;
    }
    case Icon::Revolve: {
        // A profile beside its axis, and the turn it makes.
        p.drawRect(QRectF(11, 8, 7, 11));
        p.setPen(QPen(accent, 1.6, Qt::DashLine));
        p.drawLine(QPointF(6, 3), QPointF(6, 21));
        p.setPen(pen(accent, 1.8));
        QPainterPath turn;
        turn.arcMoveTo(QRectF(1, 2, 18, 6), 200);
        turn.arcTo(QRectF(1, 2, 18, 6), 200, 250);
        p.drawPath(turn);
        arrowHead(p, {17, 3.4}, {13, 2.2}, accent);
        break;
    }
    case Icon::Sweep: {
        p.setPen(pen(accent, 1.8));
        QPainterPath path;
        path.moveTo(5, 19);
        path.cubicTo(5, 8, 12, 6, 19, 6);
        p.drawPath(path);
        p.setPen(pen(ink, 1.6));
        p.drawEllipse(QPointF(5, 19), 2.6, 2.6);
        p.drawEllipse(QPointF(19, 6), 2.6, 2.6);
        break;
    }
    case Icon::Loft:
        p.drawRect(QRectF(4, 15, 16, 5));
        p.drawEllipse(QRectF(8, 3, 8, 4));
        p.setPen(pen(accent, 1.6));
        p.drawLine(QPointF(4, 15), QPointF(8, 5));
        p.drawLine(QPointF(20, 15), QPointF(16, 5));
        break;
    case Icon::Hole:
        p.drawPolygon(QPolygonF({{3, 10}, {14, 6}, {21, 10}, {10, 15}}));
        p.drawLine(QPointF(3, 10), QPointF(3, 15));
        p.drawLine(QPointF(10, 15), QPointF(10, 20));
        p.drawLine(QPointF(21, 10), QPointF(21, 15));
        p.drawLine(QPointF(3, 15), QPointF(10, 20));
        p.drawLine(QPointF(10, 20), QPointF(21, 15));
        p.setPen(pen(accent, 1.8));
        p.drawEllipse(QPointF(12, 10.4), 3.4, 1.8);
        break;
    case Icon::Plane:
        p.drawPolygon(QPolygonF({{3, 15}, {12, 19}, {21, 9}, {12, 5}}));
        dot(p, {12, 12}, accent, 2.2);
        break;
    case Icon::Axis:
        p.drawPolygon(QPolygonF({{3, 17}, {11, 20}, {17, 14}, {9, 11}}));
        p.setPen(QPen(accent, 1.8, Qt::DashLine));
        p.drawLine(QPointF(10, 22), QPointF(14, 2));
        break;
    case Icon::BodyMirror: {
        p.drawRect(QRectF(3, 8, 6, 10));
        p.setPen(QPen(ink, 1.4, Qt::DashLine));
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.setPen(pen(accent, 1.8));
        p.drawRect(QRectF(15, 8, 6, 10));
        break;
    }
    case Icon::Move: {
        // Arrows out of a block.
        p.drawRect(QRectF(5, 9, 8, 8));
        p.setPen(pen(accent, 1.8));
        p.drawLine(QPointF(9, 13), QPointF(20, 13));
        arrowHead(p, {21, 13}, {16, 13}, accent);
        p.drawLine(QPointF(9, 13), QPointF(9, 3));
        arrowHead(p, {9, 2}, {9, 7}, accent);
        break;
    }
    case Icon::Measure: {
        // A ruler with ticks.
        p.save();
        p.translate(12, 12);
        p.rotate(-35);
        p.drawRect(QRectF(-10, -3.5, 20, 7));
        p.setPen(pen(accent, 1.4));
        for (int k = -7; k <= 7; k += 3)
            p.drawLine(QPointF(k, -3.5), QPointF(k, k % 2 ? -0.5 : 0.5));
        p.restore();
        break;
    }
    case Icon::Section: {
        // A block cut by a plane, the cut face filled.
        p.drawRect(QRectF(4, 7, 14, 12));
        p.setPen(pen(accent, 1.8));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
        p.drawPolygon(QPolygonF({{11, 3}, {20, 6}, {20, 21}, {11, 18}}));
        p.setBrush(Qt::NoBrush);
        break;
    }
    case Icon::Freehand: {
        // A rough loop drawn by a pen nib.
        QPainterPath loop;
        loop.moveTo(13, 20);
        loop.cubicTo(5, 21, 2, 14, 5, 9);
        loop.cubicTo(8, 4, 15, 5, 15.5, 9);
        p.drawPath(loop);
        p.setPen(pen(accent, 1.6));
        p.setBrush(accent);
        p.drawPolygon(QPolygonF({{15.5, 9}, {17.5, 13}, {21.5, 5}, {19.5, 4}}));
        p.setBrush(Qt::NoBrush);
        break;
    }
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
