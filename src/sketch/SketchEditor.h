#pragma once

#include "sketch/Sketch.h"
#include "sketch/SketchTools.h"
#include "sketch/Snapper.h"
#include "viewer/PlanarPrs.h"
#include "viewer/ViewerInteraction.h"

#include <QObject>

#include <memory>
#include <optional>
#include <set>
#include <vector>

class OcctViewerWidget;

enum class SketchPlane { XY, XZ, YZ };

// Owns the sketches (one per base plane for now), routes primary pointer input
// to the active tool and keeps the on-screen presentations in sync.
class SketchEditor : public QObject, public ViewerInteraction, public SketchToolHost
{
    Q_OBJECT

public:
    explicit SketchEditor(OcctViewerWidget* viewer, QObject* parent = nullptr);
    ~SketchEditor() override;

    ToolKind tool() const { return m_toolKind; }
    void setTool(ToolKind kind);
    SketchPlane plane() const { return m_slots[m_active].plane; }
    void setPlane(SketchPlane plane);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void undo();
    void redo();
    void deleteSelection();
    void clearActiveSketch();

    // SketchToolHost
    const Sketch& sketch() const override { return m_slots[m_active].sketch; }
    void commit(const std::function<void(Sketch&)>& change) override;

    // ViewerInteraction
    bool wantsPress(const PointerEvent& e) override { return true; }
    bool press(const PointerEvent& e) override;
    bool move(const PointerEvent& e, bool pressed) override;
    bool release(const PointerEvent& e) override;
    bool leave() override;
    bool key(QKeyEvent* e) override;

signals:
    void toolChanged(ToolKind kind);
    void hintChanged(const QString& hint);
    void historyChanged();
    void selectionChanged(int count);

private:
    struct Slot
    {
        SketchPlane plane;
        Sketch sketch;
        Handle(PlanarPrs) prs;
    };
    struct HistoryStep
    {
        int slot;
        Sketch sketch;
    };

    static gp_Ax3 planeAxes(SketchPlane plane);

    std::optional<gp_Pnt2d> toSketch(const Graphic3d_Vec2i& pixel) const;
    double pixelsToModel(const Graphic3d_Vec2i& pixel, double logicalPixels) const;
    SnapResult snapAt(const PointerEvent& e) const;

    void setSelection(std::set<EntityId> selection);
    void resetInteraction();
    void refreshSketches();
    void refreshOverlay();
    void updateHint();

    OcctViewerWidget* m_viewer;
    Snapper m_snapper;

    std::vector<Slot> m_slots;
    int m_active = 0;

    ToolKind m_toolKind = ToolKind::Select;
    std::unique_ptr<SketchTool> m_tool;

    std::set<EntityId> m_selection;
    std::optional<EntityId> m_hover;
    std::optional<SnapResult> m_snap;
    Graphic3d_Vec2i m_pressPixel;

    std::vector<HistoryStep> m_undo;
    std::vector<HistoryStep> m_redo;

    Handle(PlanarPrs) m_previewPrs; // rubber band of the tool in progress
    Handle(PlanarPrs) m_overlayPrs; // snap marker and alignment guides
};
