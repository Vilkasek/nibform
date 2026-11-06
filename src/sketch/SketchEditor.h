#pragma once

#include "io/ProjectFile.h"
#include "model/Features.h"
#include "model/Solids.h"
#include "sketch/ConstraintCommands.h"
#include "sketch/Sketch.h"
#include "sketch/SketchTools.h"
#include "sketch/Snapper.h"
#include "sketch/Solver.h"
#include "viewer/PlanarPrs.h"
#include "viewer/ViewerInteraction.h"

#include <AIS_Shape.hxx>
#include <Bnd_Box.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <QColor>
#include <QObject>
#include <QRectF>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class OcctViewerWidget;
class ValueEditor;

enum class SketchPlane { XY, XZ, YZ };

// Model: bodies and finished sketches; their regions are pulled into bodies and body
// edges rounded. Sketch: one sketch is open for drawing and constraining.
enum class EditorMode { Model, Sketch };

// One entry of the history timeline shown under the view.
struct TimelineItem
{
    QString name;   // "Szkic 1", "Wyciągnięcie 2", ...
    QString detail; // "12 mm", "r 2 mm"; empty when there is no value to edit
    QString error;  // why it didn't apply on the last rebuild, empty if it did
    bool sketch = false;
    bool rolledBack = false; // after the sketch being edited: not shown in the model now
};

// A closed region of sketch `sketch` (index into its regions).
struct RegionRef
{
    int sketch = -1;
    int index = -1;
    auto operator<=>(const RegionRef&) const = default;
};

// Owns the sketches and the solid bodies, routes primary pointer input to the active
// tool, keeps constraints solved and the on-screen presentations in sync.
// Model-mode parts (regions, bodies, extrude, fillet) live in SketchEditorBodies.cpp.
class SketchEditor : public QObject, public ViewerInteraction, public SketchToolHost
{
    Q_OBJECT

public:
    // What the model is drawn with; one set for light backgrounds, one for dark.
    struct Colors
    {
        Quantity_Color sketch, defined, inactive, accent, preview, trim, body, region;
        QColor glyphInk, dimensionInk, overlayAccent, badgeFill;
    };

    explicit SketchEditor(OcctViewerWidget* viewer, QObject* parent = nullptr);
    ~SketchEditor() override;

    void setDark(bool dark);

    // --- Modes ----------------------------------------------------------------------
    EditorMode mode() const { return m_active >= 0 ? EditorMode::Sketch : EditorMode::Model; }
    static gp_Ax3 basePlane(SketchPlane plane);
    // Opens a new, empty sketch on `plane` and turns the view to face it.
    void createSketch(const gp_Ax3& plane);
    // Exactly one planar body face is picked: a sketch can start on it.
    bool canSketchOnFace() const;
    void createSketchOnFace();
    // Picked regions all belong to one sketch: it can be reopened.
    bool canEditSketch() const;
    void editSketch();
    // Back to the model; an empty sketch is dropped.
    void closeSketch();

    ToolKind tool() const { return m_toolKind; }
    void setTool(ToolKind kind);

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void undo();
    void redo();
    void deleteSelection();
    void clearActiveSketch();
    // Esc: drop the operation in progress, then leave the tool, then clear the selection.
    void cancel();
    // Enter: finish a polyline, accept a typed value, or edit the selected dimension.
    void confirm();

    const SketchSelection& selection() const { return m_selection; }
    // Whether `command` fits the current selection (the toolbar enables buttons with this).
    bool canApply(ConstraintCommand command) const;
    void applyConstraint(ConstraintCommand command);
    void editDimension(ConstraintId id);
    // Switches the selected curves between real and construction (reference) geometry.
    bool canToggleConstruction() const { return m_active >= 0 && !m_selection.entities.empty(); }
    void toggleConstruction();

    // --- Bodies ---------------------------------------------------------------------
    BodyOperation bodyOperation() const { return m_bodyOperation; }
    void setBodyOperation(BodyOperation operation);
    int bodyCount() const { return static_cast<int>(m_bodies.size()); }
    // Bodies owning the picked faces / edges, in the order they were picked.
    std::vector<int> selectedBodies() const;
    void combineBodies(BooleanKind kind);
    // Region of sketch `sketch` at `p` (sketch coordinates), and pulling regions directly.
    std::optional<RegionRef> regionAt(int sketch, const gp_Pnt2d& p) const;
    void extrudeRegions(const std::vector<RegionRef>& regions, double distance);
    // Picks a region as a tap in Select would (showing its arrow and distance field).
    void selectRegion(const RegionRef& region);

    // For the floating context bar: what is picked, and where the pointer last was.
    bool hasPickedEdges() const;
    bool hasPickedFaces() const;
    bool valueSessionOpen() const { return m_session.kind != ValueSession::Kind::None; }
    QPointF pointerPos() const { return m_pointerPos; }
    int sketchCount() const { return static_cast<int>(m_slots.size()); }
    const std::vector<TopoDS_Shape>& bodies() const { return m_bodies; }

    // --- History and project ----------------------------------------------------------
    std::vector<TimelineItem> timeline() const;
    // A sketch entry reopens the sketch; a valued one opens its value, previewed live.
    void activateTimelineItem(int index);
    // Removes the feature (or the sketch, with its entry); later features are rebuilt.
    void removeTimelineItem(int index);
    ProjectData project() const;
    void loadProject(ProjectData project); // replaces everything; undo starts empty
    void newProject();
    bool isModified() const { return m_changes != m_savedChanges; }
    void markSaved() { m_savedChanges = m_changes; emit historyChanged(); }

    // SketchToolHost
    const Sketch& sketch() const override;
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
    void modeChanged(EditorMode mode);
    void toolChanged(ToolKind kind);
    void hintChanged(const QString& hint);
    void historyChanged();
    void selectionChanged();
    void statusChanged(const QString& status); // DOF of the open sketch / body count
    void message(const QString& text);         // short-lived feedback (rejected constraint, ...)

private:
    struct Slot
    {
        int id = -1; // stable across deletions; what features refer to
        Sketch sketch;
        Handle(PlanarPrs) prs;
        std::vector<TopoDS_Face> regions; // closed regions, kept up to date
    };
    // Everything undo restores: the sketches and the history (bodies are rebuilt from them).
    struct Snapshot
    {
        std::vector<ProjectData::SketchEntry> sketches;
        std::vector<Feature> features;
    };
    // What is under the pointer. Sketch mode: points, curves, constraint labels.
    // Model mode: Region (`region`), Face / Edge of body `id` (picked through AIS).
    struct Hit
    {
        enum class Kind { None, Point, Entity, Constraint, Region, Face, Edge };
        Kind kind = Kind::None;
        int id = kNoId;
        RegionRef region = {};
        Handle(SelectMgr_EntityOwner) owner = {};
        TopoDS_Shape shape = {}; // the face or edge, oriented as it sits in its body
        bool operator==(const Hit& o) const
        {
            return kind == o.kind && id == o.id && region == o.region && owner == o.owner;
        }
    };
    // A body face or edge the user picked.
    struct BodyPick
    {
        Handle(SelectMgr_EntityOwner) owner;
        TopoDS_Shape shape;
        int body = -1;
    };
    // A typed value applied to the model selection with a live preview: how far to pull
    // the picked profiles (with an arrow to drag), or how much to round / bevel edges.
    struct ValueSession
    {
        enum class Kind { None, Extrude, Fillet, Chamfer, Shell };
        Kind kind = Kind::None;
        double value = 0.0;
        // The arrow: extrusion direction from the profile's middle, or into the
        // material from an edge's middle for fillet / chamfer
        bool arrow = false;
        gp_Pnt base; // where the arrow starts at value 0
        gp_Dir normal;
        bool dragging = false;
        double dragStartValue = 0.0;
        double dragStartParam = 0.0;
        std::string error; // why the previewed value doesn't work (fillet too big, ...)
    };
    // Screen rectangle of a constraint glyph or dimension label, from the last overlay paint.
    struct Label
    {
        ConstraintId id;
        QRectF rect;
    };

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

    void enterSketch(int index, bool lookAt);
    void setSelection(SketchSelection selection);
    void resetInteraction();
    void sketchChanged(); // re-analyzes DOF and regions after the open sketch changed
    void updatePreviewStyle();
    void refreshSketches();
    void refreshOverlay();
    void updateHint();
    void updateStatus();
    void updateGrid();

    // Model mode (SketchEditorBodies.cpp)
    Hit modelHitAt(const PointerEvent& e) const;
    bool modelPress(const PointerEvent& e);
    bool modelMove(const PointerEvent& e, bool pressed);
    bool modelRelease(const PointerEvent& e);
    bool isPicked(const Hit& hit) const;
    void togglePick(const Hit& hit);
    // Fillet / chamfer: a tapped face stands for all of its edges.
    void toggleFaceEdges(const Hit& face);
    // Drops picks the new tool can't use (edges outside the rounding tools and so on).
    void keepPicks(bool edges, bool faces, bool regions);
    std::vector<std::pair<int, TopoDS_Face>> selectedFaces() const;
    Handle(SelectMgr_EntityOwner) ownerOf(int body, const TopoDS_Shape& shape) const;
    void rehighlightPicks();
    void clearModelSelection();
    std::vector<Profile> selectedProfiles() const;
    std::vector<EdgeRef> selectedEdges() const;
    void syncValueSession(); // opens / updates / closes the session after a pick changed
    void endValueSession(bool restoreBodies);
    void showSessionPreview();
    void hideSessionPreview();
    void applySession(double value);
    std::optional<std::pair<QPointF, QPointF>> arrowOnScreen() const;
    bool hitsArrow(const QPointF& pos) const;
    double arrowParam(const Graphic3d_Vec2i& pixel) const;
    void paintArrow(QPainter& painter) const;
    // Applies a new feature on top of the model and records it; false (with a message)
    // if it doesn't apply.
    bool commitFeature(const Feature& feature);
    void rebuildModel(); // replays the history (up to the rollback point while a sketch is edited)
    Bnd_Box sketchBox(int slot) const;
    Bnd_Box bodiesBox() const;
    SketchLookup sketchLookup() const;
    std::optional<Feature> featureFromSession(double value) const;
    void pushUndo(); // before any change of sketches or history
    Snapshot snapshot() const;
    void restore(Snapshot snapshot);
    void refreshBodies();
    void updateSelectionModes();
    void refreshRegions(int sketch);
    void showRegions(); // redraws region fills (selection changed)
    void applyModelColors();

    OcctViewerWidget* m_viewer;
    Snapper m_snapper;
    ValueEditor* m_valueEditor;
    Colors m_colors;

    std::vector<Slot> m_slots;
    int m_active = -1; // the open sketch, -1 in model mode
    SolveResult m_analysis; // of the open sketch

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

    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;

    std::vector<Feature> m_features;
    std::vector<std::string> m_featureErrors; // per feature, from the last rebuild
    int m_nextSketchId = 1;
    // While a sketch is edited, the model shows only the features before its timeline
    // entry: what was built from the sketch (and after it) is out of the way. -1: all.
    int m_rollback = -1;
    int m_changes = 0;      // counts edits, for the unsaved-changes mark
    int m_savedChanges = 0;
    std::vector<TopoDS_Shape> m_bodies; // built from m_features
    std::vector<Handle(AIS_Shape)> m_bodyPrs;
    std::set<RegionRef> m_regionSelection;
    std::vector<BodyPick> m_bodySelection; // faces, or edges in the fillet / chamfer tools
    BodyOperation m_bodyOperation = BodyOperation::Auto;
    ValueSession m_session;
    Hit m_modelPress; // what the current model-mode press landed on
    Handle(AIS_Shape) m_regionPrs;         // all regions, faint fill
    Handle(AIS_Shape) m_selectedRegionPrs; // picked ones, accent fill
    Handle(AIS_Shape) m_previewPrs3d;      // extrusion / rounded bodies being previewed

    Handle(PlanarPrs) m_previewPrs; // rubber band of the sketch tool in progress
    Handle(PlanarPrs) m_overlayPrs; // snap marker and alignment guides
};
