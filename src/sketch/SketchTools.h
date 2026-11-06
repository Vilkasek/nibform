#pragma once

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
    // Records an undo step and applies `change` to the active sketch.
    virtual void commit(const std::function<void(Sketch&)>& change) = 0;
};

// A drawing tool. Every tool accepts both input styles Shapr3D users expect:
// tap-tap (press + release in place, then press again) and press-drag-release.
class SketchTool
{
public:
    explicit SketchTool(SketchToolHost& host) : m_host(host) {}
    virtual ~SketchTool() = default;

    virtual void press(const SnapResult& s) = 0;
    // `dragged` is true when the pointer moved noticeably since the press.
    virtual void release(const SnapResult& s, bool dragged) = 0;
    virtual void hover(const SnapResult& s) { m_cursor = s.pos; }
    // Drops the operation in progress; returns false if there was none.
    virtual bool cancel() = 0;
    virtual void preview(DrawList& out) const = 0;
    // Points the snapper should offer horizontal/vertical alignment against.
    virtual std::vector<gp_Pnt2d> anchors() const = 0;
    virtual QString hint() const = 0;

protected:
    static PointId resolve(Sketch& sketch, const SnapResult& s)
    {
        return s.pointId != kNoId ? s.pointId : sketch.pointAt(s.pos);
    }

    SketchToolHost& m_host;
    gp_Pnt2d m_cursor;
};

enum class ToolKind { Select, Line, Rectangle, Circle, Arc };

std::unique_ptr<SketchTool> makeTool(ToolKind kind, SketchToolHost& host);
