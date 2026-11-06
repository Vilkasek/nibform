#pragma once

#include "sketch/ConstraintCommands.h"
#include "sketch/Sketch.h"
#include "sketch/SketchTools.h"
#include "sketch/Snapper.h"
#include "sketch/Solver.h"
#include "viewer/PlanarPrs.h"
#include "viewer/ViewerInteraction.h"

#include <QColor>
#include <QObject>
#include <QRectF>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class OcctViewerWidget;
class ValueEditor;

enum class SketchPlane { XY, XZ, YZ };

// Owns the sketches (one per base plane for now), routes primary pointer input
// to the active tool, keeps constraints solved and the on-screen presentations in sync.
class SketchEditor : public QObject, public ViewerInteraction, public SketchToolHost
{
    Q_OBJECT

public:
    // What the sketch is drawn with; one set for light backgrounds, one for dark.
    struct Colors
    {
        Quantity_Color sketch, defined, inactive, accent, preview, trim;
        QColor glyphInk, dimensionInk, overlayAccent, badgeFill;
    };

    explicit SketchEditor(OcctViewerWidget* viewer, QObject* parent = nullptr);
    ~SketchEditor() override;

    void setDark(bool dark);

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
    // Esc: drop the operation in progress, then leave the tool, then clear the selection.
    void cancel();
    // Enter: finish a polyline, or edit the selected dimension.
    void confirm();

    const SketchSelection& selection() const { return m_selection; }
    // Whether `command` fits the current selection (the toolbar enables buttons with this).
    bool canApply(ConstraintCommand command) const;
    void applyConstraint(ConstraintCommand command);
    void editDimension(ConstraintId id);

    // SketchToolHost
    const Sketch& sketch() const override { return m_slots[m_active].sketch; }
    void commit(const std::function<void(Sketch&)>& change, bool extendLastStep = false) override;
    double pickTolerance() const override;
    void askValue(const QString& label, double value, double min, double max, std::function<void(double)> accept,
                  std::function<void()> cancel) override;

    // ViewerInteraction
    bool wantsPress(const PointerEvent& e) override { return true; }
    bool press(const PointerEvent& e) override;
    bool move(const PointerEvent& e, bool pressed) override;
    bool release(const PointerEvent& e) override;
    bool leave() override;
    void paintOverlay(QPainter& painter) override;

signals:
    void toolChanged(ToolKind kind);
    void hintChanged(const QString& hint);
    void historyChanged();
    void selectionChanged();
    void statusChanged(const QString& status); // degrees of freedom of the active sketch
    void message(const QString& text);         // short-lived feedback (rejected constraint, ...)

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
    // What is under the pointer in select mode.
    struct Hit
    {
        enum class Kind { None, Point, Entity, Constraint };
        Kind kind = Kind::None;
        int id = kNoId;
        bool operator==(const Hit&) const = default;
    };
    // Screen rectangle of a constraint glyph or dimension label, from the last overlay paint.
    struct Label
    {
        ConstraintId id;
        QRectF rect;
    };

    static gp_Ax3 planeAxes(SketchPlane plane);

    std::optional<gp_Pnt2d> toSketch(const Graphic3d_Vec2i& pixel) const;
    double pixelsToModel(const Graphic3d_Vec2i& pixel, double logicalPixels) const;
    double pixelsToModel(double logicalPixels) const; // at the view center
    SnapResult snapAt(const PointerEvent& e) const;
    Hit hitAt(const PointerEvent& e) const;

    bool selectPress(const PointerEvent& e);
    bool selectMove(const PointerEvent& e, bool pressed);
    bool selectRelease(const PointerEvent& e);
    void toggle(const Hit& hit, bool additive);

    // Solves `candidate` and checks it is consistent and not redundant compared with
    // the current sketch; false (with a message) if the change should be rejected.
    bool validate(Sketch& candidate);
    // Opens the value field for `c` near `anchor`; `accepted` gets the new value in
    // solver units, on the same side / turning direction as the current one.
    void promptDimension(const SketchConstraint& c, const QPointF& anchor, std::function<void(double)> accepted);

    void setSelection(SketchSelection selection);
    void resetInteraction();
    void sketchChanged(); // re-analyzes DOF after the active sketch changed
    void updatePreviewStyle();
    void refreshSketches();
    void refreshOverlay();
    void updateHint();
    void updateStatus();

    OcctViewerWidget* m_viewer;
    Snapper m_snapper;
    ValueEditor* m_valueEditor;
    Colors m_colors;

    std::vector<Slot> m_slots;
    int m_active = 0;
    SolveResult m_analysis; // of the active sketch

    ToolKind m_toolKind = ToolKind::Select;
    std::unique_ptr<SketchTool> m_tool;

    SketchSelection m_selection;
    Hit m_hover;
    std::optional<SnapResult> m_snap;
    Graphic3d_Vec2i m_pressPixel;
    Graphic3d_Vec2i m_pointerPixel; // last pointer position, for pick tolerances
    QPointF m_pointerPos;           // the same in logical widget coordinates
    bool m_swallowRelease = false;  // the press only dismissed / refocused the value field

    // Select-mode press / drag
    Hit m_pressHit;
    bool m_dragging = false;
    gp_Pnt2d m_dragFrom;
    std::map<PointId, gp_Pnt2d> m_dragStart;

    std::vector<Label> m_labels;

    std::vector<HistoryStep> m_undo;
    std::vector<HistoryStep> m_redo;

    Handle(PlanarPrs) m_previewPrs; // rubber band of the tool in progress
    Handle(PlanarPrs) m_overlayPrs; // snap marker and alignment guides
};
