#include "model/Threads.h"
#include "model/Tr.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax3.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin2d.hxx>

#include <algorithm>
#include <cmath>
#include <charconv>
#include <limits>

namespace {

constexpr double kInch = 25.4;

ThreadSize unified(const char* name, double inches, double tpi, double drillMm)
{
    return {name, inches * kInch, kInch / tpi, drillMm};
}

// ISO 68-1: the fundamental triangle's height.
double fundamentalHeight(double pitch)
{
    return pitch * std::sqrt(3.0) / 2.0;
}

const ThreadSize* standardSize(const ThreadSpec& spec)
{
    for (const ThreadSize& size : threadSizes(spec.standard)) {
        if (std::abs(size.diameter - spec.diameter) < 1e-6 && std::abs(size.pitch - spec.pitch) < 1e-6)
            return &size;
    }
    return nullptr;
}

// Whatever the locale: a callout is a name, and C's printf would write 4,5 under Polish.
std::string number(double v)
{
    char text[32];
    const auto [end, error] = std::to_chars(text, text + sizeof text, v, std::chars_format::fixed, 2);
    std::string s = error == std::errc() ? std::string(text, end) : std::string("?");
    while (s.find('.') != std::string::npos && s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return s;
}

} // namespace

const std::vector<ThreadSize>& threadSizes(ThreadStandard standard)
{
    // ISO 261 coarse pitches, with the usual tap drills (diameter minus pitch).
    static const std::vector<ThreadSize> metric{
        {"M1.6", 1.6, 0.35, 1.25}, {"M2", 2.0, 0.4, 1.6},    {"M2.5", 2.5, 0.45, 2.05}, {"M3", 3.0, 0.5, 2.5},
        {"M4", 4.0, 0.7, 3.3},     {"M5", 5.0, 0.8, 4.2},    {"M6", 6.0, 1.0, 5.0},     {"M8", 8.0, 1.25, 6.8},
        {"M10", 10.0, 1.5, 8.5},   {"M12", 12.0, 1.75, 10.2}, {"M14", 14.0, 2.0, 12.0}, {"M16", 16.0, 2.0, 14.0},
        {"M20", 20.0, 2.5, 17.5},  {"M24", 24.0, 3.0, 21.0}, {"M30", 30.0, 3.5, 26.5},
    };
    // ASME B1.1, with the number / letter / fraction drills in millimetres.
    static const std::vector<ThreadSize> unc{
        unified("#2-56", 0.086, 56, 1.778),  unified("#4-40", 0.112, 40, 2.261), unified("#6-32", 0.138, 32, 2.705),
        unified("#8-32", 0.164, 32, 3.454),  unified("#10-24", 0.190, 24, 3.797), unified("1/4-20", 0.25, 20, 5.105),
        unified("5/16-18", 0.3125, 18, 6.528), unified("3/8-16", 0.375, 16, 7.938), unified("7/16-14", 0.4375, 14, 9.347),
        unified("1/2-13", 0.5, 13, 10.716),  unified("5/8-11", 0.625, 11, 13.494), unified("3/4-10", 0.75, 10, 16.669),
        unified("1-8", 1.0, 8, 22.225),
    };
    static const std::vector<ThreadSize> unf{
        unified("#4-48", 0.112, 48, 2.375),  unified("#6-40", 0.138, 40, 2.870), unified("#8-36", 0.164, 36, 3.454),
        unified("#10-32", 0.190, 32, 4.039), unified("1/4-28", 0.25, 28, 5.410), unified("5/16-24", 0.3125, 24, 6.909),
        unified("3/8-24", 0.375, 24, 8.433), unified("7/16-20", 0.4375, 20, 9.922), unified("1/2-20", 0.5, 20, 11.509),
        unified("5/8-18", 0.625, 18, 14.684), unified("3/4-16", 0.75, 16, 17.463), unified("1-12", 1.0, 12, 23.416),
    };
    switch (standard) {
    case ThreadStandard::UNC: return unc;
    case ThreadStandard::UNF: return unf;
    case ThreadStandard::Metric: break;
    }
    return metric;
}

ThreadSpec nearestThread(ThreadStandard standard, double diameter, const ThreadSpec& like)
{
    ThreadSpec spec = like;
    spec.standard = standard;
    double best = std::numeric_limits<double>::max();
    for (const ThreadSize& size : threadSizes(standard)) {
        const double d = std::abs(size.diameter - diameter);
        if (d < best) {
            best = d;
            spec.diameter = size.diameter;
            spec.pitch = size.pitch;
        }
    }
    return spec;
}

ThreadSpec threadForCylinder(ThreadStandard standard, double diameter, bool internal, const ThreadSpec& like)
{
    if (!internal)
        return nearestThread(standard, diameter, like);
    ThreadSpec spec = like;
    spec.standard = standard;
    double best = std::numeric_limits<double>::max();
    for (const ThreadSize& size : threadSizes(standard)) {
        const double d = std::abs(size.tapDrill - diameter);
        if (d < best) {
            best = d;
            spec.diameter = size.diameter;
            spec.pitch = size.pitch;
        }
    }
    return spec;
}

double tapDrill(const ThreadSpec& spec)
{
    if (const ThreadSize* size = standardSize(spec))
        return size->tapDrill;
    return spec.diameter - spec.pitch;
}

double minorDiameter(const ThreadSpec& spec)
{
    return spec.diameter - 2.0 * (17.0 / 24.0) * fundamentalHeight(spec.pitch);
}

std::string threadCallout(const ThreadSpec& spec)
{
    const ThreadSize* size = standardSize(spec);
    if (spec.standard == ThreadStandard::Metric)
        return "M" + number(spec.diameter) + "×" + number(spec.pitch);
    const std::string suffix = spec.standard == ThreadStandard::UNC ? " UNC" : " UNF";
    if (size)
        return size->name + suffix;
    return number(spec.diameter / kInch) + "-" + number(kInch / spec.pitch) + suffix;
}

bool isHoleWall(const TopoDS_Shape& body, const TopoDS_Face& face)
{
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Cylinder)
        return false;
    const gp_Cylinder cylinder = surface.Cylinder();
    const gp_Ax3 frame = cylinder.Position();
    const double r = cylinder.Radius();
    double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    // A hole's wall has material behind it; a bolt's has air.
    const double step = std::min(0.05, 0.02 * r);
    const double u = (u0 + u1) / 2.0;
    const gp_Vec radial = gp_Vec(frame.XDirection()) * std::cos(u) + gp_Vec(frame.YDirection()) * std::sin(u);
    const gp_Pnt behind =
        frame.Location().Translated(gp_Vec(frame.Direction()) * ((v0 + v1) / 2.0) + radial * (r + step));
    return BRepClass3d_SolidClassifier(body, behind, 1e-7).State() == TopAbs_IN;
}

std::optional<ThreadMark> placeThread(const TopoDS_Shape& body, const TopoDS_Face& face, const ThreadSpec& spec,
                                      const gp_Pnt& near, std::string& error)
{
    const BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Cylinder) {
        error = QT_TRANSLATE_NOOP("Model", "A thread goes on a round face (a cylinder or a hole)");
        return std::nullopt;
    }
    if (spec.diameter <= 0.0 || spec.pitch <= 0.0) {
        error = QT_TRANSLATE_NOOP("Model", "The thread's size isn't valid");
        return std::nullopt;
    }
    const gp_Cylinder cylinder = surface.Cylinder();
    const gp_Ax3 frame = cylinder.Position();
    const double r = cylinder.Radius();
    double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    BRepTools::UVBounds(face, u0, u1, v0, v1);
    if (v1 - v0 < 1e-6) {
        error = QT_TRANSLATE_NOOP("Model", "The round face is too short for a thread");
        return std::nullopt;
    }
    const gp_Vec along(frame.Direction());
    const gp_Vec radial(frame.XDirection());
    auto onAxis = [&](double v) { return frame.Location().Translated(along * v); };

    BRepClass3d_SolidClassifier classifier(body);
    auto inside = [&](const gp_Pnt& p) {
        classifier.Perform(p, 1e-7);
        return classifier.State() == TopAbs_IN;
    };
    const bool internal = isHoleWall(body, face);
    if (internal && spec.diameter <= 2.0 * r + 1e-6) {
        error = QT_TRANSLATE_NOOP("Model", "The thread isn't wider than the hole: pick a larger size");
        return std::nullopt;
    }
    if (!internal && minorDiameter(spec) >= 2.0 * r) {
        error = QT_TRANSLATE_NOOP("Model", "The thread is too big for the cylinder: pick a smaller size");
        return std::nullopt;
    }
    if (!internal && spec.diameter < 2.0 * r - spec.pitch) {
        error = QT_TRANSLATE_NOOP("Model", "The thread is too small for the cylinder: pick a larger size");
        return std::nullopt;
    }
    // An end is open when just past it, near the wall, there is air.
    const double probe = std::min(spec.pitch / 2.0, (v1 - v0) / 4.0);
    auto open = [&](double v) { return !inside(onAxis(v).Translated(radial * (0.9 * r))); };
    const bool open0 = open(v0 - probe), open1 = open(v1 + probe);
    bool fromStart = true; // starts at v0
    if (open0 != open1)
        fromStart = open0;
    else
        fromStart = near.Distance(onAxis(v0)) <= near.Distance(onAxis(v1));

    ThreadMark mark;
    mark.spec = spec;
    mark.radius = r;
    mark.internal = internal;
    const double full = v1 - v0;
    mark.length = spec.length > 0.0 ? std::min(spec.length, full) : full;
    mark.axis = fromStart ? gp_Ax1(onAxis(v0), frame.Direction()) : gp_Ax1(onAxis(v1), frame.Direction().Reversed());
    mark.openStart = fromStart ? open0 : open1;
    mark.openEnd = mark.length >= full - 1e-6 && (fromStart ? open1 : open0);
    return mark;
}

TopoDS_Shape threadGroove(const ThreadMark& mark)
{
    const double pitch = mark.spec.pitch;
    const double h = fundamentalHeight(pitch);
    // The triangle reaches a little past the surface, so the cut opens cleanly.
    const double margin = 0.15 * pitch;
    double base = 0.0, apex = 0.0;
    if (mark.internal) {
        base = mark.radius - margin;
        apex = mark.spec.diameter / 2.0;
    } else {
        base = mark.radius + margin;
        apex = mark.radius - (17.0 / 24.0) * h;
    }
    const double half = std::abs(base - apex) * std::tan(M_PI / 6.0); // 60° flanks
    // Past an open end the groove runs out into the air; at a closed one (a hole's
    // bottom, a bolt's shoulder) it stops short of the wall.
    const double z0 = mark.openStart ? -pitch : half;
    const double z1 = mark.openEnd ? mark.length + pitch : mark.length - half;
    TopoDS_Compound turns;
    BRep_Builder().MakeCompound(turns);
    if (z1 - z0 < 1e-6)
        return turns;

    const gp_Dir direction = mark.axis.Direction();
    const gp_Dir x = std::abs(direction.Dot(gp::DZ())) < 0.9 ? gp_Dir(gp_Vec(direction).Crossed(gp::DZ()))
                                                             : gp_Dir(gp_Vec(direction).Crossed(gp::DX()));
    const gp_Ax3 frame(mark.axis.Location(), direction, x);
    const Handle(Geom_CylindricalSurface) helixSurface = new Geom_CylindricalSurface(frame, base);
    const double total = (z1 - z0) / pitch;
    const int count = std::max(1, static_cast<int>(std::ceil(total)));
    const double per = total / count;
    const gp_Vec up(direction);
    for (int i = 0; i < count; ++i) {
        const double a0 = 2.0 * M_PI * per * i;
        const double start = z0 + pitch * per * i;
        // A right-hand helix: a straight line on the unrolled cylinder.
        const gp_Lin2d line(gp_Pnt2d(a0, start), gp_Dir2d(2.0 * M_PI, pitch));
        const double length = per * std::sqrt(4.0 * M_PI * M_PI + pitch * pitch);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(new Geom2d_Line(line), helixSurface, 0.0, length);
        BRepLib::BuildCurves3d(edge, 1e-6 * (base + pitch), GeomAbs_C1, 14, 10000);
        const TopoDS_Wire spine = BRepBuilderAPI_MakeWire(edge);
        const gp_Vec radial = gp_Vec(frame.XDirection()) * std::cos(a0) + gp_Vec(frame.YDirection()) * std::sin(a0);
        const gp_Pnt onBase = frame.Location().Translated(radial * base + up * start);
        const gp_Pnt tip = frame.Location().Translated(radial * apex + up * start);
        BRepBuilderAPI_MakePolygon triangle(onBase.Translated(up * half), tip, onBase.Translated(up * -half), true);
        BRepOffsetAPI_MakePipeShell pipe(spine);
        pipe.SetMode(direction); // the profile keeps facing the axis
        pipe.Add(triangle.Wire());
        pipe.Build();
        if (!pipe.IsDone() || !pipe.MakeSolid())
            continue;
        BRep_Builder().Add(turns, pipe.Shape());
    }
    return turns;
}

TopoDS_Shape threadSurface(const ThreadMark& mark)
{
    const double r = mark.internal ? mark.spec.diameter / 2.0 : minorDiameter(mark.spec) / 2.0;
    const gp_Cylinder cylinder(gp_Ax3(mark.axis.Location(), mark.axis.Direction()), r);
    return BRepBuilderAPI_MakeFace(cylinder, 0.0, 2.0 * M_PI, 0.0, mark.length).Shape();
}

const ThreadMark* threadAt(const std::vector<ThreadMark>& marks, const gp_Pnt& center, double radius)
{
    for (const ThreadMark& m : marks) {
        if (std::abs(m.radius - radius) > 1e-4)
            continue;
        const gp_Vec offset(m.axis.Location(), center);
        const double off = offset.Crossed(gp_Vec(m.axis.Direction())).Magnitude(); // from the axis
        if (off < 1e-4)
            return &m;
    }
    return nullptr;
}
