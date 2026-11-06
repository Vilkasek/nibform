#include "viewer/PlanarPrs.h"

#include <ElSLib.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <gp_Pln.hxx>

PlanarPrs::PlanarPrs(const gp_Ax3& plane, const Style& style)
    : m_plane(plane)
    , m_style(style)
{
    // Sketch geometry lies exactly on the grid plane; draw it on top to avoid z-fighting.
    SetZLayer(Graphic3d_ZLayerId_Topmost);
    SetMutable(true);
}

void PlanarPrs::Compute(const Handle(PrsMgr_PresentationManager)&,
                        const Handle(Prs3d_Presentation)& prs,
                        const Standard_Integer)
{
    const gp_Pln pln(m_plane);
    auto to3d = [&pln](const gp_Pnt2d& p) { return ElSLib::Value(p.X(), p.Y(), pln); };

    auto addSegments = [&](const std::vector<Segment2d>& segs, const Quantity_Color& color) {
        if (segs.empty())
            return;
        Handle(Graphic3d_ArrayOfSegments) arr = new Graphic3d_ArrayOfSegments(static_cast<int>(segs.size()) * 2);
        for (const Segment2d& s : segs) {
            arr->AddVertex(to3d(s.a));
            arr->AddVertex(to3d(s.b));
        }
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(color, m_style.lineType, m_style.lineWidth));
        group->AddPrimitiveArray(arr);
    };

    auto addPoints = [&](const std::vector<gp_Pnt2d>& pts, const Quantity_Color& color, double scale) {
        if (pts.empty() || scale <= 0.0)
            return;
        Handle(Graphic3d_ArrayOfPoints) arr = new Graphic3d_ArrayOfPoints(static_cast<int>(pts.size()));
        for (const gp_Pnt2d& p : pts)
            arr->AddVertex(to3d(p));
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectMarker3d(Aspect_TOM_BALL, color, scale));
        group->AddPrimitiveArray(arr);
    };

    addSegments(m_list.segments, m_style.color);
    addSegments(m_list.definedSegments, m_style.definedColor);
    addSegments(m_list.accentSegments, m_style.accentColor);
    addPoints(m_list.points, m_style.color, m_style.pointScale);
    addPoints(m_list.accentPoints, m_style.accentColor, std::max(1.0, m_style.pointScale) * 1.6);
}
