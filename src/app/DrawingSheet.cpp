#include "app/DrawingSheet.h"

#include "viewer/UnitText.h"

#include <QCoreApplication>
#include <QDate>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

// ISO 128 line widths, mm.
constexpr double kThick = 0.5;
constexpr double kThin = 0.25;
constexpr double kHair = 0.18;
constexpr double kText = 3.5;     // dimension and note text height
constexpr double kArrow = 3.0;    // arrowhead length
constexpr double kGap = 1.0;      // between an extension line and what it extends
constexpr double kOvershoot = 1.5; // extension lines past the dimension line

struct Vec
{
    double x, y;
    Vec operator+(Vec o) const { return {x + o.x, y + o.y}; }
    Vec operator-(Vec o) const { return {x - o.x, y - o.y}; }
    Vec operator*(double k) const { return {x * k, y * k}; }
    double length() const { return std::hypot(x, y); }
    Vec unit() const
    {
        const double l = length();
        return l > 1e-12 ? Vec{x / l, y / l} : Vec{1, 0};
    }
    Vec left() const { return {-y, x}; }
};

Vec vec(const gp_Pnt2d& p)
{
    return {p.X(), p.Y()};
}

// Text in sheet mm: `height` tall, turned `angle` degrees (counter-clockwise, Y up),
// with `anchor` (sheet, Y up) at the middle of its baseline.
struct TextBox
{
    Vec anchor;
    double angle = 0.0;
    double width = 0.0, height = kText;
};

QFont sheetFont()
{
    QFont font(QStringLiteral("DejaVu Sans"));
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(100); // scaled to the text height when painted
    return font;
}

double textWidth(const QString& text, double height)
{
    const QFontMetricsF metrics(sheetFont());
    double widest = 0.0;
    for (const QString& line : text.split(QLatin1Char('\n')))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    return widest * height / 100.0;
}

void drawText(QPainter& p, const Drawing& d, Vec at, const QString& text, double height, double angle,
              Qt::Alignment align = Qt::AlignHCenter | Qt::AlignBottom)
{
    p.save();
    p.translate(toPaint(d, gp_Pnt2d(at.x, at.y)));
    p.rotate(-angle);
    p.scale(height / 100.0, height / 100.0);
    p.setFont(sheetFont());
    const QFontMetricsF metrics(p.font());
    const QStringList lines = text.split(QLatin1Char('\n'));
    const double lineHeight = 100.0 * 1.4;
    double y = 0.0;
    if (align & Qt::AlignBottom)
        y = -(lines.size() - 1) * lineHeight;
    else if (align & Qt::AlignTop)
        y = 100.0;
    else
        y = 50.0 - (lines.size() - 1) * lineHeight / 2.0;
    for (const QString& line : lines) {
        const double w = metrics.horizontalAdvance(line);
        double x = 0.0;
        if (align & Qt::AlignHCenter)
            x = -w / 2.0;
        else if (align & Qt::AlignRight)
            x = -w;
        p.drawText(QPointF(x, y), line);
        y += lineHeight;
    }
    p.restore();
}

QPen pen(const QColor& c, double width, Qt::PenStyle style = Qt::SolidLine)
{
    QPen p(c, width, style, Qt::RoundCap, Qt::RoundJoin);
    if (style == Qt::DashLine)
        p.setDashPattern({12.0, 6.0}); // in pen widths: 3 mm dashes for 0.25 mm lines
    if (style == Qt::DashDotLine)
        p.setDashPattern({40.0, 8.0, 4.0, 8.0});
    return p;
}

void polyline(QPainter& p, const Drawing& d, const SheetView& v, const ViewGeometry& g, const Polyline2d& line)
{
    QPainterPath path;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const QPointF q = toPaint(d, toSheet(v, g, line[i]));
        if (i == 0)
            path.moveTo(q);
        else
            path.lineTo(q);
    }
    p.drawPath(path);
}

void segment(QPainter& p, const Drawing& d, Vec a, Vec b)
{
    p.drawLine(toPaint(d, gp_Pnt2d(a.x, a.y)), toPaint(d, gp_Pnt2d(b.x, b.y)));
}

void arrow(QPainter& p, const Drawing& d, Vec tip, Vec direction)
{
    const Vec u = direction.unit();
    const Vec base = tip - u * kArrow, side = u.left() * (kArrow * 0.18);
    QPolygonF head;
    for (Vec q : {tip, base + side, base - side})
        head << toPaint(d, gp_Pnt2d(q.x, q.y));
    p.save();
    p.setBrush(p.pen().color());
    p.drawPolygon(head);
    p.restore();
}

QString number(double mm)
{
    QString n = QString::number(toDisplayLength(mm), 'f', lengthDecimals());
    if (n.contains(QLatin1Char('.'))) {
        while (n.endsWith(QLatin1Char('0')))
            n.chop(1);
        if (n.endsWith(QLatin1Char('.')))
            n.chop(1);
    }
    return n;
}

// Everything about how a dimension is drawn, in sheet mm (Y up).
struct DimensionShape
{
    std::vector<std::pair<Vec, Vec>> lines;  // dimension and extension lines, leaders
    std::vector<std::pair<Vec, Vec>> arrows; // tip, direction
    TextBox text;
    QString label;
};

std::optional<DimensionShape> shapeOf(const SheetContent& c, int index)
{
    const Drawing& d = *c.drawing;
    const SheetDimension& m = d.dimensions[index];
    if (m.view < 0 || m.view >= static_cast<int>(d.views.size()) || m.view >= static_cast<int>(c.views.size()))
        return std::nullopt;
    const SheetView& v = d.views[m.view];
    const ViewGeometry& g = c.views[m.view];
    gp_Pnt2d a2, b2;
    double radius = 0.0;
    if (!resolveDimension(m, g, a2, b2, radius))
        return std::nullopt;
    DimensionShape s;
    s.label = dimensionLabel(m, dimensionValue(m, a2, b2, radius));
    s.text.width = textWidth(s.label, kText);
    const Vec a = vec(toSheet(v, g, a2)), b = vec(toSheet(v, g, b2));
    using Kind = SheetDimension::Kind;
    switch (m.kind) {
    case Kind::Horizontal:
    case Kind::Vertical: {
        const bool h = m.kind == Kind::Horizontal;
        auto along = [h](Vec p) { return h ? p.y : p.x; };
        const double edge = m.offset >= 0.0 ? std::max(along(a), along(b)) : std::min(along(a), along(b));
        const double at = edge + m.offset;
        auto place = [h, at](Vec p) { return h ? Vec{p.x, at} : Vec{at, p.y}; };
        const Vec pa = place(a), pb = place(b);
        const double sign = m.offset >= 0.0 ? 1.0 : -1.0;
        const Vec out = h ? Vec{0, sign} : Vec{sign, 0};
        for (Vec p : {a, b}) {
            const Vec foot = place(p);
            if ((foot - p).length() > kGap)
                s.lines.push_back({p + out * kGap, foot + out * kOvershoot});
        }
        s.lines.push_back({pa, pb});
        const Vec u = (pb - pa).unit();
        s.arrows.push_back({pa, u * -1.0});
        s.arrows.push_back({pb, u});
        s.text.angle = h ? 0.0 : 90.0;
        const Vec mid = (pa + pb) * 0.5;
        s.text.anchor = h ? mid + Vec{0, 1.0} : mid + Vec{-1.0, 0};
        break;
    }
    case Kind::Distance: {
        const Vec u = (b - a).unit();
        const Vec n = u.left();
        const Vec pa = a + n * m.offset, pb = b + n * m.offset;
        const double sign = m.offset >= 0.0 ? 1.0 : -1.0;
        if (std::abs(m.offset) > kGap) {
            s.lines.push_back({a + n * (kGap * sign), pa + n * (kOvershoot * sign)});
            s.lines.push_back({b + n * (kGap * sign), pb + n * (kOvershoot * sign)});
        }
        s.lines.push_back({pa, pb});
        s.arrows.push_back({pa, u * -1.0});
        s.arrows.push_back({pb, u});
        double angle = std::atan2(u.y, u.x) * 180.0 / kPi;
        if (angle > 90.0 + 1e-9)
            angle -= 180.0;
        else if (angle <= -90.0 + 1e-9)
            angle += 180.0;
        const double r = angle * kPi / 180.0;
        s.text.angle = angle;
        s.text.anchor = (pa + pb) * 0.5 + Vec{-std::sin(r), std::cos(r)} * 1.0;
        break;
    }
    case Kind::Radius:
    case Kind::Diameter: {
        const double r = radius * v.scale;
        const double t = m.angle * kPi / 180.0;
        const Vec dir{std::cos(t), std::sin(t)};
        const Vec rim = a + dir * r;
        const Vec end = a + dir * (r + std::max(m.offset, kArrow + 1.0));
        const Vec shoulder = end + Vec{dir.x >= 0.0 ? 3.0 : -3.0, 0.0};
        s.lines.push_back({m.kind == Kind::Diameter ? a - dir * r : a, end});
        s.lines.push_back({end, shoulder});
        s.arrows.push_back({rim, dir});
        if (m.kind == Kind::Diameter)
            s.arrows.push_back({a - dir * r, dir * -1.0});
        s.text.angle = 0.0;
        s.text.anchor = shoulder + Vec{dir.x >= 0.0 ? s.text.width / 2.0 : -s.text.width / 2.0, 0.6};
        break;
    }
    }
    return s;
}

QRectF textRect(const TextBox& t)
{
    // Corners of the turned text box, then their bounds.
    const double r = t.angle * kPi / 180.0;
    const Vec along{std::cos(r), std::sin(r)}, up{-std::sin(r), std::cos(r)};
    double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
    for (double sx : {-0.5, 0.5}) {
        for (double sy : {-0.2, 1.1}) {
            const Vec q = t.anchor + along * (sx * t.width) + up * (sy * t.height);
            x0 = std::min(x0, q.x);
            x1 = std::max(x1, q.x);
            y0 = std::min(y0, q.y);
            y1 = std::max(y1, q.y);
        }
    }
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

QString sheetSizeName(const Drawing& d)
{
    const double w = std::max(d.width, d.height), h = std::min(d.width, d.height);
    struct Size
    {
        const char* name;
        double w, h;
    };
    for (const Size& s : {Size{"A4", 297, 210}, Size{"A3", 420, 297}, Size{"A2", 594, 420}, Size{"A1", 841, 594}}) {
        if (std::abs(w - s.w) < 0.5 && std::abs(h - s.h) < 0.5)
            return QString::fromLatin1(s.name);
    }
    return QStringLiteral("%1 × %2").arg(d.width).arg(d.height);
}

} // namespace

QPointF toPaint(const Drawing& drawing, const gp_Pnt2d& p)
{
    return QPointF(p.X(), drawing.height - p.Y());
}

gp_Pnt2d fromPaint(const Drawing& drawing, const QPointF& p)
{
    return gp_Pnt2d(p.x(), drawing.height - p.y());
}

QString dimensionLabel(const SheetDimension& dimension, double value)
{
    switch (dimension.kind) {
    case SheetDimension::Kind::Radius:
        return QStringLiteral("R") + number(value);
    case SheetDimension::Kind::Diameter:
        return QStringLiteral("⌀") + number(value);
    default:
        return number(value);
    }
}

QRectF viewRect(const SheetContent& content, int index)
{
    const SheetView& v = content.drawing->views[index];
    const ViewGeometry& g = content.views[index];
    const gp_Pnt2d a = toSheet(v, g, g.min), b = toSheet(v, g, g.max);
    return QRectF(QPointF(a.X(), a.Y()), QPointF(b.X(), b.Y())).normalized();
}

std::optional<QRectF> dimensionLabelRect(const SheetContent& content, int index)
{
    const auto s = shapeOf(content, index);
    if (!s)
        return std::nullopt;
    return textRect(s->text);
}

QRectF noteRect(const SheetNote& note)
{
    const QString text = QString::fromStdString(note.text);
    const int lines = static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
    const double h = note.height * (1.4 * (lines - 1) + 1.2);
    return QRectF(note.at.X(), note.at.Y() - h, std::max(textWidth(text, note.height), 2.0), h);
}

void paintSheet(QPainter& p, const SheetContent& content, const SheetHighlight& highlight, const QColor& accent)
{
    const Drawing& d = *content.drawing;
    const QColor ink(20, 20, 20);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // Paper, frame (wider on the left, for binding) and title block.
    p.fillRect(QRectF(0, 0, d.width, d.height), Qt::white);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen(ink, kThick));
    const QRectF frame(20.0, 10.0, d.width - 30.0, d.height - 20.0);
    p.drawRect(frame);
    {
        const double w = 130.0, h = 28.0;
        const QRectF block(frame.right() - w, frame.bottom() - h, w, h);
        p.drawRect(block);
        p.setPen(pen(ink, kThin));
        const double split = block.top() + 13.0;
        p.drawLine(QPointF(block.left(), split), QPointF(block.right(), split));
        const QString scale = d.views.empty() ? QStringLiteral("–") : QString::fromStdString(scaleText(d.views.front().scale));
        const QStringList captions{QCoreApplication::translate("DrawingSheet", "Scale"), QCoreApplication::translate("DrawingSheet", "Units"), QCoreApplication::translate("DrawingSheet", "Sheet"), QCoreApplication::translate("DrawingSheet", "Date")};
        const QStringList values{scale, content.units, sheetSizeName(d), QDate::currentDate().toString(Qt::ISODate)};
        const double cell = w / captions.size();
        for (int i = 0; i < captions.size(); ++i) {
            const double x = block.left() + i * cell;
            if (i > 0)
                p.drawLine(QPointF(x, split), QPointF(x, block.bottom()));
            p.setPen(ink);
            drawText(p, d, {x + 2.0, d.height - split - 4.5}, captions[i], 2.2, 0.0, Qt::AlignLeft | Qt::AlignBottom);
            drawText(p, d, {x + 2.0, d.height - block.bottom() + 2.5}, values[i], 3.5, 0.0, Qt::AlignLeft | Qt::AlignBottom);
            p.setPen(pen(ink, kThin));
        }
        p.setPen(ink);
        const QString title = d.title.empty() ? QCoreApplication::translate("DrawingSheet", "Untitled") : QString::fromStdString(d.title);
        drawText(p, d, {block.left() + 3.0, d.height - split + 3.5}, title, 6.0, 0.0, Qt::AlignLeft | Qt::AlignBottom);
        drawText(p, d, {block.right() - 2.0, d.height - block.top() - 3.5}, QStringLiteral("Nibform"), 2.2, 0.0,
                 Qt::AlignRight | Qt::AlignBottom);
    }

    // Views.
    for (int i = 0; i < static_cast<int>(d.views.size()) && i < static_cast<int>(content.views.size()); ++i) {
        const SheetView& v = d.views[i];
        const ViewGeometry& g = content.views[i];
        const bool hot = highlight.kind == SheetHighlight::Kind::View && highlight.index == i;
        const QColor color = hot ? accent : ink;
        p.setPen(pen(color, kHair));
        for (const auto& [a, b] : g.hatch)
            p.drawLine(toPaint(d, toSheet(v, g, a)), toPaint(d, toSheet(v, g, b)));
        p.setPen(pen(color, kThin, Qt::DashLine));
        for (const Polyline2d& line : g.hidden)
            polyline(p, d, v, g, line);
        p.setPen(pen(color, kThick));
        for (const Polyline2d& line : g.visible)
            polyline(p, d, v, g, line);
        for (const Polyline2d& line : g.cut)
            polyline(p, d, v, g, line);
        // Center marks on holes and round edges seen face on.
        if (v.kind != ViewKind::Iso) {
            p.setPen(pen(color, kHair, Qt::DashDotLine));
            std::vector<std::pair<gp_Pnt2d, double>> marked;
            for (const ViewGeometry::Circle& c : g.circles) {
                if (!c.round)
                    continue;
                const bool known = std::any_of(marked.begin(), marked.end(), [&](const auto& m) {
                    return m.first.Distance(c.at) < 1e-6 && std::abs(m.second - c.radius) < 1e-6;
                });
                if (known)
                    continue;
                marked.push_back({c.at, c.radius});
                const Vec m = vec(toSheet(v, g, c.at));
                const double r = c.radius * v.scale + 2.0;
                segment(p, d, m - Vec{r, 0}, m + Vec{r, 0});
                segment(p, d, m - Vec{0, r}, m + Vec{0, r});
            }
        }
        if (v.kind == ViewKind::Section && !g.empty()) {
            p.setPen(color);
            const QRectF r = viewRect(content, i);
            const QString label = QString::fromStdString(v.label.empty() ? std::string("A") : v.label);
            drawText(p, d, {r.center().x(), r.bottom() + 5.0}, QStringLiteral("%1–%1").arg(label), 5.0, 0.0);
        }
    }

    // Where each section is cut, on the views that see its plane edge on.
    for (int s = 0; s < static_cast<int>(d.views.size()); ++s) {
        const SheetView& section = d.views[s];
        if (section.kind != ViewKind::Section)
            continue;
        const gp_Dir n = viewAxes(section.base).Direction();
        const gp_Pnt at = content.modelMiddle.Translated(gp_Vec(n) * section.offset);
        for (int i = 0; i < static_cast<int>(d.views.size()) && i < static_cast<int>(content.views.size()); ++i) {
            const SheetView& v = d.views[i];
            if (v.kind == ViewKind::Section || v.kind == ViewKind::Iso)
                continue;
            const gp_Dir eye = viewAxes(v.kind).Direction();
            if (std::abs(eye.Dot(n)) > 1e-6)
                continue;
            const ViewGeometry& g = content.views[i];
            const gp_Vec along = gp_Vec(eye).Crossed(gp_Vec(n));
            const Vec c = vec(toSheet(v, g, projectPoint(v.kind, at)));
            const Vec u = (vec(toSheet(v, g, projectPoint(v.kind, at.Translated(along)))) - c).unit();
            const Vec look = (vec(toSheet(v, g, projectPoint(v.kind, at.Translated(gp_Vec(n) * -1.0)))) - c).unit();
            // Across the view, with short thick ends just outside its outline.
            const QRectF r = viewRect(content, i);
            double t0 = 1e100, t1 = -1e100;
            for (const QPointF& q : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()}) {
                const double t = (q.x() - c.x) * u.x + (q.y() - c.y) * u.y;
                t0 = std::min(t0, t);
                t1 = std::max(t1, t);
            }
            const Vec e0 = c + u * (t0 - 6.0), e1 = c + u * (t1 + 6.0);
            p.setPen(pen(ink, kHair, Qt::DashDotLine));
            segment(p, d, e0, e1);
            p.setPen(pen(ink, kThick));
            segment(p, d, e0, e0 + u * 5.0);
            segment(p, d, e1 - u * 5.0, e1);
            p.setPen(pen(ink, kThin));
            for (Vec end : {e0 + u * 2.5, e1 - u * 2.5}) {
                segment(p, d, end, end + look * 7.0);
                arrow(p, d, end + look * 7.0, look);
            }
            p.setPen(ink);
            const QString label = QString::fromStdString(section.label.empty() ? std::string("A") : section.label);
            drawText(p, d, e0 - u * 2.0 + look * 3.0, label, 5.0, 0.0, Qt::AlignHCenter | Qt::AlignVCenter);
            drawText(p, d, e1 + u * 2.0 + look * 3.0, label, 5.0, 0.0, Qt::AlignHCenter | Qt::AlignVCenter);
        }
    }

    // Dimensions; one that lost what it measured shows as a red question mark.
    for (int i = 0; i < static_cast<int>(d.dimensions.size()); ++i) {
        const bool hot = highlight.kind == SheetHighlight::Kind::Dimension && highlight.index == i;
        const auto s = shapeOf(content, i);
        if (!s) {
            const SheetDimension& m = d.dimensions[i];
            if (m.view >= 0 && m.view < static_cast<int>(d.views.size()) && m.view < static_cast<int>(content.views.size())) {
                const SheetView& v = d.views[m.view];
                const ViewGeometry& g = content.views[m.view];
                const ViewKind kind = v.kind == ViewKind::Section ? v.base : v.kind;
                p.setPen(hot ? accent : QColor(220, 38, 38));
                drawText(p, d, vec(toSheet(v, g, projectPoint(kind, m.a))), QStringLiteral("?"), 5.0, 0.0);
            }
            continue;
        }
        const QColor color = hot ? accent : ink;
        p.setPen(pen(color, kThin));
        for (const auto& [a, b] : s->lines)
            segment(p, d, a, b);
        for (const auto& [tip, dir] : s->arrows)
            arrow(p, d, tip, dir);
        p.setPen(color);
        drawText(p, d, s->text.anchor, s->label, kText, s->text.angle);
    }

    // Notes.
    for (int i = 0; i < static_cast<int>(d.notes.size()); ++i) {
        const SheetNote& n = d.notes[i];
        const bool hot = highlight.kind == SheetHighlight::Kind::Note && highlight.index == i;
        p.setPen(hot ? accent : ink);
        drawText(p, d, vec(n.at), QString::fromStdString(n.text), n.height, 0.0, Qt::AlignLeft | Qt::AlignTop);
    }

    // What the pointer would pick.
    if (highlight.snap) {
        p.setPen(pen(accent, 0.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(toPaint(d, *highlight.snap), 1.6, 1.6);
    }
    p.restore();
}
