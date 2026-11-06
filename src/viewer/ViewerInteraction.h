#pragma once

#include <Graphic3d_Vec2.hxx>

#include <QPointF>
#include <Qt>

class QKeyEvent;
class QPainter;

struct PointerEvent
{
    enum class Source { Mouse, Pen };

    Source source = Source::Mouse;
    QPointF pos;           // logical widget coordinates
    Graphic3d_Vec2i pixel; // device pixels, as OCCT expects
    double pressure = 1.0;
    bool eraser = false; // the pen's eraser end
    Qt::KeyboardModifiers modifiers;
};

// Something that wants the "primary" pointer (pen tip / left mouse button)
// before the viewer uses it for selection. Navigation input never reaches it.
// Each handler returns true when the view needs a redraw.
class ViewerInteraction
{
public:
    virtual ~ViewerInteraction() = default;

    // Return false to let the viewer handle this press itself.
    virtual bool wantsPress(const PointerEvent& e) = 0;
    virtual bool press(const PointerEvent& e) = 0;
    virtual bool move(const PointerEvent& e, bool pressed) = 0;
    virtual bool release(const PointerEvent& e) = 0;
    virtual bool leave() { return false; }
    virtual bool key(QKeyEvent* e) { return false; }
    // Screen-space drawing on top of the 3D view, in logical pixels (labels, dimensions).
    virtual void paintOverlay(QPainter& painter) {}
};
