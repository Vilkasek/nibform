// Assemblies (M17): components placed by joints, interference and sweeping a joint.

#include "Check.h"

#include "io/ComponentFiles.h"
#include "io/ProjectFile.h"
#include "model/Assembly.h"
#include "model/Drawing.h"
#include "model/Export.h"
#include "model/Measure.h"
#include "model/Features.h"
#include "model/Solids.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>

namespace {

using ::near;

bool near(const gp_Pnt& a, const gp_Pnt& b, double eps = 1e-7)
{
    return a.Distance(b) <= eps;
}

gp_Pnt placed(const AssemblySolution& s, int component, const gp_Pnt& local)
{
    return local.Transformed(s.placements.at(component));
}

Component component(int id, int source, bool grounded = false)
{
    Component c;
    c.id = id;
    c.source = source;
    c.grounded = grounded;
    return c;
}

// Two 10 mm cubes (sources 1 and 2); the second joined onto the top of the first: its
// bottom face's middle on the first's top face's middle.
Assembly stacked(JointKind kind)
{
    Assembly a;
    a.sources = {{ComponentSource::Kind::Body, 1, gp_Pnt(5, 5, 5), {}, {}, "A"},
                 {ComponentSource::Kind::Body, 2, gp_Pnt(5, 5, 5), {}, {}, "B"}};
    a.components = {component(1, 1, true), component(2, 2)};
    Joint j;
    j.id = 1;
    j.kind = kind;
    j.a = {1, gp_Ax3(gp_Pnt(5, 5, 10), gp::DZ(), gp::DX())};
    j.b = {2, gp_Ax3(gp_Pnt(5, 5, 0), -gp::DZ(), gp::DX())};
    a.joints = {j};
    return a;
}

std::map<int, TopoDS_Shape> cubes()
{
    return {{1, BRepPrimAPI_MakeBox(10, 10, 10).Shape()}, {2, BRepPrimAPI_MakeBox(10, 10, 10).Shape()}};
}

void testFrames()
{
    const gp_Ax3 frame(gp_Pnt(1, 2, 3), gp::DX(), gp::DY());
    const gp_Trsf t = frameTrsf(frame);
    CHECK(near(gp::Origin().Transformed(t), gp_Pnt(1, 2, 3)));
    CHECK(near(gp_Pnt(0, 0, 1).Transformed(t), gp_Pnt(2, 2, 3))); // z goes along X
    CHECK(near(gp_Pnt(1, 0, 0).Transformed(t), gp_Pnt(1, 3, 3))); // x along Y
    // A world frame brought into a moved component's coordinates and back.
    gp_Trsf where;
    where.SetTranslation(gp_Vec(10, 0, 0));
    const gp_Ax3 local = localFrame(frame, where);
    CHECK(near(local.Location(), gp_Pnt(-9, 2, 3)));
}

void testRigidAndSlider()
{
    const AssemblySolution s = solveAssembly(stacked(JointKind::Rigid));
    CHECK(s.jointErrors.at(0).empty());
    // B sits on A: its middle at z 15, its bottom face on A's top.
    CHECK(near(placed(s, 1, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, 15)));
    CHECK(near(placed(s, 0, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, 5)));
    const auto shapes = placedShapes(stacked(JointKind::Rigid), s.placements, cubes());
    CHECK(interferences(shapes).empty());
    CHECK(near(clearance(shapes[0], shapes[1]), 0.0, 1e-6));

    Assembly slider = stacked(JointKind::Slider);
    slider.joints[0].values[0] = 3.0; // lifted 3 off A
    const AssemblySolution lifted = solveAssembly(slider);
    CHECK(near(placed(lifted, 1, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, 18)));
    const auto apart = placedShapes(slider, lifted.placements, cubes());
    CHECK(near(clearance(apart[0], apart[1]), 3.0, 1e-6));
}

void testRevoluteAndLimits()
{
    Assembly a = stacked(JointKind::Revolute);
    const gp_Pnt corner(9, 5, 5); // 4 from the axis through (5, 5)
    const gp_Pnt at0 = placed(solveAssembly(a), 1, corner);
    a.joints[0].values[0] = 90.0;
    const gp_Pnt at90 = placed(solveAssembly(a), 1, corner);
    const gp_Vec r0(gp_Pnt(5, 5, at0.Z()), at0), r90(gp_Pnt(5, 5, at90.Z()), at90);
    CHECK(near(at0.Z(), 15.0, 1e-7) && near(at90.Z(), 15.0, 1e-7));
    CHECK(near(r0.Magnitude(), 4.0, 1e-7) && near(r90.Magnitude(), 4.0, 1e-7));
    CHECK(near(r0.Dot(r90), 0.0, 1e-7)); // a quarter turn about the axis
    // Limited to 0..45: 90 stops at 45.
    a.joints[0].limited[0] = true;
    a.joints[0].min[0] = 0.0;
    a.joints[0].max[0] = 45.0;
    const gp_Pnt at45 = placed(solveAssembly(a), 1, corner);
    const gp_Vec r45(gp_Pnt(5, 5, at45.Z()), at45);
    CHECK(near(r0.Angle(r45), M_PI / 4.0, 1e-7));
}

void testFlipAndInterference()
{
    // Flipped, the frames face the same way: B turns over, down into A.
    Assembly a = stacked(JointKind::Rigid);
    a.joints[0].flip = true;
    const AssemblySolution s = solveAssembly(a);
    CHECK(near(placed(s, 1, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, 5)));
    const auto hits = interferences(placedShapes(a, s.placements, cubes()));
    CHECK(hits.size() == 1 && hits[0].a == 0 && hits[0].b == 1);
    CHECK(!hits.empty() && near(hits[0].volume, 1000.0, 1e-3));
}

void testTreesLoopsAndFreeParts()
{
    // A chain A ← B ← C: turning B carries C.
    Assembly a = stacked(JointKind::Revolute);
    a.sources.push_back({ComponentSource::Kind::Body, 3, gp_Pnt(5, 5, 5), {}, {}, "C"});
    a.components.push_back(component(3, 3));
    Joint bc;
    bc.id = 2;
    bc.kind = JointKind::Rigid;
    bc.a = {2, gp_Ax3(gp_Pnt(5, 5, 10), gp::DZ(), gp::DX())};
    bc.b = {3, gp_Ax3(gp_Pnt(5, 5, 0), -gp::DZ(), gp::DX())};
    a.joints.push_back(bc);
    AssemblySolution s = solveAssembly(a);
    CHECK(near(placed(s, 2, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, 25)));
    const gp_Pnt before = placed(s, 2, gp_Pnt(9, 5, 5));
    a.joints[0].values[0] = 180.0;
    s = solveAssembly(a);
    const gp_Pnt after = placed(s, 2, gp_Pnt(9, 5, 5));
    CHECK(near(before.Distance(after), 8.0, 1e-7)); // half a turn about the axis 4 away

    // The same joint written the other way round (the grounded part as `b`).
    Assembly reversed = stacked(JointKind::Rigid);
    std::swap(reversed.joints[0].a, reversed.joints[0].b);
    reversed.components[0].grounded = false;
    reversed.components[1].grounded = true; // B stays, A is placed under it
    const AssemblySolution r = solveAssembly(reversed);
    CHECK(near(placed(r, 0, gp_Pnt(5, 5, 5)), gp_Pnt(5, 5, -5)));

    // C joined back to A closes a loop: that joint is left out.
    Joint ca = bc;
    ca.id = 3;
    ca.a = {3, bc.a.frame};
    ca.b = {1, bc.b.frame};
    a.joints.push_back(ca);
    s = solveAssembly(a);
    CHECK(s.jointErrors[0].empty() && s.jointErrors[1].empty() && !s.jointErrors[2].empty());

    // Two grounded parts can't be joined; a part with no joint stays where it was put.
    Assembly two = stacked(JointKind::Rigid);
    two.components[1].grounded = true;
    two.components[1].matrix = {1, 0, 0, 100, 0, 1, 0, 0, 0, 0, 1, 0};
    s = solveAssembly(two);
    CHECK(!s.jointErrors[0].empty());
    CHECK(near(placed(s, 1, gp::Origin()), gp_Pnt(100, 0, 0)));

    // A joint to a part that is gone.
    Assembly gone = stacked(JointKind::Rigid);
    gone.components.pop_back();
    CHECK(!solveAssembly(gone).jointErrors[0].empty());
}

void testJointFrames()
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), 3.0, 10.0).Shape();
    bool round = false, rim = false, flat = false;
    for (TopExp_Explorer it(cylinder, TopAbs_FACE); it.More(); it.Next()) {
        const auto frame = jointFrame(it.Current());
        CHECK(frame.has_value());
        if (!frame)
            continue;
        if (std::abs(frame->Direction().Z()) > 0.99 && near(frame->Location().X(), 0.0, 1e-7)
            && near(frame->Location().Y(), 0.0, 1e-7)) {
            if (near(frame->Location().Z(), 5.0, 1e-7))
                round = true; // the round face: its axis, at its middle
            else if (near(frame->Location().Z(), 10.0, 1e-7) && frame->Direction().Z() > 0.0)
                flat = true; // the top: outward
        }
    }
    for (TopExp_Explorer it(cylinder, TopAbs_EDGE); it.More(); it.Next()) {
        if (const auto frame = jointFrame(it.Current()); frame && near(frame->Location(), gp_Pnt(0, 0, 10)))
            rim = true;
    }
    CHECK(round && flat && rim);
}

void testSweep()
{
    // An arm (20 × 2 × 2) turning on top of a block, and a post in its way.
    Assembly a;
    a.sources = {{ComponentSource::Kind::Body, 1, gp_Pnt(5, 5, 5), {}, {}, "Block"},
                 {ComponentSource::Kind::Body, 2, gp_Pnt(10, 1, 1), {}, {}, "Arm"},
                 {ComponentSource::Kind::Body, 3, gp_Pnt(1, 1, 1), {}, {}, "Post"}};
    a.components = {component(1, 1, true), component(2, 2), component(3, 3, true)};
    a.components[2].matrix = {1, 0, 0, 4, 0, 1, 0, 14, 0, 0, 1, 10.5}; // 2 × 2 × 1 at y 14..16, z 10.5..11.5
    Joint j;
    j.id = 1;
    j.kind = JointKind::Revolute;
    j.a = {1, gp_Ax3(gp_Pnt(5, 5, 10), gp::DZ(), gp::DX())};
    j.b = {2, gp_Ax3(gp_Pnt(1, 1, 0), -gp::DZ(), gp::DX())};
    a.joints = {j};
    const std::map<int, TopoDS_Shape> shapes{{1, BRepPrimAPI_MakeBox(10, 10, 10).Shape()},
                                             {2, BRepPrimAPI_MakeBox(20, 2, 2).Shape()},
                                             {3, BRepPrimAPI_MakeBox(2, 2, 1).Shape()}};
    const auto hit = sweepJoint(a, shapes, 0, 0, 72);
    CHECK(hit.has_value());
    if (!hit)
        return;
    CHECK(hit->at >= 0.0 && hit->at <= 360.0);
    CHECK(hit->what.volume > 0.0 && ((hit->what.a == 1 && hit->what.b == 2)));
    // Kept to a range facing away from the post, it turns freely.
    a.joints[0].limited[0] = true;
    a.joints[0].min[0] = hit->at + 120.0;
    a.joints[0].max[0] = hit->at + 240.0;
    CHECK(!sweepJoint(a, shapes, 0, 0, 24).has_value());
}

// --- The hinge: what M17 is done by ----------------------------------------------------------

TopoDS_Shape box(double x0, double y0, double z0, double x1, double y1, double z1)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape();
}

// A cylinder about the Y axis (the hinge's), from y0 to y1.
TopoDS_Shape alongY(double radius, double y0, double y1)
{
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, y0, 0), gp::DY()), radius, y1 - y0).Shape();
}

TopoDS_Shape fused(std::initializer_list<TopoDS_Shape> shapes)
{
    TopoDS_Shape out;
    for (const TopoDS_Shape& s : shapes)
        out = out.IsNull() ? s : BRepAlgoAPI_Fuse(out, s).Shape();
    return out;
}

struct Hinge
{
    TopoDS_Shape leafA, leafB, pin;
};

// Two leaves turning about the Y axis and a pin through their knuckles (r 4, holes r 2.2,
// the pin r 2). A's knuckles at y 0..10 and 20..30 and its plate (z −4..−1) all along; B's
// knuckle between them, `gap` from each, and its plate to the other side. Turning B "up"
// (positive angles) folds it over A; the other way it runs into A's plate.
Hinge hinge(double gap = 0.5)
{
    Hinge h;
    const TopoDS_Shape holeA = alongY(2.2, -1, 31), holeB = alongY(2.2, -1, 31);
    h.leafA = BRepAlgoAPI_Cut(fused({alongY(4, 0, 10), alongY(4, 20, 30), box(0, 0, -4, 40, 10, -1),
                                     box(0, 20, -4, 40, 30, -1), box(4.5, 0, -4, 40, 30, -1)}),
                              holeA)
                  .Shape();
    h.leafB = BRepAlgoAPI_Cut(fused({alongY(4, 10 + gap, 20 - gap), box(-40, 10 + gap, -4, 0, 20 - gap, -1)}), holeB).Shape();
    h.pin = alongY(2, 0, 30);
    return h;
}

Feature importOf(const TopoDS_Shape& shape, const char* label)
{
    Feature f;
    f.kind = Feature::Kind::Import;
    f.shape = std::make_shared<const std::string>(shapeToText(shape));
    f.label = label;
    return f;
}

// The hinge's parts as bodies of a project (imported into its history), put together.
ProjectData hingeProject(const Hinge& h)
{
    ProjectData project;
    project.features = {importOf(h.leafA, "leaf-a.step"), importOf(h.leafB, "leaf-b.step"), importOf(h.pin, "pin.step")};
    numberFeatures(project.features);
    Assembly& a = project.assembly;
    a.sources = {{ComponentSource::Kind::Body, 1, *insidePoint(h.leafA), {}, {}, "Leaf A"},
                 {ComponentSource::Kind::Body, 2, *insidePoint(h.leafB), {}, {}, "Leaf B"},
                 {ComponentSource::Kind::Body, 3, *insidePoint(h.pin), {}, {}, "Pin"}};
    a.components = {component(1, 1, true), component(2, 2), component(3, 3)};
    // Frames on the axis, as a round edge of each would give them; the same way round.
    const gp_Ax3 axis(gp::Origin(), gp::DY(), gp::DX());
    Joint pin;
    pin.id = 1;
    pin.kind = JointKind::Rigid;
    pin.a = {1, axis};
    pin.b = {3, axis};
    pin.flip = true;
    Joint turn = pin;
    turn.id = 2;
    turn.kind = JointKind::Revolute;
    turn.b = {2, axis};
    turn.limited = {true, false, false};
    turn.min = {0, 0, 0};
    turn.max = {180, 0, 0};
    a.joints = {pin, turn};
    return project;
}

std::map<int, TopoDS_Shape> sourcesOf(const ProjectData& project)
{
    std::map<int, Sketch> none;
    const RebuildResult built = rebuild([&](int) -> const Sketch* { return nullptr; }, project.features);
    std::map<int, TopoDS_Shape> shapes;
    for (const auto& [source, body] : bodySources(project.assembly, built.bodies))
        shapes[source] = built.bodies[body];
    return shapes;
}

void testHinge()
{
    const ProjectData project = hingeProject(hinge());
    const std::map<int, TopoDS_Shape> shapes = sourcesOf(project);
    CHECK(shapes.size() == 3); // each part found in the rebuilt bodies
    const Assembly& a = project.assembly;
    const AssemblySolution s = solveAssembly(a);
    CHECK(s.jointErrors[0].empty() && s.jointErrors[1].empty());
    // Put together as modelled: nothing overlaps; the pin sits in both leaves with clearance.
    const auto placed = placedShapes(a, s.placements, shapes);
    CHECK(interferences(placed).empty());
    CHECK(near(clearance(placed[2], placed[1]), 0.2, 1e-3));

    // Opened through its range, 0..180°: no collisions.
    CHECK(!sweepJoint(a, shapes, 1, 0, 36).has_value());
    // At 90° the leaf stands up; at 180° it lies over A.
    Assembly open = a;
    open.joints[1].values[0] = 180.0;
    const gp_Pnt tip = gp_Pnt(-30, 15, -2.5).Transformed(solveAssembly(open).placements[1]);
    CHECK(near(tip, gp_Pnt(30, 15, 2.5), 1e-6));
    // The other way, it runs into A's plate.
    Assembly wrongWay = a;
    wrongWay.joints[1].min = {-180, 0, 0};
    wrongWay.joints[1].max = {0, 0, 0};
    const auto hit = sweepJoint(wrongWay, shapes, 1, 0, 18);
    CHECK(hit.has_value() && hit->what.a == 0 && hit->what.b == 1);
    // Knuckles made too long overlap: found right away.
    const ProjectData tight = hingeProject(hinge(-0.3));
    const auto tightHit = sweepJoint(tight.assembly, sourcesOf(tight), 1, 0, 36);
    CHECK(tightHit.has_value() && near(tightHit->at, 0.0));

    // Exploded: the leaf and the pin slide out along the axis, apart from everything.
    Assembly apart = a;
    apart.explode = explodedOffsets(a, shapes);
    apart.exploded = true;
    CHECK(apart.explode.size() == 2 && apart.explode.count(2) && apart.explode.count(3));
    const auto exploded = placedShapes(apart, solveAssembly(apart).placements, shapes);
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j)
            CHECK(clearance(exploded[i], exploded[j]) > 1.0);
    }

    // The exploded drawing with its parts list.
    const Drawing d = assemblyDrawing(placed, exploded, "Hinge");
    CHECK(d.assembly && d.partsList && d.views.size() == 2 && d.views[1].exploded);
    const auto rows = partsList({{1, "Leaf A"}, {2, "Leaf B"}, {3, "Pin"}});
    CHECK(rows.size() == 3 && rows[2].name == "Pin" && rows[2].quantity == 1);
    const ViewGeometry view = projectView(exploded, d.views[1]);
    std::vector<std::pair<int, gp_Pnt2d>> anchors;
    for (int i = 0; i < 3; ++i)
        anchors.emplace_back(i + 1, projectPoint(ViewKind::Iso, massProperties(exploded[i]).center));
    CHECK(placeBalloons(view, anchors, 10.0).size() == 3);

    // To STEP with its structure, and the project saved and loaded as it was.
    QTemporaryDir dir;
    std::vector<StepPart> parts;
    std::vector<StepInstance> instances;
    for (const auto& [source, shape] : shapes) {
        parts.push_back({shape, a.source(source)->name, std::nullopt});
        instances.push_back({static_cast<int>(parts.size()) - 1, s.placements[a.componentIndex(source)], a.source(source)->name});
    }
    CHECK(exportStepAssembly(parts, instances, "Hinge", (dir.path() + "/hinge.step").toStdString()).empty());
    CHECK(importStep((dir.path() + "/hinge.step").toStdString()).size() == 3);
    const auto loaded = projectFromJson(projectToJson(project));
    CHECK(loaded && loaded->assembly.joints.size() == 2 && loaded->assembly.joints[1].kind == JointKind::Revolute);
    CHECK(loaded && !sweepJoint(loaded->assembly, sourcesOf(*loaded), 1, 0, 12).has_value());

    if (const char* path = std::getenv("NIBFORM_SAVE_HINGE")) // to look at in the app
        CHECK(saveProject(QString::fromLocal8Bit(path), project).isEmpty());
}

// A part from another project file: its shown bodies, read again when the file changes.
void testPartFromAFile()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/pin.nibform");
    ProjectData pin;
    pin.features = {importOf(alongY(2, 0, 30), "pin.step")};
    CHECK(saveProject(path, pin).isEmpty());
    const auto first = projectShape(path);
    CHECK(first && near(volumeOf(*first), M_PI * 4.0 * 30.0, 1e-3));
    // Thicker in the file: the part follows.
    pin.features = {importOf(alongY(3, 0, 30), "pin.step")};
    CHECK(saveProject(path, pin).isEmpty());
    const auto second = projectShape(path);
    CHECK(second && near(volumeOf(*second), M_PI * 9.0 * 30.0, 1e-3));
    // A hidden body isn't part of it; with none shown, or no file, there's nothing (and why).
    BodyStyle hidden;
    hidden.inside = gp_Pnt(0, 15, 0);
    hidden.hidden = true;
    pin.bodyStyles = {hidden};
    CHECK(saveProject(path, pin).isEmpty());
    QString error;
    CHECK(!projectShape(path, &error) && !error.isEmpty());
    QFile::remove(path);
    error.clear();
    CHECK(!projectShape(path, &error) && !error.isEmpty());
}

} // namespace

int main()
{
    return runTests({
        {"frames", testFrames},
        {"rigid and slider joints", testRigidAndSlider},
        {"revolute joints and limits", testRevoluteAndLimits},
        {"flip and interference", testFlipAndInterference},
        {"trees, loops and free parts", testTreesLoopsAndFreeParts},
        {"joint frames of picks", testJointFrames},
        {"sweeping a joint", testSweep},
        {"the hinge", testHinge},
        {"a part from a file", testPartFromAFile},
    });
}
