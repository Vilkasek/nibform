#include "sketch/SketchEditor.h"

#include "viewer/OcctViewerWidget.h"

#include <QKeyEvent>

#include <gp_Vec.hxx>

#include <cmath>
#include <limits>

namespace {

// Pen-friendly pick radii, in logical pixels.
constexpr double kPointSnapPx = 12.0;
constexpr double kAlignSnapPx = 8.0;
constexpr double kHitPx = 10.0;
constexpr double kDragThresholdPx = 6.0;

const Quantity_Color kSketchColor(0.10, 0.12, 0.16, Quantity_TOC_sRGB);
const Quantity_Color kInactiveColor(0.45, 0.48, 0.53, Quantity_TOC_sRGB);
const Quantity_Color kAccentColor(0.98, 0.45, 0.09, Quantity_TOC_sRGB);
const Quantity_Color kPreviewColor(0.13, 0.42, 0.95, Quantity_TOC_sRGB);

} // namespace

SketchEditor::SketchEditor(OcctViewerWidget* viewer, QObject* parent)
    : QObject(parent)
    , m_viewer(viewer)
{
    const double dpr = viewer->devicePixelRatioF();
    const auto& ctx = viewer->aisContext();

    for (SketchPlane plane : {SketchPlane::XY, SketchPlane::XZ, SketchPlane::YZ}) {
        Slot slot{plane, Sketch(planeAxes(plane)), nullptr};
        slot.prs = new PlanarPrs(slot.sketch.plane(), {kSketchColor, kAccentColor, 2.0 * dpr});
        ctx->Display(slot.prs, 0, -1, false);
        m_slots.push_back(std::move(slot));
    }

    m_previewPrs = new PlanarPrs(planeAxes(SketchPlane::XY),
                                 {kPreviewColor, kPreviewColor, 2.0 * dpr, Aspect_TOL_SOLID, 0.0});
    m_overlayPrs = new PlanarPrs(planeAxes(SketchPlane::XY),
                                 {kAccentColor, kAccentColor, 1.0 * dpr, Aspect_TOL_DASH, 0.0});
    ctx->Display(m_previewPrs, 0, -1, false);
    ctx->Display(m_overlayPrs, 0, -1, false);

    setPlane(SketchPlane::XY);
}

SketchEditor::~SketchEditor() = default;

gp_Ax3 SketchEditor::planeAxes(SketchPlane plane)
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
    if (m_tool)
        m_tool->cancel();
    m_toolKind = kind;
    m_tool = makeTool(kind, *this);
    m_snap.reset();
    m_hover.reset();
    if (kind != ToolKind::Select)
        setSelection({});

    refreshSketches();
    refreshOverlay();
    updateHint();
    m_viewer->update();
    emit toolChanged(kind);
}

void SketchEditor::setPlane(SketchPlane plane)
{
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[i].plane == plane)
            m_active = i;
    }
    resetInteraction();
    setSelection({});

    const gp_Ax3& axes = sketch().plane();
    m_previewPrs->setPlane(axes);
    m_overlayPrs->setPlane(axes);
    // The grid follows the privileged plane, but only picks it up when re-activated.
    const Handle(V3d_Viewer)& viewer = m_viewer->viewer();
    viewer->SetPrivilegedPlane(axes);
    viewer->DeactivateGrid();
    viewer->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
    if (!m_viewer->view().IsNull())
        m_viewer->view()->Invalidate();

    refreshSketches();
    refreshOverlay();
    m_viewer->update();
}

void SketchEditor::commit(const std::function<void(Sketch&)>& change)
{
    m_undo.push_back({m_active, m_slots[m_active].sketch});
    m_redo.clear();
    change(m_slots[m_active].sketch);
    refreshSketches();
    emit historyChanged();
}

void SketchEditor::undo()
{
    if (m_undo.empty())
        return;
    HistoryStep step = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({step.slot, m_slots[step.slot].sketch});
    m_slots[step.slot].sketch = std::move(step.sketch);

    // Tools may hold point ids that no longer exist.
    resetInteraction();
    setSelection({});
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
    emit historyChanged();
}

void SketchEditor::redo()
{
    if (m_redo.empty())
        return;
    HistoryStep step = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({step.slot, m_slots[step.slot].sketch});
    m_slots[step.slot].sketch = std::move(step.sketch);

    resetInteraction();
    setSelection({});
    refreshSketches();
    refreshOverlay();
    m_viewer->update();
    emit historyChanged();
}

void SketchEditor::deleteSelection()
{
    if (m_selection.empty())
        return;
    const std::set<EntityId> doomed = m_selection;
    setSelection({});
    m_hover.reset();
    commit([&](Sketch& s) { s.removeEntities(doomed); });
    m_viewer->update();
}

void SketchEditor::clearActiveSketch()
{
    if (sketch().isEmpty())
        return;
    resetInteraction();
    setSelection({});
    commit([](Sketch& s) { s.clear(); });
    refreshOverlay();
    m_viewer->update();
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

SnapResult SketchEditor::snapAt(const PointerEvent& e) const
{
    SnapResult s;
    const auto raw = toSketch(e.pixel);
    if (!raw)
        return s;
    s.pos = *raw;
    // Alt disables snapping, like most CAD tools.
    if (e.modifiers & Qt::AltModifier)
        return s;
    return m_snapper.snap(sketch(), *raw, pixelsToModel(e.pixel, kPointSnapPx),
                          pixelsToModel(e.pixel, kAlignSnapPx), m_tool ? m_tool->anchors() : std::vector<gp_Pnt2d>{});
}

bool SketchEditor::press(const PointerEvent& e)
{
    m_pressPixel = e.pixel;

    if (!m_tool) {
        const auto raw = toSketch(e.pixel);
        const auto hit = raw ? sketch().hitTest(*raw, pixelsToModel(e.pixel, kHitPx)) : std::nullopt;
        std::set<EntityId> selection = (e.modifiers & Qt::ShiftModifier) ? m_selection : std::set<EntityId>{};
        if (hit) {
            if (selection.count(*hit))
                selection.erase(*hit);
            else
                selection.insert(*hit);
        }
        setSelection(std::move(selection));
        refreshSketches();
        return true;
    }

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
    if (!m_tool) {
        if (pressed)
            return false;
        const auto raw = toSketch(e.pixel);
        const auto hit = raw ? sketch().hitTest(*raw, pixelsToModel(e.pixel, kHitPx)) : std::nullopt;
        if (hit == m_hover)
            return false;
        m_hover = hit;
        refreshSketches();
        return true;
    }

    const SnapResult s = snapAt(e);
    m_tool->hover(s);
    m_snap = s;
    refreshOverlay();
    updateHint();
    return true;
}

bool SketchEditor::release(const PointerEvent& e)
{
    if (!m_tool)
        return false;

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

bool SketchEditor::leave()
{
    const bool had = m_snap.has_value() || m_hover.has_value();
    m_snap.reset();
    m_hover.reset();
    if (had) {
        refreshSketches();
        refreshOverlay();
    }
    return had;
}

bool SketchEditor::key(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_tool && m_tool->cancel()) {
            refreshOverlay();
            updateHint();
        } else if (m_tool) {
            setTool(ToolKind::Select);
        } else {
            setSelection({});
            refreshSketches();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_tool && m_tool->cancel()) {
            refreshOverlay();
            updateHint();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void SketchEditor::setSelection(std::set<EntityId> selection)
{
    if (selection == m_selection)
        return;
    m_selection = std::move(selection);
    emit selectionChanged(static_cast<int>(m_selection.size()));
}

void SketchEditor::resetInteraction()
{
    if (m_tool)
        m_tool->cancel();
    m_snap.reset();
    m_hover.reset();
    updateHint();
}

void SketchEditor::refreshSketches()
{
    const auto& ctx = m_viewer->aisContext();
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        Slot& slot = m_slots[i];
        const bool active = i == m_active;

        std::set<EntityId> accent;
        if (active) {
            accent = m_selection;
            if (m_hover)
                accent.insert(*m_hover);
        }
        slot.prs->setStyle({active ? kSketchColor : kInactiveColor, kAccentColor,
                            (active ? 2.0 : 1.5) * m_viewer->devicePixelRatioF(), Aspect_TOL_SOLID,
                            active ? 1.0 : 0.0});
        slot.prs->setDrawList(tessellate(slot.sketch, accent));
        ctx->Redisplay(slot.prs, false);
    }
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
    m_overlayPrs->setDrawList(std::move(overlay));

    const auto& ctx = m_viewer->aisContext();
    ctx->Redisplay(m_previewPrs, false);
    ctx->Redisplay(m_overlayPrs, false);
}

void SketchEditor::updateHint()
{
    if (m_tool) {
        QString hint = m_tool->hint();
        if (m_snap) {
            switch (m_snap->kind) {
            case SnapResult::Kind::Point:    hint += QStringLiteral("  ·  punkt"); break;
            case SnapResult::Kind::Midpoint: hint += QStringLiteral("  ·  środek odcinka"); break;
            case SnapResult::Kind::Origin:   hint += QStringLiteral("  ·  początek układu"); break;
            case SnapResult::Kind::Aligned:  hint += QStringLiteral("  ·  wyrównanie"); break;
            case SnapResult::Kind::None:     break;
            }
        }
        emit hintChanged(hint);
        return;
    }
    emit hintChanged(QStringLiteral("Zaznacz: dotknij elementu (Shift dodaje)  ·  Delete usuwa"));
}
