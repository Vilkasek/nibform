// Sheet metal (M18): base flanges (flat and bent along a path), edge flanges and hems,
// tabs, corner reliefs, shelled bodies converted, and the flat pattern of each, sized by
// the bend allowance of its k-factor.

#include "Check.h"

#include "io/ProjectFile.h"
#include "io/SketchExchange.h"
#include "model/Drawing.h"
#include "model/Features.h"
#include "model/Parameters.h"
#include "model/Profiles.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cmath>
#include <map>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Model
{
    std::map<int, Sketch> sketches;
    std::vector<Feature> history;
    SketchLookup lookup() const
    {
        return [this](int id) { return sketches.count(id) ? &sketches.at(id) : nullptr; };
    }
    RebuildResult build()
    {
        numberFeatures(history);
        return rebuild(lookup(), history);
    }
    Feature& add(Feature f)
    {
        history.push_back(f);
        numberFeatures(history);
        return history.back();
    }
    Sketch& sketch(int id, const gp_Ax3& plane)
    {
        sketches.erase(id);
        return sketches.emplace(id, Sketch(plane)).first->second;
    }
};

void rectangle(Sketch& s, double x0, double y0, double x1, double y1)
{
    const PointId a = s.addPoint({x0, y0}), b = s.addPoint({x1, y0}), c = s.addPoint({x1, y1}),
                  d = s.addPoint({x0, y1});
    s.addLine(a, b);
    s.addLine(b, c);
    s.addLine(c, d);
    s.addLine(d, a);
}

struct Box
{
    double x0, y0, z0, x1, y1, z1;
};

Box boxOf(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    Box b{};
    box.Get(b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
    return b;
}

double totalVolume(const std::vector<TopoDS_Shape>& bodies)
{
    double v = 0.0;
    for (const TopoDS_Shape& b : bodies)
        v += volumeOf(b);
    return v;
}

SheetSpec spec(double t, double r, double k = 0.44)
{
    SheetSpec s;
    s.thickness = t;
    s.radius = r;
    s.kFactor = k;
    return s;
}

// A plate 100 × 60 on the XY plane, `t` thick upward: its fixed face is the top.
Model plate(double t = 2.0, double r = 1.0)
{
    Model m;
    rectangle(m.sketch(1, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())), 0, 0, 100, 60);
    Feature base;
    base.kind = Feature::Kind::SheetBase;
    base.regions = {{1, {50, 30}}};
    base.sheet = spec(t, r);
    m.add(base);
    return m;
}

// The flat pattern of the only sheet.
FlatPattern flatOf(const RebuildResult& r)
{
    const std::vector<SheetFound> sheets = findSheets(r.bodies, r.context);
    if (sheets.size() != 1)
        return FlatPattern{{}, {}, {}, 0.0, 0.0, "no sheet"};
    return unfoldSheet(r.bodies[sheets[0].body], sheets[0].fixed, sheets[0].sheet.spec);
}

// Its outline's size, the longer side first.
std::pair<double, double> flatSize(const FlatPattern& f)
{
    return {std::max(f.width, f.height), std::min(f.width, f.height)};
}

double allowance(double degrees, double r, double t, double k = 0.44)
{
    return degrees * kPi / 180.0 * (r + k * t);
}

Feature flangeOn(const gp_Pnt& middle, const gp_Dir& along, double length)
{
    Feature f;
    f.kind = Feature::Kind::Flange;
    f.edges = {{middle, along}};
    f.value = length;
    return f;
}

// The edge along the top of the plate's far end (y = 60, z = t).
constexpr double kT = 2.0, kR = 1.0;
const gp_Pnt kFarTop(50, 60, kT);

void testFlatPlate()
{
    Model m = plate();
    const RebuildResult r = m.build();
    CHECK(r.errors.at(0).empty() && r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 100.0 * 60.0 * kT, 1e-6));
    CHECK(r.context.sheets.size() == 1);
    const FlatPattern f = flatOf(r);
    CHECK(f.ok());
    CHECK(f.bends.empty());
    CHECK(near(flatSize(f).first, 100.0, 1e-6) && near(flatSize(f).second, 60.0, 1e-6));
    CHECK(near(volumeOf(f.solid), 100.0 * 60.0 * kT, 1e-3));
}

void testEdgeFlange()
{
    Model m = plate();
    m.add(flangeOn(kFarTop, gp::DX(), 20.0));
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty() && r.bodies.size() == 1);
    CHECK(BRepCheck_Analyzer(r.bodies.at(0)).IsValid());
    // Bent up beyond the end: its outside 20 above the plate's underside, r + t past the end.
    const Box b = boxOf(r.bodies.at(0));
    CHECK(near(b.z1, 20.0, 1e-4) && near(b.y1, 60.0 + kR + kT, 1e-4));
    const double bend = kPi / 4.0 * ((kR + kT) * (kR + kT) - kR * kR) * 100.0;
    const double straight = (20.0 - kR - kT) * kT * 100.0;
    CHECK(near(totalVolume(r.bodies), 12000.0 + bend + straight, 1e-2));

    // Flat: the plate, the bend's allowance and the flange's straight part.
    FlatPattern f = flatOf(r);
    CHECK(f.ok());
    CHECK(near(flatSize(f).first, 100.0, 1e-4));
    CHECK(near(flatSize(f).second, 60.0 + allowance(90, kR, kT) + 17.0, 1e-4));
    CHECK(f.bends.size() == 1);
    if (!f.bends.empty()) {
        CHECK(near(f.bends[0].angle, 90.0, 1e-6) && near(f.bends[0].radius, kR, 1e-6));
        CHECK(f.bends[0].up); // seen from above, the flange comes up toward the viewer
        CHECK(near(f.bends[0].a.Distance(f.bends[0].b), 100.0, 1e-4));
    }
    CHECK(near(volumeOf(f.solid), (60.0 + allowance(90, kR, kT) + 17.0) * 100.0 * kT, 1e-2));

    // Bent the other way, at 45°.
    m.history[1].flip = true;
    m.history[1].flange.angle = 45.0;
    r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(boxOf(r.bodies.at(0)).z0 < -5.0);
    f = flatOf(r);
    CHECK(f.ok() && f.bends.size() == 1);
    const double straight45 = 20.0 - (kR + kT) * std::tan(kPi / 8.0);
    CHECK(near(flatSize(f).second, 60.0 + allowance(45, kR, kT) + straight45, 1e-4));
    if (!f.bends.empty())
        CHECK(!f.bends[0].up && near(f.bends[0].angle, 45.0, 1e-6));
}

void testFlangeInsideWithReliefs()
{
    Model m = plate();
    Feature f = flangeOn(kFarTop, gp::DX(), 20.0);
    f.flange.inside = true;
    m.add(f);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    // The flange's outside where the end was.
    Box b = boxOf(r.bodies.at(0));
    CHECK(near(b.y1, 60.0, 1e-4) && near(b.z1, 20.0, 1e-4));
    FlatPattern flat = flatOf(r);
    CHECK(flat.ok());
    CHECK(near(flatSize(flat).second, 60.0 - kR - kT + allowance(90, kR, kT) + 17.0, 1e-4));

    // Inset from the ends: reliefs cut beside it, still one sheet that unfolds.
    m.history[1].flange.inset = 10.0;
    r = m.build();
    CHECK(r.errors.at(1).empty() && r.bodies.size() == 1);
    b = boxOf(r.bodies.at(0));
    CHECK(near(b.x1, 100.0, 1e-4));
    flat = flatOf(r);
    CHECK(flat.ok() && flat.bends.size() == 1);
    if (!flat.bends.empty())
        CHECK(near(flat.bends[0].a.Distance(flat.bends[0].b), 80.0, 1e-4));
}

void testHem()
{
    Model m = plate();
    Feature hem;
    hem.kind = Feature::Kind::Hem;
    hem.edges = {{kFarTop, gp::DX()}};
    hem.value = 10.0;
    hem.flange.gap = 0.5;
    m.add(hem);
    const RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty() && r.bodies.size() == 1);
    // Folded back over the plate, half a millimetre above it.
    const Box b = boxOf(r.bodies.at(0));
    CHECK(near(b.z1, kT + 0.5 + kT, 1e-4));
    CHECK(near(b.y1, 60.0 + 0.25 + kT, 1e-4));
    const FlatPattern f = flatOf(r);
    CHECK(f.ok() && f.bends.size() == 1);
    CHECK(near(flatSize(f).second, 60.0 + allowance(180, 0.25, kT) + 10.0, 1e-4));
    if (!f.bends.empty())
        CHECK(near(f.bends[0].angle, 180.0, 1e-4));
}

void testContourBaseFlange()
{
    // An L drawn on the XY plane, 40 deep: the material inside the corner.
    Model m;
    Sketch& s = m.sketch(1, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX()));
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({50, 0}), c = s.addPoint({50, 30});
    const EntityId first = s.addLine(a, b);
    s.addLine(b, c);
    Feature base;
    base.kind = Feature::Kind::SheetBase;
    base.pathSketch = 1;
    base.pathEntity = first;
    base.value = 40.0;
    base.sheet = spec(kT, kR);
    m.add(base);
    RebuildResult r = m.build();
    CHECK(r.errors.at(0).empty() && r.bodies.size() == 1);
    // Outside as drawn: 50 × 30.
    Box bx = boxOf(r.bodies.at(0));
    CHECK(near(bx.x1 - bx.x0, 50.0, 1e-4) && near(bx.y1 - bx.y0, 30.0, 1e-4) && near(bx.z1 - bx.z0, 40.0, 1e-4));
    FlatPattern f = flatOf(r);
    CHECK(f.ok() && f.bends.size() == 1);
    // The outside legs less the bend's setback, plus its allowance.
    CHECK(near(flatSize(f).first, 47.0 + 27.0 + allowance(90, kR, kT), 1e-4));
    CHECK(near(flatSize(f).second, 40.0, 1e-4));

    // The material outside the corner: the line is the bend's inside.
    m.history[0].flip = true;
    r = m.build();
    CHECK(r.errors.at(0).empty());
    bx = boxOf(r.bodies.at(0));
    CHECK(near(bx.x1 - bx.x0, 50.0 + kT, 1e-4) && near(bx.y1 - bx.y0, 30.0 + kT, 1e-4));
    f = flatOf(r);
    CHECK(f.ok());
    CHECK(near(flatSize(f).first, 49.0 + 29.0 + allowance(90, kR, kT), 1e-4));

    // A flange on its far end: two bends, a U.
    m.history[0].flip = false;
    m.add(flangeOn(gp_Pnt(50.0, 30.0, 20.0), gp::DZ(), 15.0));
    r = m.build();
    CHECK(r.errors.at(1).empty());
    f = flatOf(r);
    CHECK(f.ok() && f.bends.size() == 2);
}

void testTabAndRelief()
{
    Model m = plate();
    // A tab drawn on the top, sticking out 10 past the near end.
    rectangle(m.sketch(2, gp_Ax3(gp::Origin().Translated(gp_Vec(0, 0, kT)), gp::DZ(), gp::DX())), 20, -10, 40, 5);
    Feature tab;
    tab.kind = Feature::Kind::Tab;
    tab.regions = {{2, {30, -5}}};
    m.add(tab);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty() && r.bodies.size() == 1);
    CHECK(near(totalVolume(r.bodies), 12000.0 + 20.0 * 10.0 * kT, 1e-3));
    CHECK(near(boxOf(r.bodies.at(0)).y0, -10.0, 1e-6));
    FlatPattern f = flatOf(r);
    CHECK(f.ok() && near(flatSize(f).second, 70.0, 1e-4));

    // A round relief at a corner of the top.
    Feature relief;
    relief.kind = Feature::Kind::Relief;
    relief.points = {gp_Pnt(100, 60, kT)};
    relief.value = 6.0;
    m.add(relief);
    r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(near(totalVolume(r.bodies), 12000.0 + 400.0 - kPi * 9.0 / 4.0 * kT, 1e-3));
    m.history[2].flange.square = true;
    r = m.build();
    CHECK(r.errors.at(2).empty());
    CHECK(near(totalVolume(r.bodies), 12000.0 + 400.0 - 9.0 * kT, 1e-3));
}

void testConvertShelledBox()
{
    // A box 100 × 60 × 40, shelled 2 thick with its top open.
    Model m;
    rectangle(m.sketch(1, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())), 0, 0, 100, 60);
    Feature box;
    box.kind = Feature::Kind::Extrude;
    box.regions = {{1, {50, 30}}};
    box.value = 40.0;
    m.add(box);
    Feature shell;
    shell.kind = Feature::Kind::Shell;
    shell.faces = {{gp_Pnt(50, 30, 40), gp::DZ()}};
    shell.value = kT;
    m.add(shell);
    RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    const auto bottom = findFace(r.bodies, {gp_Pnt(50, 30, 0), -gp::DZ()});
    CHECK(bottom.has_value());
    const auto thick = bottom ? wallThickness(r.bodies.at(0), bottom->first) : std::nullopt;
    CHECK(thick && near(*thick, kT, 1e-6));

    Feature convert;
    convert.kind = Feature::Kind::SheetConvert;
    convert.faces = {{gp_Pnt(50, 30, 0), -gp::DZ()}};
    convert.sheet = spec(kT, kR);
    m.add(convert);
    r = m.build();
    CHECK(r.errors.at(2).empty() && r.bodies.size() == 1);
    CHECK(BRepCheck_Analyzer(r.bodies.at(0)).IsValid());
    const FlatPattern f = flatOf(r);
    CHECK(f.ok());
    CHECK(f.bends.size() == 4);
    // A cross: the bottom inside its bends, each wall's allowance and straight part.
    const double wall = allowance(90, kR, kT) + 40.0 - kR - kT;
    CHECK(near(f.width, 100.0 - 2.0 * (kR + kT) + 2.0 * wall, 1e-3));
    CHECK(near(f.height, 60.0 - 2.0 * (kR + kT) + 2.0 * wall, 1e-3));
    for (const FlatBend& b : f.bends)
        CHECK(near(b.angle, 90.0, 1e-4) && near(b.radius, kR, 1e-6));

    // A solid that isn't a shell of that thickness is refused.
    m.history[1].value = 5.0;
    r = m.build();
    CHECK(!r.errors.at(2).empty());
}

void testFollowsTheRules()
{
    // The thickness from a parameter: the flange and the flat pattern follow it.
    Model m = plate();
    m.add(flangeOn(kFarTop, gp::DX(), 20.0));
    ParameterTable table;
    CHECK(setParameterValue(table, "t", ValueKind::Length, 3.0, LengthUnit::Millimetre));
    m.history[0].expressions["sheet.thickness"] = {"t", LengthUnit::Millimetre};
    applyParameters(table, m.history, {});
    CHECK(near(m.history[0].sheet.thickness, 3.0, 1e-9));
    // The flange's edge moved up with the thicker plate.
    m.history[1].edges = {{gp_Pnt(50, 60, 3.0), gp::DX()}};
    const RebuildResult r = m.build();
    CHECK(r.errors.at(1).empty());
    CHECK(near(boxOf(r.bodies.at(0)).y1, 60.0 + kR + 3.0, 1e-4));
    const FlatPattern f = flatOf(r);
    CHECK(f.ok() && near(flatSize(f).second, 60.0 + allowance(90, kR, 3.0) + 20.0 - kR - 3.0, 1e-4));
}

void testSavedAndLoaded()
{
    Model m = plate();
    Feature f = flangeOn(kFarTop, gp::DX(), 20.0);
    f.flange.angle = 60.0;
    f.flange.inset = 5.0;
    f.flange.inside = true;
    f.flip = true;
    m.add(f);
    m.history[0].sheet = spec(1.5, 2.5, 0.33);
    ProjectData project;
    for (const auto& [id, s] : m.sketches)
        project.sketches.push_back({id, s});
    project.features = m.history;
    const QByteArray json = projectToJson(project);
    QString error;
    const auto loaded = projectFromJson(json, &error);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    CHECK(loaded->features.size() == 2);
    CHECK(loaded->features[0].kind == Feature::Kind::SheetBase);
    CHECK(loaded->features[0].sheet == spec(1.5, 2.5, 0.33));
    CHECK(loaded->features[1].kind == Feature::Kind::Flange);
    CHECK(loaded->features[1].flange == f.flange && loaded->features[1].flip);
}

void testDxfForTheLaser()
{
    // A plate with a flange and a hole through it.
    Model m = plate();
    m.add(flangeOn(kFarTop, gp::DX(), 20.0));
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.faces = {{gp_Pnt(50, 30, kT), gp::DZ()}};
    hole.points = {gp_Pnt(50, 30, kT)};
    hole.hole.diameter = 8.0;
    m.add(hole);
    const RebuildResult r = m.build();
    CHECK(r.errors.at(2).empty());
    const FlatPattern f = flatOf(r);
    CHECK(f.ok());

    // The outline read back: one region, the flat sheet less the hole.
    Sketch outline;
    QString error;
    const auto curves = importDxf(outline, flatPatternToDxf(f, false), &error);
    CHECK(curves.has_value());
    const std::vector<TopoDS_Face> regions = findRegions(outline);
    const double length = 60.0 + allowance(90, kR, kT) + 17.0;
    double area = 0.0;
    for (const TopoDS_Face& region : regions)
        area = std::max(area, [&] {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(region, props);
            return props.Mass();
        }());
    CHECK(near(area, 100.0 * length - kPi * 16.0, 1e-2));

    // With the bend: its line on its own layer, and its note.
    const QString dxf = QString::fromUtf8(flatPatternToDxf(f));
    CHECK(dxf.count(QStringLiteral("\nBEND\n")) == 2); // the line and the note
    CHECK(dxf.contains(QStringLiteral("UP 90%%d R1")));
}

void testFlatDrawingView()
{
    Model m = plate();
    m.add(flangeOn(kFarTop, gp::DX(), 20.0));
    const RebuildResult r = m.build();
    const FlatPattern f = flatOf(r);
    CHECK(f.ok());
    SheetView view;
    view.kind = ViewKind::Flat;
    // Two copies side by side, 10 apart: both outlines, both bends.
    const ViewGeometry g = projectFlat({f, f}, view);
    CHECK(!g.visible.empty() && g.hidden.empty());
    CHECK(g.bends.size() == 2);
    CHECK(near(g.max.X() - g.min.X(), 2.0 * f.width + 10.0, 1e-3));
    CHECK(near(g.max.Y() - g.min.Y(), f.height, 1e-3));
    if (g.bends.size() == 2) {
        CHECK(near(g.bends[1].a.X() - g.bends[0].a.X(), f.width + 10.0, 1e-6));
        CHECK(near(g.bends[0].degrees, 90.0, 1e-6) && g.bends[0].up);
    }
}

// Done when (M18): a bent bracket with flanges and holes unfolds to a DXF that a laser
// shop can cut and bend to the right size. A Z of 2 mm sheet, bent R2: an 80 × 40 web,
// one flange up and one down, 30 outside, with holes in the web and in a flange.
void testBracketForTheLaserShop()
{
    constexpr double t = 2.0, r = 2.0;
    Model m;
    rectangle(m.sketch(1, gp_Ax3(gp::Origin(), gp::DZ(), gp::DX())), 0, 0, 80, 40);
    Feature base;
    base.kind = Feature::Kind::SheetBase;
    base.regions = {{1, {40, 20}}};
    base.sheet = spec(t, r);
    m.add(base);
    m.add(flangeOn(gp_Pnt(40, 40, t), gp::DX(), 30.0));
    Feature down = flangeOn(gp_Pnt(40, 0, t), gp::DX(), 30.0);
    down.flip = true;
    m.add(down);
    Feature webHoles;
    webHoles.kind = Feature::Kind::Hole;
    webHoles.faces = {{gp_Pnt(40, 20, t), gp::DZ()}};
    webHoles.points = {gp_Pnt(20, 20, t), gp_Pnt(60, 20, t)};
    webHoles.hole.diameter = 6.0;
    m.add(webHoles);
    Feature flangeHole;
    flangeHole.kind = Feature::Kind::Hole;
    flangeHole.faces = {{gp_Pnt(40, 40 + r + t, 17), gp::DY()}};
    flangeHole.points = {gp_Pnt(40, 40 + r + t, 20)};
    flangeHole.hole.diameter = 5.0;
    m.add(flangeHole);
    const RebuildResult built = m.build();
    for (std::size_t i = 0; i < built.errors.size(); ++i)
        CHECK(built.errors[i].empty());
    CHECK(built.bodies.size() == 1);
    // As drawn: 30 outside up and down from the web.
    const Box b = boxOf(built.bodies.at(0));
    CHECK(near(b.z1, 30.0, 1e-4) && near(b.z0, t - 30.0, 1e-4));

    const FlatPattern f = flatOf(built);
    CHECK(f.ok());
    CHECK(f.bends.size() == 2);
    const double straight = 30.0 - r - t;
    const double length = 40.0 + 2.0 * allowance(90, r, t) + 2.0 * straight;
    CHECK(near(flatSize(f).first, length, 1e-4) && near(flatSize(f).second, 80.0, 1e-4));
    // One bend up, one down.
    int up = 0;
    for (const FlatBend& bend : f.bends)
        up += bend.up ? 1 : 0;
    CHECK(up == 1);

    // The DXF: the outline and three round holes, the bends on their own layer.
    const QByteArray dxf = flatPatternToDxf(f);
    CHECK(QString::fromUtf8(dxf).count(QStringLiteral("\nCIRCLE\n")) == 3);
    Sketch outline;
    QString error;
    CHECK(importDxf(outline, flatPatternToDxf(f, false), &error).has_value());
    double largest = 0.0;
    for (const TopoDS_Face& region : findRegions(outline)) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(region, props);
        largest = std::max(largest, props.Mass());
    }
    CHECK(near(largest, 80.0 * length - kPi * (9.0 + 9.0 + 6.25), 1e-2));

    if (const char* path = std::getenv("NIBFORM_SAVE_BRACKET")) {
        ProjectData project;
        for (const auto& [id, sketch] : m.sketches)
            project.sketches.push_back({id, sketch});
        project.features = m.history;
        CHECK(saveProject(QString::fromLocal8Bit(path), project).isEmpty());
    }
    if (const char* path = std::getenv("NIBFORM_SAVE_BRACKET_DXF"))
        CHECK(exportFlatPatternDxf(f, QString::fromLocal8Bit(path)).isEmpty());
}

} // namespace

int main()
{
    std::setlocale(LC_NUMERIC, "C");
    return runTests({
        {"flat plate", testFlatPlate},
        {"edge flange", testEdgeFlange},
        {"flange inside, with reliefs", testFlangeInsideWithReliefs},
        {"hem", testHem},
        {"base flange along a path", testContourBaseFlange},
        {"tab and corner relief", testTabAndRelief},
        {"shelled box converted", testConvertShelledBox},
        {"follows the rules' parameters", testFollowsTheRules},
        {"saved and loaded", testSavedAndLoaded},
        {"DXF for the laser", testDxfForTheLaser},
        {"flat pattern drawing view", testFlatDrawingView},
        {"a bracket for the laser shop", testBracketForTheLaserShop},
    });
}
