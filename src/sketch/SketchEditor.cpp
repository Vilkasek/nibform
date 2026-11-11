#include "sketch/SketchEditor.h"

#include "sketch/Freehand.h"
#include "sketch/Trim.h"
#include "viewer/Glyphs.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/UnitText.h"
#include "viewer/ValueEditor.h"

#include <QCoreApplication>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>

#include <gp_Vec.hxx>

#include <cmath>
#include <limits>

namespace {

// Pen-friendly pick radii, in logical pixels.
constexpr double kPointSnapPx = 12.0;
constexpr double kAlignSnapPx = 8.0;
constexpr double kHitPx = 10.0;
constexpr double kDragThresholdPx = 6.0;
constexpr double kDimensionOffsetPx = 40.0; // clears the relation glyphs next to a line

Quantity_Color srgb(double r, double g, double b)
{
    return Quantity_Color(r, g, b, Quantity_TOC_sRGB);
}

// Light background: dark ink. Dark background: light ink, same hues.
const SketchEditor::Colors kLightColors{
    srgb(0.10, 0.12, 0.16), srgb(0.05, 0.47, 0.30), srgb(0.45, 0.48, 0.53), srgb(0.98, 0.45, 0.09),
    srgb(0.13, 0.42, 0.95), srgb(0.90, 0.16, 0.16), srgb(0.70, 0.74, 0.80), srgb(0.25, 0.50, 0.95),
    QColor(52, 60, 74), QColor(40, 72, 140), QColor(250, 115, 23), QColor(255, 255, 255, 235),
};
const SketchEditor::Colors kDarkColors{
    srgb(0.86, 0.88, 0.91), srgb(0.38, 0.82, 0.58), srgb(0.44, 0.47, 0.52), srgb(1.00, 0.56, 0.20),
    srgb(0.40, 0.64, 1.00), srgb(1.00, 0.38, 0.38), srgb(0.52, 0.57, 0.64), srgb(0.35, 0.60, 1.00),
    QColor(210, 216, 226), QColor(140, 180, 255), QColor(255, 140, 60), QColor(32, 34, 38, 235),
};

QPointF unit(const QPointF& v)
{
    const double len = std::hypot(v.x(), v.y());
    return len > 1e-9 ? v / len : QPointF(1.0, 0.0);
}

void drawArrow(QPainter& p, const QPointF& tip, const QPointF& from, const QColor& ink)
{
    const QPointF d = unit(tip - from);
    const QPointF n(-d.y(), d.x());
    QPainterPath head;
    head.moveTo(tip);
    head.lineTo(tip - d * 8.0 + n * 3.0);
    head.lineTo(tip - d * 8.0 - n * 3.0);
    head.closeSubpath();
    p.fillPath(head, ink);
}

QString dimensionText(const SketchConstraint& c)
{
    switch (c.kind) {
    case ConstraintKind::Radius:
        return QStringLiteral("R %1").arg(lengthText(c.value, false));
    case ConstraintKind::Diameter:
        return QStringLiteral("Ø %1").arg(lengthText(c.value, false));
    case ConstraintKind::Angle:
        return QStringLiteral("%1°").arg(std::abs(c.value) * 180.0 / M_PI, 0, 'f', 1);
    default:
        return lengthText(std::abs(c.value), false);
    }
}

} // namespace

SketchEditor::SketchEditor(OcctViewerWidget* viewer, QObject* parent)
    : QObject(parent)
    , m_viewer(viewer)
    , m_valueEditor(new ValueEditor(viewer))
    , m_colors(kLightColors)
{
    const double dpr = viewer->devicePixelRatioF();
    const auto& ctx = viewer->aisContext();
    // Body edges are thin targets for a pen.
    ctx->SetPixelTolerance(qRound(8.0 * dpr));

    m_previewPrs = new PlanarPrs(basePlane(SketchPlane::XY),
                                 {m_colors.preview, m_colors.preview, 2.0 * dpr, Aspect_TOL_SOLID, 0.0});
    m_overlayPrs = new PlanarPrs(basePlane(SketchPlane::XY),
                                 {m_colors.accent, m_colors.accent, 1.0 * dpr, Aspect_TOL_DASH, 0.0});
    ctx->Display(m_previewPrs, 0, -1, false);
    ctx->Display(m_overlayPrs, 0, -1, false);

    updateGrid();
    updateStatus();
}

SketchEditor::~SketchEditor() = default;

void SketchEditor::setDark(bool dark)
{
    m_colors = dark ? kDarkColors : kLightColors;
    m_overlayPrs->setStyle({m_colors.accent, m_colors.accent, 1.0 * m_viewer->devicePixelRatioF(),
                            Aspect_TOL_DASH, 0.0});
    updatePreviewStyle();
    applyModelColors();
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
}

void SketchEditor::updatePreviewStyle()
{
    // Trim and offset highlight the curves they act on thickly (trim in red, as it deletes),
    // so the highlight shows around the sketch lines it sits on. The rest preview in blue.
    const bool trim = m_toolKind == ToolKind::Trim;
    const bool thick = trim || m_toolKind == ToolKind::Offset || m_toolKind == ToolKind::Mirror
                    || m_toolKind == ToolKind::LinearPattern || m_toolKind == ToolKind::CircularPattern;
    m_previewPrs->setStyle({m_colors.preview, trim ? m_colors.trim : m_colors.preview,
                            (thick ? 4.0 : 2.0) * m_viewer->devicePixelRatioF(), Aspect_TOL_SOLID, 0.0});
}

gp_Ax3 SketchEditor::basePlane(SketchPlane plane)
{
    // Chosen so the sketch's (u, v) read naturally when looking at the plane from
    // the matching standard view: Top -> (X, Y), Front -> (X, Z), Right -> (Y, Z).
    switch (plane) {
    case SketchPlane::XY:
        return gp_Ax3(gp::Origin(), gp::DZ(), gp::DX());
    case SketchPlane::XZ:
        return gp_Ax3(gp::Origin(), -gp::DY(), gp::DX());
    case SketchPlane::YZ:
        return gp_Ax3(gp::Origin(), gp::DX(), gp::DY());
    }
    return gp_Ax3();
}

void SketchEditor::setTool(ToolKind kind)
{
    // Each tool belongs to one mode (the toolbar only offers those).
    if (isSketchTool(kind) != (mode() == EditorMode::Sketch) && kind != ToolKind::Select)
        return;
    endPatternEdit(false); // a pattern being changed from the history goes back
    endValueSession(true); // before the field is dropped, so dropping it doesn't clear picks
    m_valueEditor->cancel(); // may call back into the old tool, so before it goes
    const std::vector<int> pickedBodies = selectedBodies(); // Move starts with them
    inspectToolChanged();
    if (m_tool)
        m_tool->cancel();
    const SketchSelection picked = m_selection; // mirror and patterns start from it
    m_toolKind = kind;
    m_tool = makeTool(kind, *this);
    if (m_tool)
        m_tool->begin(picked);
    updatePreviewStyle();
    m_snap.reset();
    m_hover = {};
    m_stroking = false;
    m_stroke.clear();
    m_strokeScreen.clear();
    showHoverRegion();
    if (kind != ToolKind::Select)
        setSelection({});
    if (mode() == EditorMode::Sketch)
        updateSelectionModes(); // Project picks body edges

    if (mode() == EditorMode::Model) {
        // Picks carry over where they make sense: edges picked in Select into the rounding
        // tools, regions and faces into push / pull; Select keeps everything. Solid tools
        // take the regions (revolve, sweep, loft) or the bodies (mirror, patterns) picked.
        hideSessionPreview();
        const std::set<RegionRef> regions = m_regionSelection;
        const std::vector<int> bodies = selectedBodies();
        m_solid = {};
        m_solidError.clear();
        if (kind == ToolKind::Fillet || kind == ToolKind::Chamfer)
            keepPicks(true, false, false);
        else if (kind == ToolKind::PushPull)
            keepPicks(false, true, true);
        else if (kind == ToolKind::Shell)
            keepPicks(false, true, false);
        else if (isSolidTool(kind))
            clearModelSelection();
        if (kind == ToolKind::Revolve || kind == ToolKind::Sweep || kind == ToolKind::Loft) {
            m_solid.regions.assign(regions.begin(), regions.end());
            m_regionSelection = regions;
            showRegions();
        } else if (kind == ToolKind::BodyMirror || kind == ToolKind::BodyLinearPattern
                   || kind == ToolKind::BodyCircularPattern) {
            m_solid.bodies = bodies;
        } else if (kind == ToolKind::Move || kind == ToolKind::Measure || kind == ToolKind::Section) {
            clearModelSelection();
        }
        if (kind == ToolKind::Move && !pickedBodies.empty()) {
            m_move.bodies = pickedBodies;
            refreshMovePreview();
        }
        updateSelectionModes();
        syncValueSession();
    }
    refreshSolidMarks(); // and the datums the tool can pick

    refreshSketches();
    refreshOverlay();
    updateHint();
    m_viewer->update();
    emit toolChanged(kind);
}

const Sketch& SketchEditor::sketch() const
{
    static const Sketch none;
    // Undo can take the open sketch away for a moment, before closeSketch() catches up.
    return m_active >= 0 && m_active < static_cast<int>(m_slots.size()) ? m_slots[m_active].sketch : none;
}

void SketchEditor::createSketch(const gp_Ax3& plane)
{
    if (mode() == EditorMode::Sketch)
        closeSketch();
    Slot slot;
    slot.id = m_nextSketchId++;
    slot.sketch = Sketch(plane);
    slot.prs = new PlanarPrs(plane, {m_colors.sketch, m_colors.accent, 2.0 * m_viewer->devicePixelRatioF()});
    m_viewer->aisContext()->Display(slot.prs, 0, -1, false);
    m_slots.push_back(std::move(slot));
    // Not an undo step by itself: the first drawn curve records the sketch.
    enterSketch(static_cast<int>(m_slots.size()) - 1, true);
}

void SketchEditor::enterSketch(int index, bool lookAt)
{
    endValueSession(true);
    clearModelSelection();
    m_active = index;
    // Roll the model back to when the sketch was made, so what was built from it doesn't
    // stand in the way (a new sketch has no entry yet: everything stays).
    m_rollback = -1;
    for (int i = 0; i < static_cast<int>(m_features.size()); ++i) {
        if (m_features[i].kind == Feature::Kind::Sketch && m_features[i].sketch == m_slots[index].id)
            m_rollback = i;
    }
    if (m_rollback >= 0)
        rebuildModel();
    updateSelectionModes(); // bodies stop reacting to the pointer while drawing
    const gp_Ax3& axes = sketch().plane();
    m_previewPrs->setPlane(axes);
    m_overlayPrs->setPlane(axes);
    updateGrid();
    if (lookAt)
        m_viewer->animateCamera(axes.Direction(), axes.YDirection(), sketchBox(index));
    emit modeChanged(EditorMode::Sketch);
    setTool(ToolKind::Select); // a sketch opens in Select; drawing tools are picked from the bar
    sketchChanged();
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
}

void SketchEditor::closeSketch()
{
    if (m_active < 0)
        return;
    resetInteraction();
    setSelection({});
    const int closing = m_active;
    m_active = -1;
    m_rollback = -1;
    if (closing < static_cast<int>(m_slots.size())) {
        const int id = m_slots[closing].id;
        auto isEntry = [id](const Feature& f) { return f.kind == Feature::Kind::Sketch && f.sketch == id; };
        if (m_slots[closing].sketch.isEmpty()) {
            m_viewer->aisContext()->Remove(m_slots[closing].prs, false);
            m_slots.erase(m_slots.begin() + closing);
            std::erase_if(m_features, isEntry);
            m_regionSelection.clear();
        } else if (std::none_of(m_features.begin(), m_features.end(), isEntry)) {
            Feature entry;
            entry.kind = Feature::Kind::Sketch;
            entry.sketch = id;
            m_features.push_back(entry);
        }
        // Everything built from the sketch follows its changes.
        rebuildModel();
        emit historyChanged();
    }
    updateGrid();
    updateSelectionModes();
    emit modeChanged(EditorMode::Model);
    setTool(ToolKind::Select);
    refreshSketches();
    refreshOverlay();
    showRegions();
    updateStatus();
    m_viewer->update();
}

bool SketchEditor::canEditSketch() const
{
    if (mode() != EditorMode::Model || m_regionSelection.empty())
        return false;
    const int sketch = m_regionSelection.begin()->sketch;
    return std::all_of(m_regionSelection.begin(), m_regionSelection.end(),
                       [sketch](const RegionRef& r) { return r.sketch == sketch; });
}

void SketchEditor::editSketch()
{
    if (canEditSketch())
        enterSketch(m_regionSelection.begin()->sketch, true);
}

void SketchEditor::updateGrid()
{
    // The grid lies on the open sketch, or on the ground in the model.
    const gp_Ax3 axes = m_active >= 0 ? sketch().plane() : basePlane(SketchPlane::XY);
    const Handle(V3d_Viewer)& viewer = m_viewer->viewer();
    viewer->SetPrivilegedPlane(axes);
    // The grid picks up a new privileged plane only when re-activated.
    viewer->DeactivateGrid();
    viewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
    if (!m_viewer->view().IsNull())
        m_viewer->view()->Invalidate();
}

void SketchEditor::commit(const std::function<void(Sketch&)>& change, bool extendLastStep)
{
    if (m_active < 0)
        return;
    m_valueEditor->cancel(); // a pending value would apply to a sketch that no longer exists
    if (!extendLastStep || m_undo.empty())
        pushUndo();
    else
        ++m_changes;
    Sketch& s = m_slots[m_active].sketch;
    change(s);
    m_analysis = solve(s);
    updateStatus();
    refreshRegions(m_active);
    refreshSketches();
    emit historyChanged();
}

void SketchEditor::undo()
{
    if (m_undo.empty())
        return;
    Snapshot step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back(snapshot());
    ++m_changes;
    restore(std::move(step));

    // Tools may hold point ids that no longer exist.
    resetInteraction();
    if (m_active >= static_cast<int>(m_slots.size()))
        closeSketch(); // the open sketch was undone away
    setSelection({});
    clearModelSelection();
    sketchChanged();
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
    emit historyChanged();
}

void SketchEditor::redo()
{
    if (m_redo.empty())
        return;
    Snapshot step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back(snapshot());
    ++m_changes;
    restore(std::move(step));

    resetInteraction();
    if (m_active >= static_cast<int>(m_slots.size()))
        closeSketch();
    setSelection({});
    clearModelSelection();
    sketchChanged();
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
    emit historyChanged();
}

void SketchEditor::deleteSelection()
{
    if (mode() == EditorMode::Model) {
        // Picked faces delete their bodies, picked regions their whole sketches.
        const std::vector<int> bodies = selectedBodies();
        std::set<int> sketches;
        for (const RegionRef& r : m_regionSelection)
            sketches.insert(r.sketch);
        if (bodies.empty() && sketches.empty())
            return;
        endValueSession(true);
        // Bodies go through a Delete feature (found by points inside them on rebuilds),
        // sketches with their timeline entries.
        Feature drop;
        drop.kind = Feature::Kind::Delete;
        for (int b : bodies) {
            if (const auto inside = insidePoint(m_bodies[b]))
                drop.bodies.push_back(*inside);
        }
        clearModelSelection();
        pushUndo();
        for (auto it = sketches.rbegin(); it != sketches.rend(); ++it) {
            const int id = m_slots[*it].id;
            m_viewer->aisContext()->Remove(m_slots[*it].prs, false);
            m_slots.erase(m_slots.begin() + *it);
            std::erase_if(m_features, [id](const Feature& f) { return f.kind == Feature::Kind::Sketch && f.sketch == id; });
        }
        if (!drop.bodies.empty())
            m_features.push_back(drop);
        showRegions();
        rebuildModel();
        emit historyChanged();
        return;
    }

    if (m_selection.empty())
        return;
    const SketchSelection doomed = m_selection;
    std::set<EntityId> entities = doomed.entities;
    // A spline loses just the picked points it passes through, while it keeps enough.
    std::vector<std::pair<EntityId, PointId>> thinned;
    {
        Sketch probe = sketch();
        for (PointId p : doomed.points) {
            for (EntityId e : probe.entitiesAt(p)) {
                const SketchEntity* ent = probe.entity(e);
                const bool inner = ent->kind == EntityKind::Spline && ent->a != p && ent->b != p;
                if (inner && !entities.count(e) && probe.entitiesAt(p).size() == 1 && probe.removeThroughPoint(e, p))
                    thinned.emplace_back(e, p);
                else
                    entities.insert(e);
            }
        }
    }
    setSelection({});
    m_hover = {};
    commit([&](Sketch& s) {
        s.removeConstraints(doomed.constraints);
        for (const auto& [spline, point] : thinned)
            s.removeThroughPoint(spline, point);
        s.removeEntities(entities);
    });
    m_viewer->update();
}

void SketchEditor::clearActiveSketch()
{
    if (m_active < 0 || sketch().isEmpty())
        return;
    resetInteraction();
    setSelection({});
    commit([](Sketch& s) { s.clear(); });
    refreshOverlay();
    m_viewer->update();
}

void SketchEditor::toggleConstruction()
{
    if (!canToggleConstruction())
        return;
    // All of them become construction, unless all already are: then all become real.
    const std::set<EntityId> picked = m_selection.entities;
    const bool allConstruction = std::all_of(picked.begin(), picked.end(), [this](EntityId id) {
        const SketchEntity* e = sketch().entity(id);
        return e && e->construction;
    });
    commit([&](Sketch& s) {
        for (EntityId id : picked)
            s.setConstruction(id, !allConstruction);
    });
    m_viewer->update();
}

void SketchEditor::setTabletMode(bool on)
{
    // Fingertips are bigger and less precise than a pen or a mouse.
    m_touchScale = on ? 1.6 : 1.0;
    m_viewer->aisContext()->SetPixelTolerance(qRound(8.0 * m_touchScale * m_viewer->devicePixelRatioF()));
    m_valueEditor->setKeypad(on);
}

bool SketchEditor::canApply(ConstraintCommand command) const
{
    if (m_active < 0 || (m_selection.points.empty() && m_selection.entities.empty()))
        return false;
    Sketch probe = sketch();
    return applyCommand(command, probe, m_selection, 1.0).applied;
}

void SketchEditor::applyConstraint(ConstraintCommand command)
{
    if (m_active < 0)
        return;
    if (m_selection.points.empty() && m_selection.entities.empty()) {
        emit message(tr("Select geometry first, then pick a constraint"));
        return;
    }

    Sketch candidate = sketch();
    const std::size_t constraintsBefore = candidate.constraints().size();
    const CommandResult r =
        applyCommand(command, candidate, m_selection, pixelsToModel(kDimensionOffsetPx));
    if (!r.applied) {
        emit message(tr("This constraint doesn't fit the selection"));
        return;
    }
    const bool merged = candidate.points().size() < sketch().points().size();
    if (!merged && candidate.constraints().size() == constraintsBefore) {
        emit message(tr("This constraint already exists"));
        return;
    }

    auto apply = [this](Sketch& next) {
        if (!validate(next))
            return;
        commit([&](Sketch& s) { s = std::move(next); });
        setSelection({});
        refreshSketches();
        m_viewer->update();
    };
    if (r.dimension == kNoId) {
        apply(candidate);
        return;
    }

    // Reject a redundant dimension before asking for its value.
    Sketch probe = candidate;
    if (!validate(probe))
        return;
    const ConstraintId dimension = r.dimension;
    promptDimension(*candidate.constraint(dimension), m_pointerPos,
                    [apply, candidate, dimension](double value) mutable {
                        candidate.constraint(dimension)->value = value;
                        apply(candidate);
                    },
                    [this] {
                        // The Dimension tool starts over, unless the field closed for a
                        // tap that adds to what is being dimensioned.
                        if (m_toolKind == ToolKind::Dimension && !m_chainingDimension) {
                            setSelection({});
                            refreshSketches();
                        }
                        updateHint();
                    });
    updateHint();
}

void SketchEditor::dimension()
{
    if (mode() != EditorMode::Sketch)
        return;
    if (canApply(ConstraintCommand::Dimension)) {
        applyConstraint(ConstraintCommand::Dimension); // selection first, then D
        return;
    }
    // A lone point picked before D is the first pick of the tool.
    const SketchSelection kept = m_selection;
    setTool(ToolKind::Dimension);
    if (kept.entities.empty() && kept.constraints.empty() && kept.points.size() == 1) {
        setSelection(kept);
        refreshSketches();
        updateHint();
    }
}

bool SketchEditor::fitsDimension(const SketchSelection& selection) const
{
    Sketch probe = sketch();
    return applyCommand(ConstraintCommand::Dimension, probe, selection, 1.0).applied;
}

void SketchEditor::dimensionTap(const Hit& hit)
{
    if (hit.kind == Hit::Kind::Constraint) {
        const SketchConstraint* c = sketch().constraint(hit.id);
        if (c && isDimension(c->kind))
            editDimension(hit.id); // an existing dimension: change its value
        return;
    }
    if (hit.kind != Hit::Kind::Point && hit.kind != Hit::Kind::Entity) {
        setSelection({}); // empty space: start over
        refreshSketches();
        updateHint();
        return;
    }
    auto add = [&hit](SketchSelection& s) {
        (hit.kind == Hit::Kind::Point ? s.points : s.entities).insert(hit.id);
    };
    SketchSelection next = m_selection;
    next.constraints.clear();
    if ((hit.kind == Hit::Kind::Point ? next.points : next.entities).erase(hit.id)) {
        setSelection(next); // tapped again: dropped
        refreshSketches();
        updateHint();
        return;
    }
    add(next);
    // What doesn't go with the earlier picks starts a new dimension. A lone point waits
    // for a second point or a line.
    const bool waiting = next.entities.empty() && next.points.size() == 1;
    if (!waiting && !fitsDimension(next)) {
        next = {};
        add(next);
    }
    setSelection(next);
    refreshSketches();
    if (fitsDimension(next))
        applyConstraint(ConstraintCommand::Dimension); // asks for the value right away
    updateHint();
}

void SketchEditor::editDimension(ConstraintId id)
{
    const SketchConstraint* c = sketch().constraint(id);
    if (!c || !isDimension(c->kind))
        return;
    // Over the dimension's own label, so a double tap lands on the field, not past it.
    QPointF anchor = m_pointerPos;
    for (const Label& label : m_labels) {
        if (label.id == id)
            anchor = label.rect.center();
    }
    promptDimension(*c, anchor, [this, id](double value) {
        Sketch candidate = sketch();
        candidate.constraint(id)->value = value;
        if (validate(candidate)) {
            commit([&](Sketch& s) { s = std::move(candidate); });
            m_viewer->update();
        }
    });
}

bool SketchEditor::validate(Sketch& candidate)
{
    const SolveResult after = solve(candidate);
    if (!after.ok) {
        emit message(tr("The constraints conflict; change rejected"));
        return false;
    }
    // Each new equation (and each merged point) should take away one degree of freedom;
    // if fewer went, the change only restates something already determined.
    const int expected = (after.equations - m_analysis.equations) + (m_analysis.variables - after.variables);
    if (m_analysis.dof - after.dof < expected) {
        emit message(tr("Redundant constraint: this geometry is already defined"));
        return false;
    }
    return true;
}

void SketchEditor::promptDimension(const SketchConstraint& c, const QPointF& anchor,
                                   const std::function<void(double)>& accepted, std::function<void()> cancelled)
{
    const double current = measure(sketch(), c);
    // Distances to a line and angles are signed: keep the side / turning direction.
    const double sign = current < 0.0 ? -1.0 : 1.0;
    double shown = std::abs(current);
    double max = 1e6;
    double toSolver = 1.0;
    switch (c.kind) {
    case ConstraintKind::Distance:
    case ConstraintKind::PointLineDistance:
    case ConstraintKind::LineDistance:
    case ConstraintKind::HorizontalDistance:
    case ConstraintKind::VerticalDistance:
    case ConstraintKind::Radius:
    case ConstraintKind::Diameter:
        break;
    case ConstraintKind::Angle:
        shown *= 180.0 / M_PI;
        max = 180.0;
        toSolver = M_PI / 180.0;
        break;
    default:
        return;
    }
    auto label = [kind = c.kind, ofLine = c.e1 != kNoId] {
        switch (kind) {
        case ConstraintKind::Distance:
            return ofLine ? tr("Length [mm]") : tr("Distance [mm]"); // a line, or two points
        case ConstraintKind::Radius:
            return tr("Radius [mm]");
        case ConstraintKind::Diameter:
            return tr("Diameter [mm]");
        case ConstraintKind::Angle:
            return tr("Angle [°]");
        case ConstraintKind::HorizontalDistance:
            return tr("Horizontal distance [mm]");
        case ConstraintKind::VerticalDistance:
            return tr("Vertical distance [mm]");
        default:
            return tr("Distance [mm]");
        }
    };
    m_valueEditor->open(label, shown, 1e-3, max, anchor,
                        [this, accepted, sign, toSolver](double value) {
                            accepted(sign * value * toSolver);
                            m_viewer->update();
                        },
                        std::move(cancelled));
}

std::optional<gp_Pnt2d> SketchEditor::toSketch(const Graphic3d_Vec2i& pixel) const
{
    const Handle(V3d_View)& view = m_viewer->view();
    if (view.IsNull())
        return std::nullopt;

    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);

    const gp_Ax3& axes = sketch().plane();
    const gp_Vec normal(axes.Direction());
    const gp_Vec ray(dx, dy, dz);
    const double denom = ray.Dot(normal);
    if (std::abs(denom) < 1e-9)
        return std::nullopt; // looking along the plane

    const gp_Pnt origin(x, y, z);
    const double t = gp_Vec(origin, axes.Location()).Dot(normal) / denom;
    return sketch().to2d(origin.Translated(ray * t));
}

double SketchEditor::pixelsToModel(const Graphic3d_Vec2i& pixel, double logicalPixels) const
{
    const int offset = qMax(1, qRound(logicalPixels * m_viewer->devicePixelRatioF()));
    const auto a = toSketch(pixel);
    const auto b = toSketch(pixel + Graphic3d_Vec2i(offset, 0));
    const auto c = toSketch(pixel + Graphic3d_Vec2i(0, offset));
    if (!a || (!b && !c))
        return 0.0;
    // Take the shorter screen axis so tolerances don't explode on steep planes.
    double d = std::numeric_limits<double>::max();
    if (b)
        d = std::min(d, a->Distance(*b));
    if (c)
        d = std::min(d, a->Distance(*c));
    return d;
}

double SketchEditor::pixelsToModel(double logicalPixels) const
{
    const double dpr = m_viewer->devicePixelRatioF();
    const Graphic3d_Vec2i center(qRound(m_viewer->width() * dpr / 2.0), qRound(m_viewer->height() * dpr / 2.0));
    const double d = pixelsToModel(center, logicalPixels);
    return d > 0.0 ? d : logicalPixels * 0.2;
}

void SketchEditor::askValue(std::function<QString()> label, double value, double min, double max,
                            std::function<void(double)> accept, std::function<void()> cancel)
{
    auto refresh = [this] {
        refreshOverlay();
        updateHint();
        m_viewer->update();
    };
    // Just above the pointer, so the field doesn't cover what is being offset.
    m_valueEditor->open(std::move(label), value, min, max, m_pointerPos - QPointF(0, 40),
                        [accept, refresh](double v) {
                            accept(v);
                            refresh();
                        },
                        [cancel, refresh] {
                            if (cancel)
                                cancel();
                            refresh();
                        });
    updateHint();
}

double SketchEditor::pickTolerance() const
{
    return pixelsToModel(m_pointerPixel, kHitPx * m_touchScale);
}

SnapResult SketchEditor::snapAt(const PointerEvent& e) const
{
    SnapResult s;
    const auto raw = toSketch(e.pixel);
    if (!raw)
        return s;
    s.pos = *raw;
    // Alt disables snapping, like most CAD tools.
    if ((e.modifiers & Qt::AltModifier) || (m_tool && !m_tool->snaps()))
        return s;
    return m_snapper.snap(sketch(), *raw, pixelsToModel(e.pixel, kPointSnapPx * m_touchScale),
                          pixelsToModel(e.pixel, kAlignSnapPx * m_touchScale),
                          m_tool ? m_tool->anchors() : std::vector<gp_Pnt2d>{});
}

SketchEditor::Hit SketchEditor::hitAt(const PointerEvent& e) const
{
    // Labels are drawn on top, so they win.
    for (auto it = m_labels.rbegin(); it != m_labels.rend(); ++it) {
        if (it->rect.adjusted(-3, -3, 3, 3).contains(e.pos))
            return {Hit::Kind::Constraint, it->id};
    }
    const auto raw = toSketch(e.pixel);
    if (raw) {
        const double tolerance = pixelsToModel(e.pixel, kHitPx * m_touchScale);
        if (auto p = sketch().findPoint(*raw, tolerance))
            return {Hit::Kind::Point, *p};
        if (auto h = sketch().hitTest(*raw, tolerance))
            return {Hit::Kind::Entity, *h};
    }
    return {};
}

bool SketchEditor::press(const PointerEvent& e)
{
    m_pressChanges = m_changes;
    m_pressPixel = e.pixel;
    m_pointerPixel = e.pixel;
    m_pointerPos = e.pos;
    if (e.eraser) {
        if (mode() != EditorMode::Sketch)
            return false; // nothing to erase in the model
        m_valueEditor->cancel();
        m_erasing = true;
        m_eraseStroke = false;
        eraseAt(e);
        return true;
    }
    if (mode() == EditorMode::Model)
        return modelPress(e); // the field there stays open while picking / dragging
    if (m_valueEditor->isOpen() && m_toolKind == ToolKind::Dimension) {
        // Dimension tool: tapping more geometry while the value is asked turns it into a
        // dimension to that (a line's length into the gap to another line, ...).
        const Hit hit = hitAt(e);
        if (hit.kind == Hit::Kind::Point || hit.kind == Hit::Kind::Entity) {
            m_chainingDimension = true;
            m_valueEditor->cancel();
            m_chainingDimension = false;
            return selectPress(e);
        }
    }
    if (m_valueEditor->isOpen()) {
        // The second tap of a double tap lands where the field opened: keep typing.
        // A tap anywhere else drops the edit, and does nothing more.
        m_swallowRelease = true;
        if (QLineF(e.pos, m_valueEditor->anchor()).length() < 30.0) {
            m_valueEditor->refocus();
            return false;
        }
        m_valueEditor->cancel();
        return true;
    }
    if (m_toolKind == ToolKind::Project) {
        projectAt(e);
        return true;
    }
    if (m_toolKind == ToolKind::Freehand) {
        // Ink from wherever it starts; a tap (no stroke) picks like Select.
        m_pressHit = hitAt(e);
        m_dragging = false;
        m_stroking = true;
        m_stroke.clear();
        m_strokeScreen.clear();
        strokeTo(e);
        return true;
    }
    if (!m_tool)
        return selectPress(e);

    const SnapResult s = snapAt(e);
    m_tool->hover(s);
    m_tool->press(s);
    m_snap = s;
    refreshOverlay();
    updateHint();
    return true;
}

bool SketchEditor::move(const PointerEvent& e, bool pressed)
{
    m_pointerPixel = e.pixel;
    m_pointerPos = e.pos;
    if (pressed && m_swallowRelease)
        return false;
    if (m_erasing) {
        if (pressed)
            eraseAt(e);
        return pressed;
    }
    // The hovering eraser shows the piece it would trim.
    if (!pressed && e.eraser && mode() == EditorMode::Sketch) {
        m_eraserHover.clear();
        if (const auto raw = toSketch(e.pixel)) {
            if (const auto piece = findTrimPiece(sketch(), *raw, pixelsToModel(e.pixel, kHitPx * m_touchScale)))
                m_eraserHover = trimPieceSegments(sketch(), *piece);
        }
        refreshOverlay();
        return true;
    }
    if (!m_eraserHover.empty()) {
        m_eraserHover.clear();
        refreshOverlay();
    }
    if (mode() == EditorMode::Model)
        return modelMove(e, pressed);
    if (m_toolKind == ToolKind::Project) {
        const Hit hit = modelHitAt(e); // highlights the edge / face under the pointer
        const bool changed = !(hit == m_hover);
        m_hover = hit;
        if (changed)
            updateHint();
        return true;
    }
    if (m_toolKind == ToolKind::Freehand) {
        if (pressed && m_stroking) {
            strokeTo(e);
            return true;
        }
        return selectMove(e, false); // hovering: what a tap would pick
    }
    if (!m_tool)
        return selectMove(e, pressed);

    const SnapResult s = snapAt(e);
    m_tool->hover(s);
    m_snap = s;
    refreshOverlay();
    updateHint();
    return true;
}

bool SketchEditor::release(const PointerEvent& e)
{
    m_pointerPixel = e.pixel;
    m_pointerPos = e.pos;
    if (m_swallowRelease) {
        m_swallowRelease = false;
        return false;
    }
    if (m_erasing) {
        m_erasing = false;
        return true;
    }
    if (mode() == EditorMode::Model)
        return modelRelease(e);
    if (m_toolKind == ToolKind::Project)
        return true;
    if (m_toolKind == ToolKind::Freehand) {
        if (m_stroking)
            strokeTo(e);
        return selectRelease(e); // a stroke becomes its shape, a tap picks
    }
    if (!m_tool)
        return selectRelease(e);

    const Graphic3d_Vec2i d = e.pixel - m_pressPixel;
    const double threshold = kDragThresholdPx * m_viewer->devicePixelRatioF();
    const bool dragged = d.x() * d.x() + d.y() * d.y() > threshold * threshold;

    const SnapResult s = snapAt(e);
    m_tool->hover(s);
    m_tool->release(s, dragged);
    m_snap = s;
    refreshOverlay();
    updateHint();
    return true;
}

void SketchEditor::eraseAt(const PointerEvent& e)
{
    const auto raw = toSketch(e.pixel);
    if (!raw)
        return;
    const auto piece = findTrimPiece(sketch(), *raw, pixelsToModel(e.pixel, kHitPx * m_touchScale));
    if (!piece)
        return;
    commit([&](Sketch& s) { trim(s, *piece); }, m_eraseStroke); // one stroke, one undo step
    m_eraseStroke = true;
    m_viewer->update();
}

void SketchEditor::strokeTo(const PointerEvent& e)
{
    const auto p = toSketch(e.pixel);
    if (!p)
        return;
    m_stroke.push_back(*p);
    m_strokeScreen.push_back(e.pos);
}

bool SketchEditor::strokeMoved() const
{
    const double threshold = kDragThresholdPx;
    return std::any_of(m_strokeScreen.begin(), m_strokeScreen.end(), [&](const QPointF& q) {
        return QLineF(m_strokeScreen.front(), q).length() > threshold;
    });
}

bool SketchEditor::finishStroke(bool shapes)
{
    const StrokeShape shape = recognizeStroke(m_stroke, pickTolerance());
    bool used = true;
    if (shape.kind == StrokeShape::Kind::Scribble) {
        scribbleOut();
    } else if (!shapes) {
        used = false;
    } else if (shape.kind == StrokeShape::Kind::None) {
        emit message(tr("Freehand: not recognized  ·  draw a line, corners, a rectangle, a circle or an arc"));
    } else {
        const double snap = pixelsToModel(m_pointerPixel, kPointSnapPx * m_touchScale);
        setSelection({});
        commit([&](Sketch& s) { addStrokeShape(s, shape, snap); });
    }
    m_stroking = false;
    m_stroke.clear();
    m_strokeScreen.clear();
    m_viewer->update();
    return used;
}

void SketchEditor::scribbleOut()
{
    const std::set<EntityId> curves = entitiesCrossed(sketch(), m_stroke, 0.6 * pickTolerance());
    // Dimension labels and relation glyphs it went over (checked every few pixels).
    std::set<ConstraintId> labels;
    for (std::size_t i = 1; i < m_strokeScreen.size(); ++i) {
        const QLineF piece(m_strokeScreen[i - 1], m_strokeScreen[i]);
        const int steps = std::max(1, static_cast<int>(piece.length() / 3.0));
        for (int k = 0; k <= steps; ++k) {
            const QPointF q = piece.pointAt(static_cast<double>(k) / steps);
            for (const Label& label : m_labels) {
                if (label.rect.contains(q))
                    labels.insert(label.id);
            }
        }
    }
    if (curves.empty() && labels.empty()) {
        emit message(tr("Nothing under the scribble to delete"));
        return;
    }
    setSelection({});
    m_hover = {};
    commit([&](Sketch& s) {
        s.removeConstraints(labels);
        s.removeEntities(curves);
    });
    emit message(tr("Scribbled out"));
}

bool SketchEditor::selectPress(const PointerEvent& e)
{
    m_pressHit = hitAt(e);
    m_dragging = false;
    // From empty space a drag is ink that can scribble things out.
    m_stroking = m_pressHit.kind == Hit::Kind::None && m_toolKind == ToolKind::Select;
    m_stroke.clear();
    m_strokeScreen.clear();
    if (m_stroking)
        strokeTo(e);
    return false;
}

bool SketchEditor::selectMove(const PointerEvent& e, bool pressed)
{
    if (!pressed) {
        const Hit hit = hitAt(e);
        if (hit == m_hover)
            return false;
        m_hover = hit;
        refreshSketches();
        return true;
    }

    if (m_stroking && m_toolKind == ToolKind::Select) {
        strokeTo(e);
        return true;
    }
    if (m_pressHit.kind != Hit::Kind::Point && m_pressHit.kind != Hit::Kind::Entity)
        return false;

    if (!m_dragging) {
        const Graphic3d_Vec2i d = e.pixel - m_pressPixel;
        const double threshold = kDragThresholdPx * m_viewer->devicePixelRatioF();
        const auto from = toSketch(m_pressPixel);
        if (d.x() * d.x() + d.y() * d.y() <= threshold * threshold || !from)
            return false;

        m_dragFrom = *from;
        m_dragStart.clear();
        const Sketch& s = sketch();
        if (m_pressHit.kind == Hit::Kind::Point) {
            m_dragStart[m_pressHit.id] = s.pos(m_pressHit.id);
        } else if (const SketchEntity* ent = s.entity(m_pressHit.id)) {
            for (PointId p : ent->pointIds())
                m_dragStart[p] = s.pos(p);
        }
        m_valueEditor->cancel();
        m_undo.push_back(snapshot());
        m_redo.clear();
        m_dragging = true;
    }

    const auto to = toSketch(e.pixel);
    if (!to)
        return false;
    const gp_Vec2d delta(m_dragFrom, *to);
    std::map<PointId, gp_Pnt2d> targets;
    for (const auto& [id, start] : m_dragStart)
        targets[id] = start.Translated(delta);
    // Keep the last good state when the constraints can't follow at all.
    solve(m_slots[m_active].sketch, targets, false);
    refreshSketches();
    return true;
}

bool SketchEditor::selectRelease(const PointerEvent& e)
{
    const Hit hit = m_pressHit;
    m_pressHit = {};
    if (m_stroking) {
        // Freehand turns the stroke into a shape; Select only takes scribbles.
        const bool stroke = strokeMoved();
        if (stroke && finishStroke(m_toolKind == ToolKind::Freehand))
            return true;
        m_stroking = false;
        m_stroke.clear();
        m_strokeScreen.clear();
        if (stroke)
            return true; // a drag over nothing: no tap either
    }
    if (m_dragging) {
        m_dragging = false;
        m_dragStart.clear();
        sketchChanged();
        refreshSketches();
        emit historyChanged();
        return true;
    }

    // Tapping a dimension that is already selected opens it for editing.
    const SketchConstraint* c = hit.kind == Hit::Kind::Constraint ? sketch().constraint(hit.id) : nullptr;
    if (m_toolKind == ToolKind::Dimension) {
        dimensionTap(hit);
        return true;
    }
    if (c && isDimension(c->kind) && m_selection.constraints.count(hit.id)) {
        editDimension(hit.id);
        return true;
    }
    toggle(hit, true);
    return true;
}

void SketchEditor::toggle(const Hit& hit, bool additive)
{
    SketchSelection selection = additive ? m_selection : SketchSelection{};
    auto flip = [](std::set<int>& set, int id) {
        if (!set.erase(id))
            set.insert(id);
    };
    switch (hit.kind) {
    case Hit::Kind::None:
        selection = {};
        break;
    case Hit::Kind::Point:
        flip(selection.points, hit.id);
        break;
    case Hit::Kind::Entity:
        flip(selection.entities, hit.id);
        break;
    case Hit::Kind::Constraint:
        flip(selection.constraints, hit.id);
        break;
    default:
        break; // model-mode picks never reach the sketch selection
    }
    setSelection(std::move(selection));
    refreshSketches();
}

bool SketchEditor::leave()
{
    const bool had = m_snap.has_value() || m_hover.kind != Hit::Kind::None || !m_eraserHover.empty();
    m_snap.reset();
    m_hover = {};
    m_eraserHover.clear();
    if (had) {
        showHoverRegion();
        refreshSketches();
        refreshOverlay();
    }
    return had;
}

void SketchEditor::abortPress()
{
    m_stroking = false;
    m_stroke.clear();
    m_strokeScreen.clear();
    m_erasing = false;
    m_swallowRelease = false;
    m_pressHit = {};
    m_modelPress = {};
    m_session.dragging = false;
    // Whatever the press already changed (a trim, a segment closing a polyline) goes.
    if (m_changes != m_pressChanges && !m_undo.empty()) {
        undo();
        m_redo.pop_back();
        emit historyChanged();
    }
    if (mode() == EditorMode::Sketch) {
        m_valueEditor->cancel();
        if (m_tool)
            m_tool->cancel();
    }
    m_snap.reset();
    refreshOverlay();
    updateHint();
    m_viewer->update();
}

void SketchEditor::cancel()
{
    if (m_valueEditor->isOpen()) {
        m_valueEditor->cancel();
        return;
    }
    if (m_patternEdit) {
        endPatternEdit(false);
        return;
    }
    if (mode() == EditorMode::Model) {
        // First drop what is picked, then leave the tool.
        if (m_toolKind == ToolKind::Move && (m_move.anchor || !m_move.bodies.empty())) {
            if (m_move.anchor)
                m_move.anchor.reset();
            else
                endMove();
            updateHint();
        } else if (m_toolKind == ToolKind::Measure && !m_measured.empty()) {
            m_measured.clear();
            refreshMeasure();
        } else if (solidCancel()) {
            // the solid tool's picks went
        } else if (!m_regionSelection.empty() || !m_bodySelection.empty() || m_selectedDatum != 0) {
            endValueSession(true);
            clearModelSelection();
        } else if (m_toolKind != ToolKind::Select) {
            setTool(ToolKind::Select);
        }
    } else if (m_tool && m_tool->cancel()) {
        refreshOverlay();
        updateHint();
    } else if (m_tool || (m_toolKind == ToolKind::Dimension && m_selection.empty())) {
        setTool(ToolKind::Select);
    } else {
        setSelection({});
        refreshSketches();
    }
    m_viewer->update();
}

void SketchEditor::confirm()
{
    if (m_valueEditor->isOpen()) {
        m_valueEditor->accept();
        return;
    }
    if (m_patternEdit) {
        acceptPattern();
        return;
    }
    if (solidConfirm())
        return;
    if (mode() == EditorMode::Model && m_toolKind == ToolKind::Move && !m_move.bodies.empty()) {
        askMoveValues();
        return;
    }
    if (m_tool && m_tool->confirm()) {
        refreshOverlay();
        updateHint();
        m_viewer->update();
    } else if (mode() == EditorMode::Sketch && !m_tool && m_selection.size() == 1
               && m_selection.constraints.size() == 1) {
        editDimension(*m_selection.constraints.begin());
    }
}

void SketchEditor::setSelection(SketchSelection selection)
{
    if (m_active < 0)
        selection = {};
    // Drop ids that no longer exist (after undo, delete, merge).
    const Sketch& s = sketch();
    std::erase_if(selection.points, [&](PointId id) { return !s.point(id); });
    std::erase_if(selection.entities, [&](EntityId id) { return !s.entity(id); });
    std::erase_if(selection.constraints, [&](ConstraintId id) { return !s.constraint(id); });
    if (selection == m_selection)
        return;
    m_selection = std::move(selection);
    emit selectionChanged();
}

void SketchEditor::resetInteraction()
{
    endPatternEdit(false);
    endValueSession(true);
    m_valueEditor->cancel();
    if (m_tool)
        m_tool->cancel();
    m_snap.reset();
    m_hover = {};
    m_pressHit = {};
    m_dragging = false;
    m_stroking = false;
    m_stroke.clear();
    m_strokeScreen.clear();
    showHoverRegion();
    updateHint();
}

void SketchEditor::sketchChanged()
{
    if (m_active >= 0) {
        m_analysis = analyze(sketch());
        refreshRegions(m_active);
    }
    updateStatus();
}

void SketchEditor::refreshSketches()
{
    const auto& ctx = m_viewer->aisContext();
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        Slot& slot = m_slots[i];
        const bool active = i == m_active;

        std::set<EntityId> accent;
        std::set<PointId> accentPoints;
        std::set<EntityId> defined;
        if (active) {
            accent = m_selection.entities;
            accentPoints = m_selection.points;
            if (m_hover.kind == Hit::Kind::Entity)
                accent.insert(m_hover.id);
            if (m_hover.kind == Hit::Kind::Point)
                accentPoints.insert(m_hover.id);
            defined = m_analysis.definedEntities;
        }
        PlanarPrs::Style style{active ? m_colors.sketch : m_colors.inactive, m_colors.accent,
                               (active ? 2.0 : 1.5) * m_viewer->devicePixelRatioF(), Aspect_TOL_SOLID,
                               active ? 1.0 : 0.0};
        style.definedColor = m_colors.defined;
        slot.prs->setStyle(style);
        slot.prs->setDrawList(tessellate(slot.sketch, accent, defined, accentPoints));
        ctx->Redisplay(slot.prs, false);
    }
    refreshImage();
}

void SketchEditor::refreshOverlay()
{
    DrawList preview;
    if (m_tool && m_snap)
        m_tool->preview(preview);
    m_previewPrs->setDrawList(std::move(preview));

    DrawList overlay;
    if (m_tool && m_snap) {
        overlay.segments = m_snap->guides;
        if (m_snap->snapped())
            overlay.accentPoints.push_back(m_snap->pos);
    }
    overlay.accentSegments = m_eraserHover;
    m_overlayPrs->setDrawList(std::move(overlay));

    const auto& ctx = m_viewer->aisContext();
    ctx->Redisplay(m_previewPrs, false);
    ctx->Redisplay(m_overlayPrs, false);
}

QString SketchEditor::modelText(const std::string& message)
{
    // Messages from the model core are English, marked with QT_TRANSLATE_NOOP("Model", ...).
    return message.empty() ? QString() : QCoreApplication::translate("Model", message.c_str());
}

void SketchEditor::retranslate()
{
    updateHint();
    updateStatus();
    emit historyChanged(); // the timeline's names
}

void SketchEditor::updateHint()
{
    emit patternChanged(); // the panel follows the picks and values
    if (m_patternEdit) {
        const bool circular = m_patternEdit->draft.pattern == Feature::PatternKind::Circular;
        const QString name = circular ? tr("Circular pattern") : tr("Linear pattern");
        const QString error = modelText(m_solidError);
        emit hintChanged(!error.isEmpty() ? QStringLiteral("%1: %2").arg(name, error)
                                          : tr("%1: change the values in the panel  ·  Enter accepts  ·  Esc puts it back")
                                                .arg(name));
        return;
    }
    if (mode() == EditorMode::Model && isSolidTool(m_toolKind)) {
        emit hintChanged(solidHint());
        return;
    }
    if (mode() == EditorMode::Model && m_toolKind == ToolKind::Move) {
        emit hintChanged(m_valueEditor->isOpen() ? tr("Move: type the offset or the turn  ·  Enter goes on")
                         : m_move.bodies.empty() ? tr("Move: tap the bodies to move or copy")
                         : m_move.anchor ? tr("Move: tap where it goes: a corner, round edge or face of another body")
                                         : tr("Move: drag an arrow or ring, or tap a corner, round edge or face of it and "
                                              "then where it goes  ·  Enter types values and applies"));
        return;
    }
    if (mode() == EditorMode::Model && m_toolKind == ToolKind::Measure) {
        emit hintChanged(tr("Measure: tap a face, edge or corner, and a second one to measure between them"));
        return;
    }
    if (mode() == EditorMode::Model && m_toolKind == ToolKind::Section) {
        emit hintChanged(m_valueEditor->isOpen() ? tr("Section: type how far to shift the cut  ·  Enter accepts")
                         : sectionActive() ? tr("Section: tap another face or plane to cut along it  ·  "
                                                "View → Section View turns it off")
                                           : tr("Section: tap a flat face or a plane to cut the model open along it"));
        return;
    }
    if (mode() == EditorMode::Model) {
        const bool open = m_session.kind != ValueSession::Kind::None;
        const QString error = modelText(m_session.error);
        switch (m_toolKind) {
        case ToolKind::PushPull:
            emit hintChanged(open ? tr("Extrude: drag the arrow or type the distance  ·  Enter accepts, Esc cancels")
                                  : tr("Extrude: tap a sketch region or a flat face of a body"));
            return;
        case ToolKind::Fillet:
            emit hintChanged(!error.isEmpty() ? tr("Fillet: %1").arg(error)
                             : open ? tr("Fillet: tap more edges, type the radius  ·  Enter accepts")
                                    : tr("Fillet: tap edges of a body"));
            return;
        case ToolKind::Chamfer:
            emit hintChanged(!error.isEmpty() ? tr("Chamfer: %1").arg(error)
                             : open ? tr("Chamfer: tap more edges, type the size  ·  Enter accepts")
                                    : tr("Chamfer: tap edges of a body"));
            return;
        case ToolKind::Shell:
            emit hintChanged(!error.isEmpty() ? tr("Shell: %1").arg(error)
                             : open ? tr("Shell: tap more faces to open, type the wall thickness  ·  Enter accepts")
                                    : tr("Shell: tap the faces to open"));
            return;
        default:
            emit hintChanged(open ? tr("Drag the arrow or type the distance  ·  Enter extrudes, Esc cancels")
                                  : tr("Tap a sketch region to extrude it, or a face of a body  ·  "
                                       "New Sketch to draw"));
            return;
        }
    }
    if (m_toolKind == ToolKind::Project) {
        emit hintChanged(m_bodies.empty() ? tr("Project: there are no bodies to project")
                         : m_hover.kind == Hit::Kind::Edge ? tr("Project: tap to copy this edge into the sketch")
                         : m_hover.kind == Hit::Kind::Face ? tr("Project: tap to copy this face's outline into the sketch")
                                                           : tr("Project: tap a body edge or face"));
        return;
    }
    if (m_tool) {
        QString hint = m_tool->hint();
        if (m_snap) {
            switch (m_snap->kind) {
            case SnapResult::Kind::Point:    hint += tr("  ·  point"); break;
            case SnapResult::Kind::Midpoint: hint += tr("  ·  midpoint"); break;
            case SnapResult::Kind::Origin:   hint += tr("  ·  origin"); break;
            case SnapResult::Kind::Aligned:  hint += tr("  ·  aligned"); break;
            case SnapResult::Kind::None:     break;
            }
        }
        emit hintChanged(hint);
        return;
    }
    if (m_toolKind == ToolKind::Dimension) {
        if (m_valueEditor->isOpen())
            emit hintChanged(tr("Dimension: type the value, or tap another point or line to measure to it  ·  "
                                "Enter accepts, Esc cancels"));
        else if (m_selection.entities.empty() && m_selection.points.size() == 1)
            emit hintChanged(tr("Dimension: tap a second point, or a line"));
        else
            emit hintChanged(tr("Dimension: tap a line, circle or arc, or two points, a point and a line, two lines"
                                "  ·  Esc goes back to Select"));
        return;
    }
    if (m_toolKind == ToolKind::Freehand) {
        emit hintChanged(tr("Freehand: draw a line, corners, a rectangle, a circle or an arc  ·  "
                            "scribble over something to delete it  ·  tap to select"));
        return;
    }
    emit hintChanged(tr("Select: tap to add  ·  drag to move  ·  tap a selected dimension to change it  ·  "
                        "scribble over something to delete it"));
}

void SketchEditor::updateStatus()
{
    if (m_active < 0) {
        emit statusChanged(tr("Bodies: %1").arg(bodyCount()));
        return;
    }
    const Sketch& s = sketch();
    if (s.entities().empty())
        emit statusChanged(tr("Sketch: empty"));
    else if (!m_analysis.ok)
        emit statusChanged(tr("Sketch: conflicting constraints"));
    else if (m_analysis.dof == 0)
        emit statusChanged(tr("Sketch: fully defined"));
    else
        emit statusChanged(tr("Sketch: %1 DOF").arg(m_analysis.dof));
}

void SketchEditor::paintOverlay(QPainter& p)
{
    m_labels.clear();
    if (m_active < 0) {
        paintArrow(p);
        if (m_toolKind == ToolKind::Measure)
            paintMeasure(p);
        return;
    }
    const Sketch& s = sketch();
    auto screen = [&](const gp_Pnt2d& q) { return m_viewer->project(s.to3d(q)); };
    auto accented = [&](ConstraintId id) {
        return m_selection.constraints.count(id) || (m_hover.kind == Hit::Kind::Constraint && m_hover.id == id);
    };

    QFont font = p.font();
    font.setPointSizeF(9.5);
    font.setBold(true);
    p.setFont(font);
    const QFontMetricsF metrics(font);

    // Draws a value badge centered at `at` and remembers it for picking.
    auto badge = [&](ConstraintId id, const QPointF& at, const QString& text) {
        const bool hot = accented(id);
        const QSizeF size = metrics.size(Qt::TextSingleLine, text) + QSizeF(10, 4);
        const QRectF rect(at - QPointF(size.width() / 2, size.height() / 2), size);
        p.setPen(QPen(hot ? m_colors.overlayAccent : m_colors.dimensionInk, 1.0));
        p.setBrush(hot ? m_colors.overlayAccent : m_colors.badgeFill);
        p.drawRoundedRect(rect, 4, 4);
        p.setPen(hot ? Qt::white : m_colors.dimensionInk);
        p.drawText(rect, Qt::AlignCenter, text);
        m_labels.push_back({id, rect});
    };

    // --- Dimensions ------------------------------------------------------------------
    for (const SketchConstraint& c : s.constraints()) {
        if (!isDimension(c.kind))
            continue;
        const QColor ink = accented(c.id) ? m_colors.overlayAccent : m_colors.dimensionInk;
        p.setPen(QPen(ink, 1.2));
        p.setBrush(Qt::NoBrush);

        switch (c.kind) {
        case ConstraintKind::Distance: {
            const gp_Pnt2d a = s.pos(c.p1), b = s.pos(c.p2);
            if (a.Distance(b) < 1e-9)
                break;
            const gp_Vec2d d = gp_Vec2d(a, b).Normalized();
            const gp_Vec2d n(-d.Y(), d.X());
            const QPointF sa = screen(a), sb = screen(b);
            const QPointF da = screen(a.Translated(n * c.offset)), db = screen(b.Translated(n * c.offset));
            const QPointF overshoot = unit(da - sa) * 5.0;
            p.drawLine(sa, da + overshoot);
            p.drawLine(sb, db + overshoot);
            p.drawLine(da, db);
            drawArrow(p, da, db, ink);
            drawArrow(p, db, da, ink);
            badge(c.id, (da + db) / 2.0, dimensionText(c));
            break;
        }
        case ConstraintKind::HorizontalDistance:
        case ConstraintKind::VerticalDistance: {
            // Extension lines straight out to a dimension line along X (or Y), past both points.
            const bool horizontal = c.kind == ConstraintKind::HorizontalDistance;
            const gp_Pnt2d a = s.pos(c.p1), b = s.pos(c.p2);
            const gp_Vec2d out = horizontal ? gp_Vec2d(0.0, 1.0) : gp_Vec2d(1.0, 0.0);
            const double level = horizontal ? std::max(a.Y(), b.Y()) + c.offset : std::max(a.X(), b.X()) + c.offset;
            auto onLevel = [&](const gp_Pnt2d& p) {
                return horizontal ? gp_Pnt2d(p.X(), level) : gp_Pnt2d(level, p.Y());
            };
            const gp_Pnt2d da = onLevel(a), db = onLevel(b);
            if (da.Distance(db) < 1e-9)
                break;
            const QPointF sa = screen(a), sb = screen(b), sda = screen(da), sdb = screen(db);
            const QPointF overshoot = unit(screen(da.Translated(out)) - sda) * 5.0;
            p.drawLine(sa, sda + overshoot);
            p.drawLine(sb, sdb + overshoot);
            p.drawLine(sda, sdb);
            drawArrow(p, sda, sdb, ink);
            drawArrow(p, sdb, sda, ink);
            badge(c.id, (sda + sdb) / 2.0, dimensionText(c));
            break;
        }
        case ConstraintKind::PointLineDistance:
        case ConstraintKind::LineDistance: {
            const SketchEntity* line = s.entity(c.e1);
            const SketchEntity* other = c.kind == ConstraintKind::LineDistance ? s.entity(c.e2) : nullptr;
            if (!line || (c.kind == ConstraintKind::LineDistance && !other))
                break;
            const gp_Pnt2d a = s.pos(line->a), b = s.pos(line->b);
            const gp_Vec2d d(a, b);
            if (d.Magnitude() < 1e-9)
                break;
            // From the point (or the other line's middle) straight across to the line.
            const gp_Pnt2d from = other ? gp_Pnt2d((s.pos(other->a).XY() + s.pos(other->b).XY()) / 2.0)
                                        : s.pos(c.p1);
            const double t = gp_Vec2d(a, from).Dot(d) / d.SquareMagnitude();
            const gp_Pnt2d foot = a.Translated(d * t);
            const QPointF sf = screen(from), sfoot = screen(foot);
            p.drawLine(sf, sfoot);
            drawArrow(p, sf, sfoot, ink);
            drawArrow(p, sfoot, sf, ink);
            if (t < 0.0 || t > 1.0) {
                // The foot lies past the line's end: extend it there, dashed.
                QPen dashed(ink, 1.0, Qt::DashLine);
                p.setPen(dashed);
                p.drawLine(screen(t < 0.0 ? a : b), sfoot);
                p.setPen(QPen(ink, 1.2));
            }
            badge(c.id, (sf + sfoot) / 2.0, dimensionText(c));
            break;
        }
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter: {
            const SketchEntity* e = s.entity(c.e1);
            if (!e)
                break;
            const gp_Pnt2d center = s.pos(e->c);
            const double r = c.kind == ConstraintKind::Radius ? c.value : c.value / 2.0;
            double angle = c.offset;
            if (e->kind == EntityKind::Arc) {
                // Point the leader at the middle of the arc so it always lands on it.
                const gp_Pnt2d from = s.pos(e->a), to = s.pos(e->b);
                const double a0 = std::atan2(from.Y() - center.Y(), from.X() - center.X());
                double sweep = std::atan2(to.Y() - center.Y(), to.X() - center.X()) - a0;
                if (sweep <= 0.0)
                    sweep += 2.0 * M_PI;
                angle = a0 + sweep / 2.0;
            }
            const gp_Vec2d u(std::cos(angle), std::sin(angle));
            const QPointF rim = screen(center.Translated(u * r));
            const QPointF sc = screen(center);
            const QPointF start = c.kind == ConstraintKind::Diameter ? screen(center.Translated(-u * r)) : sc;
            const QPointF label = rim + unit(rim - sc) * 26.0;
            p.drawLine(start, label);
            drawArrow(p, rim, sc, ink);
            if (c.kind == ConstraintKind::Diameter)
                drawArrow(p, start, sc, ink);
            badge(c.id, label, dimensionText(c));
            break;
        }
        case ConstraintKind::Angle: {
            const SketchEntity* e1 = s.entity(c.e1);
            const SketchEntity* e2 = s.entity(c.e2);
            if (!e1 || !e2)
                break;
            const gp_Vec2d d1 = rayDirection(s, *e1, c.p1);
            const gp_Vec2d d2 = rayDirection(s, *e2, c.p1);
            if (d1.Magnitude() < 1e-9 || d2.Magnitude() < 1e-9)
                break;
            // Corner: the shared point, else where the two lines meet.
            gp_Pnt2d corner;
            if (c.p1 != kNoId) {
                corner = s.pos(c.p1);
            } else {
                const gp_Pnt2d a1 = s.pos(e1->a), a2 = s.pos(e2->a);
                const double den = d1.Crossed(d2);
                if (std::abs(den) < 1e-12)
                    break;
                const double t = gp_Vec2d(a1, a2).Crossed(d2) / den;
                corner = a1.Translated(d1 * t);
            }
            const QPointF sc = screen(corner);
            const QPointF r1 = unit(screen(corner.Translated(d1.Normalized())) - sc);
            const QPointF r2 = unit(screen(corner.Translated(d2.Normalized())) - sc);
            // Qt angles are counter-clockwise with y pointing up.
            const double a1 = std::atan2(-r1.y(), r1.x()) * 180.0 / M_PI;
            double span = std::atan2(-r2.y(), r2.x()) * 180.0 / M_PI - a1;
            while (span > 180.0)
                span -= 360.0;
            while (span <= -180.0)
                span += 360.0;
            constexpr double radius = 48.0;
            p.drawArc(QRectF(sc - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius)),
                      qRound(a1 * 16.0), qRound(span * 16.0));
            // Far enough out along the bisector that the badge clears both the arc and the rays.
            const double mid = (a1 + span / 2.0) * M_PI / 180.0;
            badge(c.id, sc + QPointF(std::cos(mid), -std::sin(mid)) * (radius + 44.0), dimensionText(c));
            break;
        }
        default:
            break;
        }
    }

    // --- Relation glyphs, stacked next to what they constrain -------------------------
    struct Anchor
    {
        QPointF pos;
        QPointF along;
        QPointF out;
    };
    // Key: (0, entity) / (1, point) / (2, constraint for point pairs without a line).
    std::map<std::pair<int, int>, std::vector<const SketchConstraint*>> groups;
    for (const SketchConstraint& c : s.constraints()) {
        if (isDimension(c.kind))
            continue;
        switch (c.kind) {
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical:
            groups[c.e1 != kNoId && s.entity(c.e1) ? std::pair(0, c.e1) : std::pair(2, c.id)].push_back(&c);
            break;
        case ConstraintKind::Symmetric:
            groups[{2, c.id}].push_back(&c); // between its two points, on the axis
            break;
        case ConstraintKind::PointOnEntity:
        case ConstraintKind::Midpoint:
        case ConstraintKind::Fixed:
            groups[{1, c.p1}].push_back(&c);
            break;
        default:
            groups[{0, c.e1}].push_back(&c);
            if (c.e2 != kNoId)
                groups[{0, c.e2}].push_back(&c);
            break;
        }
    }

    auto anchorFor = [&](const std::pair<int, int>& key, const SketchConstraint& first) -> std::optional<Anchor> {
        if (key.first == 1) {
            if (!s.point(key.second))
                return std::nullopt;
            return Anchor{screen(s.pos(key.second)) + QPointF(14, -14), QPointF(1, 0), QPointF(0, 0)};
        }
        if (key.first == 2) {
            const QPointF a = screen(s.pos(first.p1)), b = screen(s.pos(first.p2));
            const QPointF t = unit(b - a);
            QPointF n(-t.y(), t.x());
            if (n.y() > 0)
                n = -n;
            return Anchor{(a + b) / 2.0, t, n * 14.0};
        }
        const SketchEntity* e = s.entity(key.second);
        if (!e)
            return std::nullopt;
        if (!e->isClassic()) {
            // Ellipses and splines: at the middle of the drawn curve.
            const std::vector<gp_Pnt2d> pts = s.polyline(*e);
            if (pts.size() < 2)
                return std::nullopt;
            const std::size_t i = pts.size() / 2;
            const QPointF at = screen(pts[i]);
            const QPointF t = unit(screen(pts[i]) - screen(pts[i - 1]));
            QPointF n(-t.y(), t.x());
            if (n.y() > 0)
                n = -n;
            return Anchor{at, t, n * 16.0};
        }
        if (e->kind == EntityKind::Line) {
            const QPointF a = screen(s.pos(e->a)), b = screen(s.pos(e->b));
            const QPointF t = unit(b - a);
            QPointF n(-t.y(), t.x());
            if (n.y() > 0)
                n = -n; // keep glyphs above the line on screen
            return Anchor{(a + b) / 2.0, t, n * 16.0};
        }
        // Circles: on top of the rim; arcs: middle of the arc.
        const gp_Pnt2d center = s.pos(e->c);
        double angle = M_PI / 2.0;
        if (e->kind == EntityKind::Arc) {
            const gp_Pnt2d from = s.pos(e->a), to = s.pos(e->b);
            const double a0 = std::atan2(from.Y() - center.Y(), from.X() - center.X());
            double sweep = std::atan2(to.Y() - center.Y(), to.X() - center.X()) - a0;
            if (sweep <= 0.0)
                sweep += 2.0 * M_PI;
            angle = a0 + sweep / 2.0;
        }
        const QPointF rim = screen(center.Translated(gp_Vec2d(std::cos(angle), std::sin(angle)) * e->radius));
        return Anchor{rim, QPointF(1, 0), unit(rim - screen(center)) * 16.0};
    };

    constexpr double glyph = 18.0;
    for (const auto& [key, list] : groups) {
        const auto anchor = anchorFor(key, *list.front());
        if (!anchor)
            continue;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const SketchConstraint& c = *list[i];
            const double shift = (static_cast<double>(i) - (key.first == 1 ? 0.0 : (list.size() - 1) / 2.0)) * (glyph + 3.0);
            const QPointF at = anchor->pos + anchor->out + anchor->along * shift;
            const QRectF rect(at - QPointF(glyph / 2, glyph / 2), QSizeF(glyph, glyph));
            const bool hot = accented(c.id);
            QColor border = m_colors.glyphInk;
            border.setAlpha(110);
            p.setPen(QPen(hot ? m_colors.overlayAccent : border, 1.0));
            p.setBrush(hot ? m_colors.overlayAccent : m_colors.badgeFill);
            p.drawRoundedRect(rect, 4, 4);
            drawConstraintGlyph(p, c.kind, rect, hot ? Qt::white : m_colors.glyphInk);
            m_labels.push_back({c.id, rect});
        }
    }

    // Freehand ink, on top of everything.
    if (m_stroking && m_strokeScreen.size() > 1) {
        QPen ink(m_colors.overlayAccent, 2.5);
        ink.setCapStyle(Qt::RoundCap);
        ink.setJoinStyle(Qt::RoundJoin);
        p.setPen(ink);
        p.setBrush(Qt::NoBrush);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPolyline(m_strokeScreen.data(), static_cast<int>(m_strokeScreen.size()));
    }
}

