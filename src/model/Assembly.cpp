#include "model/Assembly.h"

#include "model/Features.h"
#include "model/Solids.h"
#include "model/Tr.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ElCLib.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>

#include <algorithm>
#include <deque>
#include <functional>
#include <tuple>

namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double degrees)
{
    return degrees * kPi / 180.0;
}

gp_Trsf turn(const gp_Dir& axis, double degrees)
{
    gp_Trsf t;
    t.SetRotation(gp_Ax1(gp::Origin(), axis), radians(degrees));
    return t;
}

gp_Trsf slide(double x, double y, double z)
{
    gp_Trsf t;
    t.SetTranslation(gp_Vec(x, y, z));
    return t;
}

// Turns `b`'s frame round so its z faces `a`'s (unless flipped): flush faces.
gp_Trsf facing(const Joint& joint)
{
    return joint.flip ? gp_Trsf() : turn(gp::DX(), 180.0);
}

gp_Trsf placementOf(const Component& c)
{
    if (const auto t = trsfOf(c.matrix))
        return *t;
    return {};
}

gp_Pnt surfaceMiddle(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

} // namespace

const ComponentSource* Assembly::source(int id) const
{
    const auto it = std::find_if(sources.begin(), sources.end(), [id](const ComponentSource& s) { return s.id == id; });
    return it == sources.end() ? nullptr : &*it;
}

const Component* Assembly::component(int id) const
{
    const int i = componentIndex(id);
    return i < 0 ? nullptr : &components[i];
}

int Assembly::componentIndex(int id) const
{
    for (int i = 0; i < static_cast<int>(components.size()); ++i) {
        if (components[i].id == id)
            return i;
    }
    return -1;
}

int degreesOfFreedom(JointKind kind)
{
    switch (kind) {
    case JointKind::Rigid: return 0;
    case JointKind::Revolute:
    case JointKind::Slider: return 1;
    case JointKind::Cylindrical: return 2;
    case JointKind::Ball:
    case JointKind::Planar: return 3;
    }
    return 0;
}

bool isAngle(JointKind kind, int value)
{
    switch (kind) {
    case JointKind::Revolute:
    case JointKind::Ball: return true;
    case JointKind::Cylindrical: return value == 0;
    case JointKind::Planar: return value == 2;
    default: return false;
    }
}

gp_Trsf jointMotion(JointKind kind, const std::array<double, 3>& v)
{
    switch (kind) {
    case JointKind::Rigid: return {};
    case JointKind::Revolute: return turn(gp::DZ(), v[0]);
    case JointKind::Slider: return slide(0, 0, v[0]);
    case JointKind::Cylindrical: return slide(0, 0, v[1]) * turn(gp::DZ(), v[0]);
    case JointKind::Ball: return turn(gp::DZ(), v[2]) * turn(gp::DY(), v[1]) * turn(gp::DX(), v[0]);
    case JointKind::Planar: return slide(v[0], v[1], 0) * turn(gp::DZ(), v[2]);
    }
    return {};
}

std::array<double, 3> clampedValues(const Joint& joint)
{
    std::array<double, 3> v = joint.values;
    for (int i = 0; i < 3; ++i) {
        if (joint.limited[i])
            v[i] = std::clamp(v[i], std::min(joint.min[i], joint.max[i]), std::max(joint.min[i], joint.max[i]));
    }
    return v;
}

gp_Trsf frameTrsf(const gp_Ax3& frame)
{
    gp_Trsf toLocal;
    toLocal.SetTransformation(frame); // world coordinates into the frame's
    return toLocal.Inverted();
}

AssemblySolution solveAssembly(const Assembly& assembly)
{
    const int count = static_cast<int>(assembly.components.size());
    AssemblySolution out;
    out.placements.resize(count);
    out.placedBy.assign(count, -1);
    out.placedAsB.assign(count, false);
    out.jointErrors.assign(assembly.joints.size(), {});
    std::vector<bool> placed(count, false);
    std::vector<bool> done(assembly.joints.size(), false);
    for (int i = 0; i < count; ++i)
        out.placements[i] = placementOf(assembly.components[i]);

    // Joints that can't apply: an end that isn't there, or both ends on one component.
    for (std::size_t j = 0; j < assembly.joints.size(); ++j) {
        const Joint& joint = assembly.joints[j];
        const int a = assembly.componentIndex(joint.a.component), b = assembly.componentIndex(joint.b.component);
        if (a < 0 || b < 0) {
            out.jointErrors[j] = QT_TRANSLATE_NOOP("Model", "A part this joint holds is gone");
            done[j] = true;
        } else if (a == b) {
            out.jointErrors[j] = QT_TRANSLATE_NOOP("Model", "A joint needs two different parts");
            done[j] = true;
        }
    }

    // Which joints place parts: in the order they were made, each one that joins two groups
    // not yet joined (the grounded parts count as one group). A later joint closing a loop
    // is left out.
    std::vector<int> group(count + 1); // the last: the ground
    for (int i = 0; i <= count; ++i)
        group[i] = i;
    auto root = [&](int i) {
        while (group[i] != i)
            i = group[i] = group[group[i]];
        return i;
    };
    for (int i = 0; i < count; ++i) {
        if (assembly.components[i].grounded)
            group[root(i)] = root(count);
    }
    for (std::size_t j = 0; j < assembly.joints.size(); ++j) {
        if (done[j])
            continue;
        const int a = root(assembly.componentIndex(assembly.joints[j].a.component));
        const int b = root(assembly.componentIndex(assembly.joints[j].b.component));
        if (a == b) {
            out.jointErrors[j] =
                QT_TRANSLATE_NOOP("Model", "This joint closes a loop or joins two grounded parts: it is left out");
            done[j] = true;
        } else {
            group[a] = b;
        }
    }

    std::deque<int> queue;
    auto spread = [&] {
        while (!queue.empty()) {
            const int from = queue.front();
            queue.pop_front();
            for (std::size_t j = 0; j < assembly.joints.size(); ++j) {
                if (done[j])
                    continue;
                const Joint& joint = assembly.joints[j];
                const int a = assembly.componentIndex(joint.a.component), b = assembly.componentIndex(joint.b.component);
                if (a != from && b != from)
                    continue;
                done[j] = true;
                const int other = a == from ? b : a;
                // b = a · frame a · motion · facing · (frame b)⁻¹, or the other way round.
                const gp_Trsf link = frameTrsf(joint.a.frame) * jointMotion(joint.kind, clampedValues(joint))
                                   * facing(joint) * frameTrsf(joint.b.frame).Inverted();
                out.placements[other] = a == from ? out.placements[a] * link : out.placements[b] * link.Inverted();
                out.placedBy[other] = static_cast<int>(j);
                out.placedAsB[other] = a == from;
                placed[other] = true;
                queue.push_back(other);
            }
        }
    };

    bool anyGrounded = false;
    for (int i = 0; i < count; ++i) {
        if (assembly.components[i].grounded) {
            placed[i] = true;
            queue.push_back(i);
            anyGrounded = true;
        }
    }
    if (!anyGrounded && count > 0) {
        placed[0] = true;
        queue.push_back(0);
    }
    spread();
    // Groups no grounded part reaches hang from their first part, where it sits.
    for (int i = 0; i < count; ++i) {
        if (!placed[i]) {
            placed[i] = true;
            queue.push_back(i);
            spread();
        }
    }
    return out;
}

std::optional<gp_Ax3> jointFrame(const TopoDS_Shape& picked)
{
    if (picked.IsNull())
        return std::nullopt;
    switch (picked.ShapeType()) {
    case TopAbs_VERTEX:
        return gp_Ax3(BRep_Tool::Pnt(TopoDS::Vertex(picked)), gp::DZ());
    case TopAbs_EDGE: {
        const BRepAdaptor_Curve curve(TopoDS::Edge(picked));
        if (curve.GetType() == GeomAbs_Circle)
            return gp_Ax3(curve.Circle().Location(), curve.Circle().Axis().Direction());
        if (curve.GetType() == GeomAbs_Line) {
            const gp_Pnt middle = curve.Value((curve.FirstParameter() + curve.LastParameter()) / 2.0);
            return gp_Ax3(middle, curve.Line().Direction());
        }
        return std::nullopt;
    }
    case TopAbs_FACE: {
        const TopoDS_Face face = TopoDS::Face(picked);
        const BRepAdaptor_Surface surface(face);
        const gp_Pnt middle = surfaceMiddle(face);
        auto onAxis = [&](const gp_Ax1& axis) {
            // The face's middle, brought onto the axis: where along it the face is.
            const gp_Lin line(axis);
            return gp_Ax3(ElCLib::Value(ElCLib::Parameter(line, middle), line), axis.Direction());
        };
        switch (surface.GetType()) {
        case GeomAbs_Plane:
            if (const auto normal = planarNormal(face))
                return gp_Ax3(middle, *normal);
            return std::nullopt;
        case GeomAbs_Cylinder: return onAxis(surface.Cylinder().Axis());
        case GeomAbs_Cone: return onAxis(surface.Cone().Axis());
        case GeomAbs_Sphere: return gp_Ax3(surface.Sphere().Location(), gp::DZ());
        default: return std::nullopt;
        }
    }
    default:
        return std::nullopt;
    }
}

gp_Ax3 localFrame(const gp_Ax3& world, const gp_Trsf& placement)
{
    return world.Transformed(placement.Inverted());
}

std::map<int, int> bodySources(const Assembly& assembly, const std::vector<TopoDS_Shape>& bodies)
{
    std::map<int, int> out;
    for (const ComponentSource& source : assembly.sources) {
        if (source.kind != ComponentSource::Kind::Body)
            continue;
        if (const auto body = bodyContaining(bodies, source.inside))
            out[source.id] = *body;
    }
    return out;
}

std::vector<TopoDS_Shape> placedShapes(const Assembly& assembly, const std::vector<gp_Trsf>& placements,
                                       const std::map<int, TopoDS_Shape>& shapes)
{
    std::vector<TopoDS_Shape> out(assembly.components.size());
    for (std::size_t i = 0; i < assembly.components.size() && i < placements.size(); ++i) {
        const Component& c = assembly.components[i];
        const auto it = shapes.find(c.source);
        if (c.hidden || it == shapes.end() || it->second.IsNull())
            continue;
        gp_Trsf where = placements[i];
        if (assembly.exploded) {
            if (const auto e = assembly.explode.find(c.id); e != assembly.explode.end()) {
                gp_Trsf apart;
                apart.SetTranslation(e->second);
                where = apart * where;
            }
        }
        out[i] = it->second.Moved(TopLoc_Location(where));
    }
    return out;
}

std::map<int, gp_Vec> explodedOffsets(const Assembly& assembly, const std::map<int, TopoDS_Shape>& shapes)
{
    Assembly together = assembly;
    together.exploded = false;
    const AssemblySolution s = solveAssembly(together);
    const std::vector<TopoDS_Shape> placed = placedShapes(together, s.placements, shapes);
    const int count = static_cast<int>(assembly.components.size());
    // Where a part spans along a direction (its box's corners), moved by `offset`.
    auto span = [&](int i, const gp_Dir& d, const gp_Vec& offset) {
        Bnd_Box box;
        if (!placed[i].IsNull())
            BRepBndLib::Add(placed[i], box);
        if (box.IsVoid())
            return std::pair(0.0, 0.0);
        double lo = 1e100, hi = -1e100;
        const gp_Pnt a = box.CornerMin(), b = box.CornerMax();
        for (int k = 0; k < 8; ++k) {
            const gp_Pnt p(k & 1 ? b.X() : a.X(), k & 2 ? b.Y() : a.Y(), k & 4 ? b.Z() : a.Z());
            const double t = (gp_Vec(p.XYZ()) + offset).Dot(gp_Vec(d));
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
        return std::pair(lo, hi);
    };
    // Each part slides along its joint's axis until it clears its parent and the parts
    // already slid out of that parent the same way; its own children go with it.
    std::vector<std::optional<gp_Vec>> offsets(count);
    std::map<std::pair<int, std::array<long, 3>>, double> reached; // by parent and direction
    std::function<gp_Vec(int)> offsetOf = [&](int i) -> gp_Vec {
        if (offsets[i])
            return *offsets[i];
        offsets[i] = gp_Vec(); // a part reached again (no loops are solved) stays
        const int j = s.placedBy[i];
        if (j < 0)
            return gp_Vec();
        const Joint& joint = assembly.joints[j];
        const int parent = assembly.componentIndex(s.placedAsB[i] ? joint.a.component : joint.b.component);
        gp_Dir d = (s.placedAsB[i] ? joint.a.frame : joint.b.frame).Transformed(s.placements[parent]).Direction();
        const gp_Vec base = offsetOf(parent);
        auto [lo, hi] = span(i, d, base);
        auto [parentLo, parentHi] = span(parent, d, base);
        // Out the way it sticks out more (a pin through a hole: whichever end is nearer).
        if (lo - parentLo < parentHi - hi) {
            d.Reverse();
            std::tie(lo, hi) = span(i, d, base);
            std::tie(parentLo, parentHi) = span(parent, d, base);
        }
        const std::array<long, 3> key{std::lround(d.X() * 1e3), std::lround(d.Y() * 1e3), std::lround(d.Z() * 1e3)};
        double& frontier = reached.try_emplace({parent, key}, parentHi).first->second;
        const double gap = std::max(5.0, 0.15 * (hi - lo));
        const double shift = std::max(0.0, frontier + gap - lo);
        frontier = hi + shift;
        offsets[i] = base + gp_Vec(d) * shift;
        return *offsets[i];
    };
    std::map<int, gp_Vec> out;
    for (int i = 0; i < count; ++i) {
        const gp_Vec v = offsetOf(i);
        if (v.Magnitude() > 1e-9)
            out[assembly.components[i].id] = v;
    }
    return out;
}

std::vector<Interference> interferences(const std::vector<TopoDS_Shape>& placed, double minVolume)
{
    std::vector<Interference> out;
    for (int i = 0; i < static_cast<int>(placed.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(placed.size()); ++j) {
            if (placed[i].IsNull() || placed[j].IsNull() || apart(placed[i], placed[j]))
                continue;
            const auto common = commonOf(placed[i], placed[j]);
            if (!common)
                continue;
            const double volume = volumeOf(*common);
            if (volume > minVolume)
                out.push_back({i, j, volume, *common});
        }
    }
    return out;
}

double clearance(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    if (a.IsNull() || b.IsNull())
        return 0.0;
    if (overlap(a, b) > 0.0)
        return 0.0;
    BRepExtrema_DistShapeShape distance(a, b);
    return distance.IsDone() ? distance.Value() : 0.0;
}

std::optional<SweepHit> sweepJoint(const Assembly& assembly, const std::map<int, TopoDS_Shape>& shapes, int joint,
                                   int value, int steps)
{
    if (joint < 0 || joint >= static_cast<int>(assembly.joints.size()) || value < 0
        || value >= degreesOfFreedom(assembly.joints[joint].kind) || steps < 1)
        return std::nullopt;
    Assembly moving = assembly;
    moving.exploded = false;
    Joint& j = moving.joints[joint];
    double from = isAngle(j.kind, value) ? 0.0 : -50.0;
    double to = isAngle(j.kind, value) ? 360.0 : 50.0;
    if (j.limited[value]) {
        from = std::min(j.min[value], j.max[value]);
        to = std::max(j.min[value], j.max[value]);
    }
    for (int k = 0; k <= steps; ++k) {
        j.values[value] = from + (to - from) * k / steps;
        const AssemblySolution solution = solveAssembly(moving);
        const auto hits = interferences(placedShapes(moving, solution.placements, shapes));
        if (!hits.empty())
            return SweepHit{j.values[value], hits.front()};
    }
    return std::nullopt;
}
