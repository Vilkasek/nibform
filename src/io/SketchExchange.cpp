#include "io/SketchExchange.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kJoin = 1e-4; // mm: ends closer than this are one point

QString num(double v)
{
    return QString::number(v, 'g', 12);
}

// Curve ends meet at shared points.
PointId at(Sketch& sketch, const gp_Pnt2d& p)
{
    return sketch.pointAt(p, kJoin);
}

void addLine(Sketch& sketch, std::vector<EntityId>& made, const gp_Pnt2d& a, const gp_Pnt2d& b)
{
    if (a.Distance(b) < kJoin)
        return;
    made.push_back(sketch.addLine(at(sketch, a), at(sketch, b)));
}

// An arc through `start`, `middle` and `end` (any turning direction).
void addArcThrough(Sketch& sketch, std::vector<EntityId>& made, const gp_Pnt2d& start, const gp_Pnt2d& middle,
                   const gp_Pnt2d& end)
{
    const auto center = circumcenter(start, middle, end);
    if (!center) {
        addLine(sketch, made, start, end);
        return;
    }
    const PointId c = sketch.addPoint(*center);
    const PointId a = at(sketch, start), b = at(sketch, end);
    made.push_back(isOnCcwArc(*center, start, end, middle) ? sketch.addArc(c, a, b) : sketch.addArc(c, b, a));
}

// A smooth curve through points along it (ends joined to what they touch).
void addSmooth(Sketch& sketch, std::vector<EntityId>& made, const std::vector<gp_Pnt2d>& pts, bool closed)
{
    std::vector<gp_Pnt2d> clean;
    for (const gp_Pnt2d& p : pts) {
        if (clean.empty() || clean.back().Distance(p) > kJoin)
            clean.push_back(p);
    }
    if (closed && clean.size() > 2 && clean.front().Distance(clean.back()) < kJoin)
        clean.pop_back();
    if (clean.size() < 2)
        return;
    if (clean.size() == 2 && !closed) {
        addLine(sketch, made, clean[0], clean[1]);
        return;
    }
    std::vector<PointId> ids;
    for (std::size_t i = 0; i < clean.size(); ++i) {
        const bool end = !closed && (i == 0 || i + 1 == clean.size());
        ids.push_back(end ? at(sketch, clean[i]) : sketch.addPoint(clean[i]));
    }
    if (closed && ids.size() < 3)
        return;
    made.push_back(sketch.addSpline(ids, closed));
}

// --- DXF out ------------------------------------------------------------------------

void pair(QString& out, int code, const QString& value)
{
    out += QString::number(code) + QLatin1Char('\n') + value + QLatin1Char('\n');
}

} // namespace

QByteArray sketchToDxf(const Sketch& sketch)
{
    QString out;
    pair(out, 0, QStringLiteral("SECTION"));
    pair(out, 2, QStringLiteral("HEADER"));
    pair(out, 9, QStringLiteral("$ACADVER"));
    pair(out, 1, QStringLiteral("AC1009"));
    pair(out, 9, QStringLiteral("$INSUNITS"));
    pair(out, 70, QStringLiteral("4")); // millimetres
    pair(out, 0, QStringLiteral("ENDSEC"));
    pair(out, 0, QStringLiteral("SECTION"));
    pair(out, 2, QStringLiteral("ENTITIES"));
    for (const SketchEntity& e : sketch.entities()) {
        const QString layer = e.construction ? QStringLiteral("CONSTRUCTION") : QStringLiteral("0");
        auto point = [&](int code, const gp_Pnt2d& p) {
            pair(out, code, num(p.X()));
            pair(out, code + 10, num(p.Y()));
            pair(out, code + 20, QStringLiteral("0"));
        };
        switch (e.kind) {
        case EntityKind::Line:
            pair(out, 0, QStringLiteral("LINE"));
            pair(out, 8, layer);
            point(10, sketch.pos(e.a));
            point(11, sketch.pos(e.b));
            break;
        case EntityKind::Circle:
            pair(out, 0, QStringLiteral("CIRCLE"));
            pair(out, 8, layer);
            point(10, sketch.pos(e.c));
            pair(out, 40, num(e.radius));
            break;
        case EntityKind::Arc: {
            const gp_Pnt2d c = sketch.pos(e.c), a = sketch.pos(e.a), b = sketch.pos(e.b);
            pair(out, 0, QStringLiteral("ARC"));
            pair(out, 8, layer);
            point(10, c);
            pair(out, 40, num(c.Distance(a)));
            pair(out, 50, num(std::atan2(a.Y() - c.Y(), a.X() - c.X()) * 180.0 / kPi));
            pair(out, 51, num(std::atan2(b.Y() - c.Y(), b.X() - c.X()) * 180.0 / kPi));
            break;
        }
        case EntityKind::Ellipse: {
            const EllipseAxes axes = ellipseAxes(sketch, e);
            const double u = axes.u.Magnitude(), v = axes.v.Magnitude();
            const bool uMajor = u >= v;
            const gp_Vec2d major = uMajor ? axes.u : axes.v;
            pair(out, 0, QStringLiteral("ELLIPSE"));
            pair(out, 8, layer);
            point(10, axes.center);
            point(11, gp_Pnt2d(major.X(), major.Y()));
            pair(out, 40, num(uMajor ? v / u : u / v));
            pair(out, 41, QStringLiteral("0"));
            pair(out, 42, num(2.0 * kPi));
            break;
        }
        case EntityKind::Spline: {
            // Through its points (fit points), as the sketch keeps it.
            pair(out, 0, QStringLiteral("SPLINE"));
            pair(out, 8, layer);
            pair(out, 70, e.closed ? QStringLiteral("9") : QStringLiteral("8"));
            pair(out, 71, QStringLiteral("3"));
            pair(out, 74, QString::number(e.through.size()));
            for (PointId p : e.through)
                point(11, sketch.pos(p));
            break;
        }
        }
    }
    pair(out, 0, QStringLiteral("ENDSEC"));
    pair(out, 0, QStringLiteral("EOF"));
    return out.toUtf8();
}

// --- DXF in -------------------------------------------------------------------------

namespace {

struct DxfEntity
{
    QString type;
    std::vector<std::pair<int, QString>> codes;
    double get(int code, double fallback = 0.0) const
    {
        for (const auto& [c, v] : codes) {
            if (c == code)
                return v.toDouble();
        }
        return fallback;
    }
    bool has(int code) const
    {
        return std::any_of(codes.begin(), codes.end(), [code](const auto& c) { return c.first == code; });
    }
};

// One segment of a polyline, straight or bulged into an arc (bulge: tan of a quarter
// of the turn, positive counter-clockwise).
void addBulged(Sketch& sketch, std::vector<EntityId>& made, const gp_Pnt2d& p, const gp_Pnt2d& q, double bulge)
{
    if (std::abs(bulge) < 1e-9) {
        addLine(sketch, made, p, q);
        return;
    }
    const gp_Vec2d chord(p, q);
    const double length = chord.Magnitude();
    if (length < kJoin)
        return;
    // The arc's middle sits off the chord's middle by the sagitta, to the turning side.
    const double sagitta = bulge * length / 2.0;
    const gp_Vec2d right(chord.Y() / length, -chord.X() / length);
    const gp_Pnt2d middle(p.X() + chord.X() / 2.0 + right.X() * sagitta, p.Y() + chord.Y() / 2.0 + right.Y() * sagitta);
    addArcThrough(sketch, made, p, middle, q);
}

} // namespace

std::optional<std::vector<EntityId>> importDxf(Sketch& sketch, const QByteArray& data, QString* error)
{
    QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    for (QString& l : lines)
        l = l.trimmed();
    std::vector<std::pair<int, QString>> codes;
    for (int i = 0; i + 1 < lines.size(); i += 2) {
        bool ok = false;
        const int code = lines[i].toInt(&ok);
        if (!ok) {
            if (error)
                *error = QCoreApplication::translate("SketchExchange", "Not a DXF file (or a binary one: save it as ASCII DXF)");
            return std::nullopt;
        }
        codes.emplace_back(code, lines[i + 1]);
    }

    // Units, then the entities section, entity by entity.
    double scale = 1.0;
    std::vector<DxfEntity> entities;
    QString section;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const auto& [code, value] = codes[i];
        if (code == 0 && value == QLatin1String("SECTION") && i + 1 < codes.size()) {
            section = codes[i + 1].second;
            continue;
        }
        if (code == 9 && value == QLatin1String("$INSUNITS") && i + 1 < codes.size()) {
            switch (codes[i + 1].second.toInt()) {
            case 1: scale = 25.4; break;
            case 2: scale = 304.8; break;
            case 5: scale = 10.0; break;
            case 6: scale = 1000.0; break;
            default: scale = 1.0; break;
            }
        }
        if (section != QLatin1String("ENTITIES"))
            continue;
        if (code == 0) {
            if (value == QLatin1String("ENDSEC")) {
                section.clear();
                continue;
            }
            entities.push_back({value, {}});
        } else if (!entities.empty()) {
            entities.back().codes.emplace_back(code, value);
        }
    }

    std::vector<EntityId> made;
    auto point = [scale](double x, double y) { return gp_Pnt2d(x * scale, y * scale); };
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const DxfEntity& e = entities[i];
        if (e.type == QLatin1String("LINE")) {
            addLine(sketch, made, point(e.get(10), e.get(20)), point(e.get(11), e.get(21)));
        } else if (e.type == QLatin1String("CIRCLE")) {
            if (e.get(40) > 0.0)
                made.push_back(sketch.addCircle(sketch.addPoint(point(e.get(10), e.get(20))), e.get(40) * scale));
        } else if (e.type == QLatin1String("ARC")) {
            const gp_Pnt2d c = point(e.get(10), e.get(20));
            const double r = e.get(40) * scale;
            const double a0 = e.get(50) * kPi / 180.0, a1 = e.get(51) * kPi / 180.0;
            if (r <= 0.0)
                continue;
            const gp_Pnt2d s(c.X() + r * std::cos(a0), c.Y() + r * std::sin(a0));
            const gp_Pnt2d t(c.X() + r * std::cos(a1), c.Y() + r * std::sin(a1));
            made.push_back(sketch.addArc(sketch.addPoint(c), at(sketch, s), at(sketch, t)));
        } else if (e.type == QLatin1String("LWPOLYLINE") || e.type == QLatin1String("POLYLINE")) {
            std::vector<gp_Pnt2d> pts;
            std::vector<double> bulges;
            const bool closed = static_cast<int>(e.get(70)) & 1;
            if (e.type == QLatin1String("LWPOLYLINE")) {
                std::optional<double> x;
                for (const auto& [code, value] : e.codes) {
                    if (code == 10) {
                        x = value.toDouble();
                    } else if (code == 20 && x) {
                        pts.push_back(point(*x, value.toDouble()));
                        bulges.push_back(0.0);
                        x.reset();
                    } else if (code == 42 && !bulges.empty()) {
                        bulges.back() = value.toDouble();
                    }
                }
            } else {
                // The vertices follow as entities of their own, up to SEQEND.
                for (++i; i < entities.size() && entities[i].type == QLatin1String("VERTEX"); ++i) {
                    pts.push_back(point(entities[i].get(10), entities[i].get(20)));
                    bulges.push_back(entities[i].get(42));
                }
            }
            for (std::size_t k = 0; k + 1 < pts.size(); ++k)
                addBulged(sketch, made, pts[k], pts[k + 1], bulges[k]);
            if (closed && pts.size() > 2)
                addBulged(sketch, made, pts.back(), pts.front(), bulges.back());
        } else if (e.type == QLatin1String("SPLINE")) {
            // Through its fit points when it has them, else (roughly) its control points.
            std::vector<gp_Pnt2d> fit, control;
            std::optional<double> fx, cx;
            for (const auto& [code, value] : e.codes) {
                if (code == 11)
                    fx = value.toDouble();
                else if (code == 21 && fx)
                    fit.push_back(point(*fx, value.toDouble())), fx.reset();
                else if (code == 10)
                    cx = value.toDouble();
                else if (code == 20 && cx)
                    control.push_back(point(*cx, value.toDouble())), cx.reset();
            }
            addSmooth(sketch, made, fit.size() >= 2 ? fit : control, static_cast<int>(e.get(70)) & 1);
        } else if (e.type == QLatin1String("ELLIPSE")) {
            const gp_Pnt2d c = point(e.get(10), e.get(20));
            const gp_Vec2d major(e.get(11) * scale, e.get(21) * scale);
            const double ratio = e.get(40, 1.0);
            const double t0 = e.get(41, 0.0), t1 = e.get(42, 2.0 * kPi);
            const gp_Vec2d minor(-major.Y() * ratio, major.X() * ratio);
            if (major.Magnitude() < kJoin)
                continue;
            if (std::abs(t1 - t0 - 2.0 * kPi) < 1e-6) {
                made.push_back(sketch.addEllipse(sketch.addPoint(c), sketch.addPoint(c.Translated(major)),
                                                 sketch.addPoint(c.Translated(minor))));
            } else {
                std::vector<gp_Pnt2d> pts;
                const double span = t1 > t0 ? t1 - t0 : t1 - t0 + 2.0 * kPi;
                for (int k = 0; k <= 16; ++k) {
                    const double t = t0 + span * k / 16.0;
                    pts.push_back(c.Translated(major * std::cos(t) + minor * std::sin(t)));
                }
                addSmooth(sketch, made, pts, false);
            }
        } else if (e.type == QLatin1String("POINT")) {
            sketch.pointAt(point(e.get(10), e.get(20)), kJoin);
        }
    }
    if (made.empty()) {
        if (error)
            *error = QCoreApplication::translate("SketchExchange", "The drawing has no curves this app reads (lines, circles, arcs, polylines, splines, ellipses)");
        return std::nullopt;
    }
    return made;
}

// --- SVG out ------------------------------------------------------------------------

QByteArray sketchToSvg(const Sketch& sketch)
{
    // Bounds of everything drawn, with a small margin; Y flipped to SVG's downward Y.
    double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
    for (const SketchEntity& e : sketch.entities()) {
        for (const gp_Pnt2d& p : sketch.polyline(e)) {
            x0 = std::min(x0, p.X());
            x1 = std::max(x1, p.X());
            y0 = std::min(y0, -p.Y());
            y1 = std::max(y1, -p.Y());
        }
    }
    if (x0 > x1) {
        x0 = y0 = 0.0;
        x1 = y1 = 10.0;
    }
    const double margin = 2.0;
    x0 -= margin;
    y0 -= margin;
    const double w = x1 - x0 + margin, h = y1 - y0 + margin;
    QString out;
    QTextStream s(&out);
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(w) << "mm\" height=\"" << num(h)
      << "mm\" viewBox=\"" << num(x0) << ' ' << num(y0) << ' ' << num(w) << ' ' << num(h) << "\">\n"
      << " <g fill=\"none\" stroke=\"black\" stroke-width=\"0.25\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
    auto xy = [](const gp_Pnt2d& p) { return num(p.X()) + QLatin1Char(' ') + num(-p.Y()); };
    for (const SketchEntity& e : sketch.entities()) {
        const QString style = e.construction ? QStringLiteral(" stroke=\"gray\" stroke-dasharray=\"1 1\"") : QString();
        switch (e.kind) {
        case EntityKind::Line:
            s << "  <path d=\"M " << xy(sketch.pos(e.a)) << " L " << xy(sketch.pos(e.b)) << "\"" << style << "/>\n";
            break;
        case EntityKind::Circle: {
            const gp_Pnt2d c = sketch.pos(e.c);
            s << "  <circle cx=\"" << num(c.X()) << "\" cy=\"" << num(-c.Y()) << "\" r=\"" << num(e.radius) << "\"" << style
              << "/>\n";
            break;
        }
        case EntityKind::Arc: {
            const gp_Pnt2d c = sketch.pos(e.c), a = sketch.pos(e.a), b = sketch.pos(e.b);
            double sweep = std::atan2(b.Y() - c.Y(), b.X() - c.X()) - std::atan2(a.Y() - c.Y(), a.X() - c.X());
            if (sweep <= 0.0)
                sweep += 2.0 * kPi;
            const double r = c.Distance(a);
            // Mirrored into SVG's downward Y, counter-clockwise runs the other way: sweep flag 0.
            s << "  <path d=\"M " << xy(a) << " A " << num(r) << ' ' << num(r) << " 0 " << (sweep > kPi ? 1 : 0) << " 0 "
              << xy(b) << "\"" << style << "/>\n";
            break;
        }
        case EntityKind::Ellipse:
        case EntityKind::Spline: {
            const std::vector<gp_Pnt2d> pts = sketch.polyline(e);
            s << "  <path d=\"M " << xy(pts.front());
            for (std::size_t i = 1; i < pts.size(); ++i)
                s << " L " << xy(pts[i]);
            s << "\"" << style << "/>\n";
            break;
        }
        }
    }
    s << " </g>\n</svg>\n";
    s.flush();
    return out.toUtf8();
}

// --- SVG in -------------------------------------------------------------------------

namespace {

// x' = a x + c y + e, y' = b x + d y + f (SVG's matrix order).
struct Affine
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Affine then(const Affine& inner) const // this ∘ inner
    {
        return {a * inner.a + c * inner.b, b * inner.a + d * inner.b, a * inner.c + c * inner.d,
                b * inner.c + d * inner.d, a * inner.e + c * inner.f + e, b * inner.e + d * inner.f + f};
    }
    gp_Pnt2d map(double x, double y) const { return gp_Pnt2d(a * x + c * y + e, b * x + d * y + f); }
    // No skew, the same scale both ways: circles stay circles.
    bool similarity() const
    {
        return std::abs(a * a + b * b - (c * c + d * d)) < 1e-9 * (a * a + b * b + 1e-30) && std::abs(a * c + b * d) < 1e-9;
    }
    double scale() const { return std::sqrt(std::abs(a * d - b * c)); }
};

Affine parseTransform(const QString& text)
{
    Affine total;
    static const QRegularExpression item(QStringLiteral("(matrix|translate|scale|rotate|skewX|skewY)\\s*\\(([^)]*)\\)"));
    static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
    auto it = item.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString name = m.captured(1);
        std::vector<double> v;
        for (const QString& part : m.captured(2).split(separators, Qt::SkipEmptyParts))
            v.push_back(part.toDouble());
        Affine t;
        if (name == QLatin1String("matrix") && v.size() == 6) {
            t = {v[0], v[1], v[2], v[3], v[4], v[5]};
        } else if (name == QLatin1String("translate") && !v.empty()) {
            t.e = v[0];
            t.f = v.size() > 1 ? v[1] : 0.0;
        } else if (name == QLatin1String("scale") && !v.empty()) {
            t.a = v[0];
            t.d = v.size() > 1 ? v[1] : v[0];
        } else if (name == QLatin1String("rotate") && !v.empty()) {
            const double r = v[0] * kPi / 180.0;
            Affine turn{std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
            if (v.size() >= 3) {
                Affine to{1, 0, 0, 1, v[1], v[2]}, back{1, 0, 0, 1, -v[1], -v[2]};
                turn = to.then(turn).then(back);
            }
            t = turn;
        } else if (name == QLatin1String("skewX") && !v.empty()) {
            t.c = std::tan(v[0] * kPi / 180.0);
        } else if (name == QLatin1String("skewY") && !v.empty()) {
            t.b = std::tan(v[0] * kPi / 180.0);
        }
        total = total.then(t);
    }
    return total;
}

// A length with its unit, in millimetres (CSS pixels at 96 per inch).
std::optional<double> lengthMm(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*([-+0-9.eE]+)\\s*(mm|cm|in|pt|pc|px)?\\s*$"));
    const auto m = pattern.match(text);
    if (!m.hasMatch())
        return std::nullopt;
    const double v = m.captured(1).toDouble();
    const QString unit = m.captured(2);
    if (unit == QLatin1String("mm"))
        return v;
    if (unit == QLatin1String("cm"))
        return v * 10.0;
    if (unit == QLatin1String("in"))
        return v * 25.4;
    if (unit == QLatin1String("pt"))
        return v * 25.4 / 72.0;
    if (unit == QLatin1String("pc"))
        return v * 25.4 / 6.0;
    return v * 25.4 / 96.0;
}

// Numbers (and single-digit arc flags) of a path's data, read as asked for.
class PathReader
{
public:
    explicit PathReader(const QString& d) : m_d(d) {}
    bool atCommand()
    {
        skip();
        return m_i < m_d.size() && m_d[m_i].isLetter() && m_d[m_i] != QLatin1Char('e') && m_d[m_i] != QLatin1Char('E');
    }
    bool atEnd()
    {
        skip();
        return m_i >= m_d.size();
    }
    QChar command() { return m_d[m_i++]; }
    std::optional<double> number()
    {
        skip();
        static const QRegularExpression pattern(QStringLiteral("[-+]?(\\d+\\.?\\d*|\\.\\d+)([eE][-+]?\\d+)?"));
        const auto m = pattern.match(m_d, m_i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch())
            return std::nullopt;
        m_i += m.capturedLength();
        return m.captured(0).toDouble();
    }
    std::optional<double> flag()
    {
        skip();
        if (m_i < m_d.size() && (m_d[m_i] == QLatin1Char('0') || m_d[m_i] == QLatin1Char('1')))
            return m_d[m_i++] == QLatin1Char('1') ? 1.0 : 0.0;
        return std::nullopt;
    }

private:
    void skip()
    {
        while (m_i < m_d.size() && (m_d[m_i].isSpace() || m_d[m_i] == QLatin1Char(',')))
            ++m_i;
    }
    QString m_d;
    qsizetype m_i = 0;
};

struct SvgBuilder
{
    Sketch& sketch;
    std::vector<EntityId> made;

    void line(const Affine& t, double x0, double y0, double x1, double y1)
    {
        addLine(sketch, made, t.map(x0, y0), t.map(x1, y1));
    }

    void ellipse(const Affine& t, double cx, double cy, double rx, double ry)
    {
        if (rx <= 0.0 || ry <= 0.0)
            return;
        const gp_Pnt2d c = t.map(cx, cy);
        if (t.similarity() && std::abs(rx - ry) < 1e-9 * rx) {
            made.push_back(sketch.addCircle(sketch.addPoint(c), rx * t.scale()));
            return;
        }
        const gp_Pnt2d a = t.map(cx + rx, cy), b = t.map(cx, cy + ry);
        if (std::abs(gp_Vec2d(c, a).Dot(gp_Vec2d(c, b))) < 1e-9 * c.Distance(a) * c.Distance(b)) {
            // Still perpendicular half-axes; keep the right-handed order.
            const bool ccw = gp_Vec2d(c, a).Crossed(gp_Vec2d(c, b)) > 0.0;
            made.push_back(sketch.addEllipse(sketch.addPoint(c), sketch.addPoint(ccw ? a : b), sketch.addPoint(ccw ? b : a)));
            return;
        }
        std::vector<gp_Pnt2d> pts;
        for (int k = 0; k < 24; ++k) {
            const double u = 2.0 * kPi * k / 24.0;
            pts.push_back(t.map(cx + rx * std::cos(u), cy + ry * std::sin(u)));
        }
        addSmooth(sketch, made, pts, true);
    }

    // SVG's elliptical arc (endpoint form) from (x1, y1) to (x2, y2).
    void arc(const Affine& t, double x1, double y1, double rx, double ry, double phiDeg, bool large, bool sweep, double x2,
             double y2)
    {
        rx = std::abs(rx);
        ry = std::abs(ry);
        if (rx < 1e-12 || ry < 1e-12 || (std::abs(x1 - x2) < 1e-12 && std::abs(y1 - y2) < 1e-12)) {
            line(t, x1, y1, x2, y2);
            return;
        }
        const double phi = phiDeg * kPi / 180.0, cp = std::cos(phi), sp = std::sin(phi);
        const double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
        const double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
        const double lambda = xp * xp / (rx * rx) + yp * yp / (ry * ry);
        if (lambda > 1.0) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }
        const double num_ = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp;
        const double den = rx * rx * yp * yp + ry * ry * xp * xp;
        double coef = den > 0.0 ? std::sqrt(std::max(0.0, num_ / den)) : 0.0;
        if (large == sweep)
            coef = -coef;
        const double cxp = coef * rx * yp / ry, cyp = -coef * ry * xp / rx;
        const double cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0, cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0;
        auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
        const double t1 = angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry);
        double dt = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry);
        if (!sweep && dt > 0)
            dt -= 2.0 * kPi;
        else if (sweep && dt < 0)
            dt += 2.0 * kPi;
        auto pointAt = [&](double u) {
            return t.map(cx + rx * cp * std::cos(u) - ry * sp * std::sin(u), cy + rx * sp * std::cos(u) + ry * cp * std::sin(u));
        };
        if (std::abs(rx - ry) < 1e-9 * rx && t.similarity()) {
            addArcThrough(sketch, made, t.map(x1, y1), pointAt(t1 + dt / 2.0), t.map(x2, y2));
            return;
        }
        std::vector<gp_Pnt2d> pts;
        const int n = std::max(4, static_cast<int>(std::ceil(std::abs(dt) / (kPi / 8.0))));
        for (int k = 0; k <= n; ++k)
            pts.push_back(k == 0 ? t.map(x1, y1) : k == n ? t.map(x2, y2) : pointAt(t1 + dt * k / n));
        addSmooth(sketch, made, pts, false);
    }

    void path(const Affine& t, const QString& d)
    {
        PathReader r(d);
        double x = 0, y = 0, sx = 0, sy = 0;   // current point, subpath start
        double cx = 0, cy = 0;                 // last control point (for S / T)
        QChar last;
        while (!r.atEnd()) {
            QChar cmd = r.atCommand() ? r.command() : last;
            if (cmd.isNull())
                return;
            const bool rel = cmd.isLower();
            const QChar up = cmd.toUpper();
            auto n = [&]() { return r.number(); };
            if (up == QLatin1Char('Z')) {
                line(t, x, y, sx, sy);
                x = sx;
                y = sy;
                last = QChar();
                cx = x;
                cy = y;
                continue;
            }
            if (up == QLatin1Char('M')) {
                const auto px = n(), py = n();
                if (!px || !py)
                    return;
                x = (rel ? x : 0) + *px;
                y = (rel ? y : 0) + *py;
                sx = x;
                sy = y;
                last = rel ? QLatin1Char('l') : QLatin1Char('L'); // more pairs are lines
                cx = x;
                cy = y;
                continue;
            }
            if (up == QLatin1Char('L') || up == QLatin1Char('H') || up == QLatin1Char('V')) {
                double nx = x, ny = y;
                if (up == QLatin1Char('L')) {
                    const auto px = n(), py = n();
                    if (!px || !py)
                        return;
                    nx = (rel ? x : 0) + *px;
                    ny = (rel ? y : 0) + *py;
                } else if (up == QLatin1Char('H')) {
                    const auto px = n();
                    if (!px)
                        return;
                    nx = (rel ? x : 0) + *px;
                } else {
                    const auto py = n();
                    if (!py)
                        return;
                    ny = (rel ? y : 0) + *py;
                }
                line(t, x, y, nx, ny);
                x = cx = nx;
                y = cy = ny;
            } else if (up == QLatin1Char('C') || up == QLatin1Char('S') || up == QLatin1Char('Q') || up == QLatin1Char('T')) {
                double c1x = x, c1y = y, c2x, c2y, ex, ey;
                const bool cubic = up == QLatin1Char('C') || up == QLatin1Char('S');
                const bool smooth = up == QLatin1Char('S') || up == QLatin1Char('T');
                if (smooth) {
                    c1x = 2 * x - cx;
                    c1y = 2 * y - cy;
                } else {
                    const auto a = n(), b = n();
                    if (!a || !b)
                        return;
                    c1x = (rel ? x : 0) + *a;
                    c1y = (rel ? y : 0) + *b;
                }
                if (cubic) {
                    const auto a = n(), b = n();
                    if (!a || !b)
                        return;
                    c2x = (rel ? x : 0) + *a;
                    c2y = (rel ? y : 0) + *b;
                } else {
                    c2x = c1x;
                    c2y = c1y;
                }
                const auto a = n(), b = n();
                if (!a || !b)
                    return;
                ex = (rel ? x : 0) + *a;
                ey = (rel ? y : 0) + *b;
                // Through points along the curve.
                std::vector<gp_Pnt2d> pts;
                for (int k = 0; k <= 4; ++k) {
                    const double u = k / 4.0, v = 1.0 - u;
                    double px, py;
                    if (cubic) {
                        px = v * v * v * x + 3 * v * v * u * c1x + 3 * v * u * u * c2x + u * u * u * ex;
                        py = v * v * v * y + 3 * v * v * u * c1y + 3 * v * u * u * c2y + u * u * u * ey;
                    } else {
                        px = v * v * x + 2 * v * u * c1x + u * u * ex;
                        py = v * v * y + 2 * v * u * c1y + u * u * ey;
                    }
                    pts.push_back(t.map(px, py));
                }
                addSmooth(sketch, made, pts, false);
                cx = cubic ? c2x : c1x;
                cy = cubic ? c2y : c1y;
                x = ex;
                y = ey;
            } else if (up == QLatin1Char('A')) {
                const auto rx = n(), ry = n(), phi = n();
                const auto large = r.flag(), sweep = r.flag();
                const auto px = n(), py = n();
                if (!rx || !ry || !phi || !large || !sweep || !px || !py)
                    return;
                const double nx = (rel ? x : 0) + *px, ny = (rel ? y : 0) + *py;
                arc(t, x, y, *rx, *ry, *phi, *large > 0.5, *sweep > 0.5, nx, ny);
                x = cx = nx;
                y = cy = ny;
            } else {
                return; // unknown command
            }
            last = cmd;
        }
    }
};

std::vector<double> numbers(const QString& text)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
    std::vector<double> v;
    for (const QString& part : text.split(separators, Qt::SkipEmptyParts))
        v.push_back(part.toDouble());
    return v;
}

} // namespace

std::optional<std::vector<EntityId>> importSvg(Sketch& sketch, const QByteArray& data, QString* error)
{
    QXmlStreamReader xml(data);
    SvgBuilder build{sketch, {}};
    std::vector<Affine> stack;
    int skipping = 0; // inside <defs>, <clipPath> and the like
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (skipping > 0)
                --skipping;
            else if (!stack.empty())
                stack.pop_back();
            continue;
        }
        if (token != QXmlStreamReader::StartElement)
            continue;
        if (skipping > 0) {
            ++skipping;
            continue;
        }
        const QString name = xml.name().toString();
        const auto attrs = xml.attributes();
        auto value = [&](const char* key, double fallback = 0.0) {
            const QStringView v = attrs.value(QLatin1String(key));
            return v.isEmpty() ? fallback : lengthMm(v.toString()).value_or(0.0) * 96.0 / 25.4;
        };
        static const QStringList hidden{QStringLiteral("defs"),   QStringLiteral("clipPath"), QStringLiteral("mask"),
                                        QStringLiteral("symbol"), QStringLiteral("marker"),   QStringLiteral("pattern"),
                                        QStringLiteral("metadata"), QStringLiteral("title"),  QStringLiteral("desc")};
        if (hidden.contains(name)) {
            skipping = 1;
            continue;
        }
        Affine t;
        if (name == QLatin1String("svg") && stack.empty()) {
            // Millimetres per user unit, then Y up.
            double scale = 25.4 / 96.0;
            const auto viewBox = numbers(attrs.value(QLatin1String("viewBox")).toString());
            const auto width = lengthMm(attrs.value(QLatin1String("width")).toString());
            // User coordinates stay as they are (the view box only frames them).
            if (viewBox.size() == 4 && viewBox[2] > 0.0 && width)
                scale = *width / viewBox[2];
            t = Affine{scale, 0, 0, -scale, 0, 0};
        }
        t = (stack.empty() ? Affine() : stack.back()).then(t).then(parseTransform(attrs.value(QLatin1String("transform")).toString()));
        stack.push_back(t);
        if (name == QLatin1String("line")) {
            build.line(t, value("x1"), value("y1"), value("x2"), value("y2"));
        } else if (name == QLatin1String("rect")) {
            const double x = value("x"), y = value("y"), w = value("width"), h = value("height");
            if (w > 0.0 && h > 0.0) {
                build.line(t, x, y, x + w, y);
                build.line(t, x + w, y, x + w, y + h);
                build.line(t, x + w, y + h, x, y + h);
                build.line(t, x, y + h, x, y);
            }
        } else if (name == QLatin1String("circle")) {
            build.ellipse(t, value("cx"), value("cy"), value("r"), value("r"));
        } else if (name == QLatin1String("ellipse")) {
            build.ellipse(t, value("cx"), value("cy"), value("rx"), value("ry"));
        } else if (name == QLatin1String("polyline") || name == QLatin1String("polygon")) {
            const auto v = numbers(attrs.value(QLatin1String("points")).toString());
            for (std::size_t i = 2; i + 1 < v.size(); i += 2)
                build.line(t, v[i - 2], v[i - 1], v[i], v[i + 1]);
            if (name == QLatin1String("polygon") && v.size() >= 6)
                build.line(t, v[v.size() - 2], v[v.size() - 1], v[0], v[1]);
        } else if (name == QLatin1String("path")) {
            build.path(t, attrs.value(QLatin1String("d")).toString());
        }
    }
    if (xml.hasError()) {
        if (error)
            *error = QCoreApplication::translate("SketchExchange", "Not a readable SVG file: %1").arg(xml.errorString());
        return std::nullopt;
    }
    if (build.made.empty()) {
        if (error)
            *error = QCoreApplication::translate("SketchExchange", "The drawing has no shapes this app reads (paths, lines, rectangles, circles, ellipses, polylines)");
        return std::nullopt;
    }
    return build.made;
}

// --- Files --------------------------------------------------------------------------

namespace {

QString writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return QCoreApplication::translate("SketchExchange", "Could not write %1").arg(path);
    return {};
}

} // namespace

QString exportSketchDxf(const Sketch& sketch, const QString& path)
{
    return writeFile(path, sketchToDxf(sketch));
}

QString exportSketchSvg(const Sketch& sketch, const QString& path)
{
    return writeFile(path, sketchToSvg(sketch));
}

std::optional<std::vector<EntityId>> importSketchFile(Sketch& sketch, const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QCoreApplication::translate("SketchExchange", "Could not open %1").arg(path);
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("svg"))
        return importSvg(sketch, data, error);
    return importDxf(sketch, data, error);
}
