#include "model/Measure.h"

#include "model/Solids.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

gp_Pnt surfaceCenter(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

// A corner, or the center of a round edge.
std::optional<gp_Pnt> pointOf(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() == TopAbs_VERTEX)
        return BRep_Tool::Pnt(TopoDS::Vertex(shape));
    if (shape.ShapeType() == TopAbs_EDGE) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
        if (curve.GetType() == GeomAbs_Circle)
            return curve.Circle().Location();
    }
    return std::nullopt;
}

// A flat face: a point on it and its outward normal.
std::optional<std::pair<gp_Pnt, gp_Dir>> planeOf(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() != TopAbs_FACE)
        return std::nullopt;
    const auto normal = planarNormal(TopoDS::Face(shape));
    if (!normal)
        return std::nullopt;
    return std::pair(surfaceCenter(shape), *normal);
}

std::optional<gp_Dir> lineOf(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() != TopAbs_EDGE)
        return std::nullopt;
    const BRepAdaptor_Curve curve(TopoDS::Edge(shape));
    if (curve.GetType() != GeomAbs_Line)
        return std::nullopt;
    return curve.Line().Direction();
}

double degrees(double radians)
{
    return radians * 180.0 / kPi;
}

} // namespace

Measurement measure(const TopoDS_Shape& item)
{
    Measurement m;
    switch (item.ShapeType()) {
    case TopAbs_VERTEX:
        m.position = BRep_Tool::Pnt(TopoDS::Vertex(item));
        break;
    case TopAbs_EDGE: {
        const BRepAdaptor_Curve curve(TopoDS::Edge(item));
        m.length = GCPnts_AbscissaPoint::Length(curve);
        if (curve.GetType() == GeomAbs_Circle)
            m.radius = curve.Circle().Radius();
        break;
    }
    case TopAbs_FACE: {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(item, props);
        m.area = props.Mass();
        const BRepAdaptor_Surface surface(TopoDS::Face(item));
        if (surface.GetType() == GeomAbs_Cylinder)
            m.radius = surface.Cylinder().Radius();
        else if (surface.GetType() == GeomAbs_Sphere)
            m.radius = surface.Sphere().Radius();
        break;
    }
    default:
        break;
    }
    return m;
}

Measurement measure(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    Measurement m;
    BRepExtrema_DistShapeShape distance(a, b);
    if (distance.IsDone() && distance.NbSolution() > 0) {
        m.distance = distance.Value();
        m.from = distance.PointOnShape1(1);
        m.to = distance.PointOnShape2(1);
    }
    // Angles between flat faces and straight edges (as lines and planes, 0..90°).
    const auto pa = planeOf(a), pb = planeOf(b);
    const auto la = lineOf(a), lb = lineOf(b);
    if (pa && pb)
        m.angle = degrees(std::acos(std::min(1.0, std::abs(pa->second.Dot(pb->second)))));
    else if (la && lb)
        m.angle = degrees(std::acos(std::min(1.0, std::abs(la->Dot(*lb)))));
    else if ((pa && lb) || (la && pb))
        m.angle = 90.0 - degrees(std::acos(std::min(1.0, std::abs((pa ? pa->second : pb->second).Dot(la ? *la : *lb)))));
    const auto ca = pointOf(a), cb = pointOf(b);
    if (ca && cb)
        m.centers = ca->Distance(*cb);
    return m;
}

MassProperties massProperties(const TopoDS_Shape& body)
{
    MassProperties out;
    GProp_GProps volume;
    BRepGProp::VolumeProperties(body, volume);
    out.volume = std::abs(volume.Mass());
    out.center = volume.CentreOfMass();
    double i1 = 0, i2 = 0, i3 = 0;
    volume.PrincipalProperties().Moments(i1, i2, i3);
    out.inertia = gp_XYZ(std::abs(i1), std::abs(i2), std::abs(i3));
    GProp_GProps surface;
    BRepGProp::SurfaceProperties(body, surface);
    out.area = surface.Mass();
    Bnd_Box box;
    BRepBndLib::AddOptimal(body, box, false, false);
    if (!box.IsVoid())
        out.size = box.CornerMax().XYZ() - box.CornerMin().XYZ();
    return out;
}

std::optional<gp_Trsf> alignment(const TopoDS_Shape& from, const TopoDS_Shape& to)
{
    gp_Trsf move;
    const auto pf = pointOf(from), pt = pointOf(to);
    const auto ff = planeOf(from), ft = planeOf(to);
    if (pf && pt) {
        move.SetTranslation(gp_Vec(*pf, *pt));
        return move;
    }
    if (pf && ft) {
        // Onto the face's plane, straight across.
        move.SetTranslation(gp_Vec(ft->second) * gp_Vec(*pf, ft->first).Dot(gp_Vec(ft->second)));
        return move;
    }
    if (ff && pt) {
        move.SetTranslation(gp_Vec(ff->second) * gp_Vec(ff->first, *pt).Dot(gp_Vec(ff->second)));
        return move;
    }
    if (ff && ft) {
        // Turn the face to look at the other one, then slide it onto its plane.
        const gp_Dir want = ft->second.Reversed();
        gp_Trsf turn;
        const gp_Vec axis = gp_Vec(ff->second).Crossed(gp_Vec(want));
        if (axis.Magnitude() > 1e-9) {
            turn.SetRotation(gp_Ax1(ff->first, gp_Dir(axis)), ff->second.Angle(want));
        } else if (ff->second.Dot(want) < 0.0) {
            // Facing the same way: half a turn about any line across the face.
            const gp_Dir n = ff->second;
            const gp_Dir across = std::abs(n.X()) < 0.9 ? n.Crossed(gp::DX()) : n.Crossed(gp::DY());
            turn.SetRotation(gp_Ax1(ff->first, across), kPi);
        }
        gp_Trsf slide;
        slide.SetTranslation(gp_Vec(ft->second) * gp_Vec(ff->first, ft->first).Dot(gp_Vec(ft->second)));
        return slide * turn;
    }
    return std::nullopt;
}
