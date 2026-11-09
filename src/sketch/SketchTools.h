#pragma once

#include "sketch/ConstraintCommands.h"
#include "sketch/DrawList.h"
#include "sketch/Sketch.h"
#include "sketch/Snapper.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>

class SketchToolHost
{
public:
    virtual ~SketchToolHost() = default;
    virtual const Sketch& sketch() const = 0;
    // Records an undo step and applies `change` to the active sketch. With
    // `extendLastStep` the change joins the previous step (one stroke, one undo).
    virtual void commit(const std::function<void(Sketch&)>& change, bool extendLastStep = false) = 0;
    // How close (in sketch units) the pointer must be to pick a curve.
    virtual double pickTolerance() const = 0;
    // Asks the user for a number without blocking: `accept` runs with it, or
    // `cancel` when the user drops it (or the tool is switched away first).
    virtual void askValue(std::function<QString()> label, double value, double min, double max,
                          std::function<void(double)> accept, std::function<void()> cancel) = 0;
};

// A drawing tool. Every tool accepts both input styles a pen invites:
// tap-tap (press + release in place, then press again) and press-drag-release.
class SketchTool
{
public:
    explicit SketchTool(SketchToolHost& host) : m_host(host) {}
    virtual ~SketchTool() = default;

    // The selection made before the tool was picked (mirror and patterns work on it).
    virtual void begin(const SketchSelection& selection) {}
    virtual void press(const SnapResult& s) = 0;
    // `dragged` is true when the pointer moved noticeably since the press.
    virtual void release(const SnapResult& s, bool dragged) = 0;
    virtual void hover(const SnapResult& s) { m_cursor = s.pos; }
    // Drops the operation in progress; returns false if there was none.
    virtual bool cancel() = 0;
    // Enter: finishes what is in progress (a polyline, a spline) or moves to the next
    // step; false if there was nothing to do.
    virtual bool confirm() { return cancel(); }
    virtual void preview(DrawList& out) const = 0;
    // Points the snapper should offer horizontal/vertical alignment against.
    virtual std::vector<gp_Pnt2d> anchors() const = 0;
    virtual QString hint() const = 0;
    // Whether the pointer should snap to points and alignments for this tool.
    virtual bool snaps() const { return true; }

protected:
    static PointId resolve(Sketch& sketch, const SnapResult& s)
    {
        return s.pointId != kNoId ? s.pointId : sketch.pointAt(s.pos);
    }
    // resolve() plus whatever the snap implied: pinned to the origin, or a line's midpoint.
    static PointId resolveConstrained(Sketch& sketch, const SnapResult& s);
    // Horizontal / vertical when the two points line up exactly (alignment snapping).
    static void constrainAxis(Sketch& sketch, PointId a, PointId b, EntityId line);

    SketchToolHost& m_host;
    gp_Pnt2d m_cursor;
};

// Sketch tools draw on the open sketch. PushPull, Fillet and Chamfer work on regions
// and bodies in model mode, so the editor handles them itself, like Select and
// Dimension (picking, like Select): makeTool() gives no SketchTool for those.
// Project picks body edges, and Freehand draws ink, so the editor handles them as well.
enum class ToolKind
{
    Select, Line, Rectangle, Circle, Arc, Trim, Offset, Dimension, PushPull, Fillet, Chamfer, Shell,
    Polygon, Slot, Ellipse, Spline, SketchFillet, SketchChamfer, Mirror, LinearPattern, CircularPattern,
    Project, Image, Freehand,
    // Model: moving, measuring and cutting open (SketchEditorInspect.cpp)
    Move, Measure, Section,
    // Model: solid features and construction geometry (SketchEditorSolids.cpp)
    Revolve, Sweep, Loft, Hole, BodyMirror, BodyLinearPattern, BodyCircularPattern,
    OffsetPlane, AnglePlane, ThreePointPlane, Axis,
};

// The model tools that build a solid feature or construction geometry from several picks.
inline bool isSolidTool(ToolKind kind)
{
    return kind >= ToolKind::Revolve;
}

// Whether the tool belongs to sketch mode (Select works in both).
inline bool isSketchTool(ToolKind kind)
{
    return kind != ToolKind::Select && kind != ToolKind::PushPull && kind != ToolKind::Fillet
        && kind != ToolKind::Chamfer && kind != ToolKind::Shell && kind != ToolKind::Move
        && kind != ToolKind::Measure && kind != ToolKind::Section && !isSolidTool(kind);
}

std::unique_ptr<SketchTool> makeTool(ToolKind kind, SketchToolHost& host);
