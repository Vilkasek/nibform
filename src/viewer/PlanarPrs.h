#pragma once

#include "sketch/DrawList.h"

#include <AIS_InteractiveObject.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Quantity_Color.hxx>
#include <gp_Ax3.hxx>

// Draws a DrawList on a plane. Not selectable: sketch picking is done in 2D by the editor.
class PlanarPrs : public AIS_InteractiveObject
{
    DEFINE_STANDARD_RTTI_INLINE(PlanarPrs, AIS_InteractiveObject)

public:
    struct Style
    {
        Quantity_Color color;
        Quantity_Color accentColor;
        double lineWidth = 2.0;
        Aspect_TypeOfLine lineType = Aspect_TOL_SOLID;
        double pointScale = 1.0; // 0 hides plain points
    };

    PlanarPrs(const gp_Ax3& plane, const Style& style);

    void setPlane(const gp_Ax3& plane) { m_plane = plane; }
    void setDrawList(DrawList list) { m_list = std::move(list); }
    void setStyle(const Style& style) { m_style = style; }
    const DrawList& drawList() const { return m_list; }

    bool AcceptDisplayMode(const Standard_Integer mode) const override { return mode == 0; }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& prs,
                 const Standard_Integer mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)&, const Standard_Integer) override {}

private:
    gp_Ax3 m_plane;
    Style m_style;
    DrawList m_list;
};
