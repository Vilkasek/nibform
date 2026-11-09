#include "app/DrawingWidget.h"

#include "sketch/SketchEditor.h"
#include "viewer/UnitText.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSvgGenerator>
#include <QWheelEvent>

#include <cmath>
#include <sstream>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string keyOf(const SheetView& v)
{
    std::ostringstream key;
    key << static_cast<int>(v.kind) << '|' << static_cast<int>(v.base) << '|' << v.offset << '|' << v.hidden << '|'
        << v.scale;
    return key.str();
}

QString nextSectionLabel(const Drawing& d)
{
    for (char c = 'A'; c <= 'Z'; ++c) {
        const bool used = std::any_of(d.views.begin(), d.views.end(), [c](const SheetView& v) {
            return v.kind == ViewKind::Section && v.label == std::string(1, c);
        });
        if (!used)
            return QString(QLatin1Char(c));
    }
    return QStringLiteral("Z");
}

} // namespace

DrawingWidget::DrawingWidget(SketchEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    connect(m_editor, &SketchEditor::drawingsChanged, this, [this] {
        if (isVisible())
            reload();
    });
}

// --- Content ------------------------------------------------------------------------

void DrawingWidget::open(int index)
{
    m_index = index;
    if (static_cast<int>(m_editor->drawings().size()) <= index) {
        const std::vector<TopoDS_Shape> bodies = m_editor->visibleBodies();
        m_editor->setDrawing(index, defaultDrawing(bodies, tr("Part").toStdString()));
    }
    m_fitted = false;
    m_first.reset();
    m_selected = m_hover = {};
    reload();
    fitSheet();
}

void DrawingWidget::reload()
{
    if (m_index < 0 || m_index >= static_cast<int>(m_editor->drawings().size()))
        return;
    m_drawing = m_editor->drawings()[m_index];
    auto valid = [this](const SheetHighlight& h) {
        switch (h.kind) {
        case SheetHighlight::Kind::View: return h.index < static_cast<int>(m_drawing.views.size());
        case SheetHighlight::Kind::Dimension: return h.index < static_cast<int>(m_drawing.dimensions.size());
        case SheetHighlight::Kind::Note: return h.index < static_cast<int>(m_drawing.notes.size());
        default: return true;
        }
    };
    if (!valid(m_selected))
        m_selected = {};
    if (!valid(m_hover))
        m_hover = {};
    regenerate();
    updateHint();
    emit selectionChanged();
    update();
}

void DrawingWidget::regenerate()
{
    const std::vector<TopoDS_Shape> bodies = m_editor->visibleBodies();
    bool modelChanged = bodies.size() != m_bodies.size();
    for (std::size_t i = 0; !modelChanged && i < bodies.size(); ++i)
        modelChanged = !bodies[i].IsSame(m_bodies[i]); // rebuilt bodies are new shapes
    m_bodies = bodies;
    if (modelChanged) {
        Bnd_Box box;
        for (const TopoDS_Shape& b : bodies)
            BRepBndLib::Add(b, box);
        m_middle = box.IsVoid() ? gp::Origin() : gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0);
    }
    m_geometry.resize(m_drawing.views.size());
    m_keys.resize(m_drawing.views.size());
    bool waiting = false;
    for (std::size_t i = 0; i < m_drawing.views.size(); ++i) {
        const std::string key = keyOf(m_drawing.views[i]);
        if (!modelChanged && key == m_keys[i])
            continue;
        if (!waiting) {
            QGuiApplication::setOverrideCursor(Qt::WaitCursor);
            waiting = true;
        }
        m_geometry[i] = projectView(bodies, m_drawing.views[i]);
        m_keys[i] = key;
    }
    if (waiting)
        QGuiApplication::restoreOverrideCursor();

    // Dimensions follow what they measure (without an undo step of their own).
    bool followed = false;
    for (SheetDimension& m : m_drawing.dimensions) {
        if (m.view < 0 || m.view >= static_cast<int>(m_geometry.size()))
            continue;
        gp_Pnt2d a, b;
        double r = 0.0;
        SheetDimension moved = m;
        if (resolveDimension(m, m_geometry[m.view], a, b, r, &moved)
            && (moved.a.Distance(m.a) > 1e-9 || moved.b.Distance(m.b) > 1e-9 || std::abs(moved.radius - m.radius) > 1e-9)) {
            m = moved;
            followed = true;
        }
    }
    if (followed)
        m_editor->setDrawing(m_index, m_drawing, false);
}

void DrawingWidget::commit()
{
    m_editor->setDrawing(m_index, m_drawing); // reloads through drawingsChanged
}

SheetContent DrawingWidget::content() const
{
    SheetContent c;
    c.drawing = &m_drawing;
    c.views = m_geometry;
    c.modelMiddle = m_middle;
    c.units = QString::fromLatin1(lengthSymbol());
    return c;
}

// --- Editing ------------------------------------------------------------------------

void DrawingWidget::setTool(Tool tool)
{
    m_tool = tool;
    m_first.reset();
    m_snap.reset();
    m_hover = {};
    updateHint();
    update();
}

void DrawingWidget::addView(ViewKind kind)
{
    SheetView v;
    v.kind = kind == ViewKind::Section ? ViewKind::Section : kind;
    v.base = ViewKind::Front;
    v.at = gp_Pnt2d(m_drawing.width / 2.0, m_drawing.height / 2.0);
    v.scale = m_drawing.views.empty() ? 1.0 : m_drawing.views.front().scale;
    v.hidden = kind != ViewKind::Iso && kind != ViewKind::Section;
    if (kind == ViewKind::Section) {
        // Across which view: the section looks the same way, cut through the middle.
        const QStringList bases{tr("Front"), tr("Top"), tr("Left"), tr("Right")};
        bool ok = false;
        const QString base = QInputDialog::getItem(this, tr("Section View"), tr("Cut and seen like the view:"), bases, 0, false, &ok);
        if (!ok)
            return;
        static const ViewKind kinds[] = {ViewKind::Front, ViewKind::Top, ViewKind::Left, ViewKind::Right};
        v.base = kinds[bases.indexOf(base)];
        v.label = nextSectionLabel(m_drawing).toStdString();
    }
    m_drawing.views.push_back(v);
    m_selected = {SheetHighlight::Kind::View, static_cast<int>(m_drawing.views.size()) - 1, {}};
    commit();
    emit hintChanged(tr("Drag the new view to its place"));
}

void DrawingWidget::setSheet(double width, double height)
{
    // Views keep their place relative to the top left of the frame.
    const double dy = height - m_drawing.height;
    for (SheetView& v : m_drawing.views)
        v.at.SetY(v.at.Y() + dy);
    for (SheetNote& n : m_drawing.notes)
        n.at.SetY(n.at.Y() + dy);
    m_drawing.width = width;
    m_drawing.height = height;
    commit();
    fitSheet();
}

void DrawingWidget::editTitle()
{
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Drawing Title"), tr("Title:"), QLineEdit::Normal,
                                                QString::fromStdString(m_drawing.title), &ok);
    if (!ok)
        return;
    m_drawing.title = title.toStdString();
    commit();
}

void DrawingWidget::deleteSelected()
{
    const int i = m_selected.index;
    switch (m_selected.kind) {
    case SheetHighlight::Kind::View:
        m_drawing.views.erase(m_drawing.views.begin() + i);
        std::erase_if(m_drawing.dimensions, [i](const SheetDimension& m) { return m.view == i; });
        for (SheetDimension& m : m_drawing.dimensions) {
            if (m.view > i)
                --m.view;
        }
        m_geometry.erase(m_geometry.begin() + i);
        m_keys.erase(m_keys.begin() + i);
        break;
    case SheetHighlight::Kind::Dimension:
        m_drawing.dimensions.erase(m_drawing.dimensions.begin() + i);
        break;
    case SheetHighlight::Kind::Note:
        m_drawing.notes.erase(m_drawing.notes.begin() + i);
        break;
    case SheetHighlight::Kind::None:
        return;
    }
    m_selected = m_hover = {};
    commit();
}

void DrawingWidget::cancel()
{
    if (m_first) {
        m_first.reset();
    } else if (m_selected.kind != SheetHighlight::Kind::None) {
        m_selected = {};
        emit selectionChanged();
    } else if (m_tool != Tool::Select) {
        setTool(Tool::Select);
        return;
    }
    updateHint();
    update();
}

void DrawingWidget::fitSheet()
{
    if (width() < 50 || height() < 50)
        return;
    m_zoom = std::min((width() - 40.0) / m_drawing.width, (height() - 40.0) / m_drawing.height);
    m_origin = QPointF((width() - m_drawing.width * m_zoom) / 2.0, (height() - m_drawing.height * m_zoom) / 2.0);
    m_fitted = true;
    update();
}

void DrawingWidget::updateHint()
{
    switch (m_tool) {
    case Tool::Select:
        emit hintChanged(tr("Drawing: drag views, dimensions and notes  ·  double-tap one for its options  ·  "
                            "right drag pans, the wheel zooms"));
        break;
    case Tool::Dimension:
        emit hintChanged(m_first ? tr("Dimension: tap the second corner or center in the same view")
                                 : tr("Dimension: tap two corners or centers of a view, or the rim of a hole for its size"));
        break;
    case Tool::Note:
        emit hintChanged(tr("Note: tap where the text goes"));
        break;
    }
}

// --- Picking ------------------------------------------------------------------------

gp_Pnt2d DrawingWidget::sheetAt(const QPointF& pos) const
{
    return fromPaint(m_drawing, (pos - m_origin) / m_zoom);
}

SheetHighlight DrawingWidget::hitAt(const QPointF& pos) const
{
    const gp_Pnt2d s = sheetAt(pos);
    const QPointF q(s.X(), s.Y());
    const double tol = pixels(6.0);
    const SheetContent c = content();
    for (int i = static_cast<int>(m_drawing.notes.size()) - 1; i >= 0; --i) {
        if (noteRect(m_drawing.notes[i]).adjusted(-tol, -tol, tol, tol).contains(q))
            return {SheetHighlight::Kind::Note, i, {}};
    }
    for (int i = static_cast<int>(m_drawing.dimensions.size()) - 1; i >= 0; --i) {
        const auto r = dimensionLabelRect(c, i);
        if (r && r->adjusted(-tol, -tol, tol, tol).contains(q))
            return {SheetHighlight::Kind::Dimension, i, {}};
    }
    for (int i = static_cast<int>(m_drawing.views.size()) - 1; i >= 0; --i) {
        if (i < static_cast<int>(m_geometry.size()) && viewRect(c, i).adjusted(-tol, -tol, tol, tol).contains(q))
            return {SheetHighlight::Kind::View, i, {}};
    }
    return {};
}

std::optional<DrawingWidget::Snap> DrawingWidget::snapAt(const QPointF& pos) const
{
    const gp_Pnt2d s = sheetAt(pos);
    std::optional<Snap> best;
    double bestDistance = pixels(14.0);
    for (int i = 0; i < static_cast<int>(m_drawing.views.size()) && i < static_cast<int>(m_geometry.size()); ++i) {
        const SheetView& v = m_drawing.views[i];
        const ViewGeometry& g = m_geometry[i];
        if (v.kind == ViewKind::Iso)
            continue; // not measured true to size
        auto consider = [&](const gp_Pnt& model, const gp_Pnt2d& at) {
            const gp_Pnt2d p = toSheet(v, g, at);
            if (p.Distance(s) < bestDistance) {
                bestDistance = p.Distance(s);
                best = Snap{i, model, p, 0.0, true};
            }
        };
        for (const ViewGeometry::Anchor& c : g.corners)
            consider(c.model, c.at);
        for (const ViewGeometry::Circle& c : g.circles)
            consider(c.center, c.at);
    }
    if (best)
        return best;
    // The rim of a hole or round edge seen face on.
    double rimDistance = pixels(8.0);
    for (int i = 0; i < static_cast<int>(m_drawing.views.size()) && i < static_cast<int>(m_geometry.size()); ++i) {
        const SheetView& v = m_drawing.views[i];
        const ViewGeometry& g = m_geometry[i];
        if (v.kind == ViewKind::Iso)
            continue;
        for (const ViewGeometry::Circle& c : g.circles) {
            if (!c.round)
                continue;
            const gp_Pnt2d center = toSheet(v, g, c.at);
            const double off = std::abs(center.Distance(s) - c.radius * v.scale);
            if (off < rimDistance) {
                rimDistance = off;
                best = Snap{i, c.center, s, c.radius, c.full};
            }
        }
    }
    return best;
}

bool DrawingWidget::dimensionEnds(int index, gp_Pnt2d& a, gp_Pnt2d& b, double& radius) const
{
    const SheetDimension& m = m_drawing.dimensions[index];
    if (m.view < 0 || m.view >= static_cast<int>(m_geometry.size()))
        return false;
    if (!resolveDimension(m, m_geometry[m.view], a, b, radius))
        return false;
    a = toSheet(m_drawing.views[m.view], m_geometry[m.view], a);
    b = toSheet(m_drawing.views[m.view], m_geometry[m.view], b);
    return true;
}

void DrawingWidget::dragTo(const gp_Pnt2d& p)
{
    const gp_Vec2d delta(m_pressSheet, p);
    const int i = m_selected.index;
    switch (m_selected.kind) {
    case SheetHighlight::Kind::View:
        m_drawing.views[i].at = m_beforeDrag.views[i].at.Translated(delta);
        break;
    case SheetHighlight::Kind::Note:
        m_drawing.notes[i].at = m_beforeDrag.notes[i].at.Translated(delta);
        break;
    case SheetHighlight::Kind::Dimension: {
        SheetDimension& m = m_drawing.dimensions[i];
        gp_Pnt2d a, b;
        double r = 0.0;
        if (!dimensionEnds(i, a, b, r))
            break;
        using Kind = SheetDimension::Kind;
        if (m.kind == Kind::Horizontal || m.kind == Kind::Vertical) {
            const bool h = m.kind == Kind::Horizontal;
            const double pa = h ? a.Y() : a.X(), pb = h ? b.Y() : b.X(), at = h ? p.Y() : p.X();
            m.offset = at >= (pa + pb) / 2.0 ? std::max(at - std::max(pa, pb), 0.5) : std::min(at - std::min(pa, pb), -0.5);
        } else if (m.kind == Kind::Distance) {
            const gp_Vec2d u(a, b);
            if (u.Magnitude() > 1e-9) {
                const gp_Vec2d n(-u.Y() / u.Magnitude(), u.X() / u.Magnitude());
                m.offset = gp_Vec2d(a, p).Dot(n);
            }
        } else {
            m.angle = std::atan2(p.Y() - a.Y(), p.X() - a.X()) * 180.0 / kPi;
            const double scale = m_drawing.views[m.view].scale;
            m.offset = std::max(p.Distance(a) - r * scale, 3.0);
        }
        break;
    }
    case SheetHighlight::Kind::None:
        break;
    }
    update();
}

void DrawingWidget::contextMenuFor(const SheetHighlight& hit, const QPoint& global)
{
    QMenu menu(this);
    m_selected = hit;
    update();
    const int i = hit.index;
    switch (hit.kind) {
    case SheetHighlight::Kind::View: {
        SheetView& v = m_drawing.views[i];
        QMenu* scales = menu.addMenu(tr("Scale"));
        for (double s : {10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02}) {
            QAction* a = scales->addAction(QString::fromStdString(scaleText(s)), this, [this, i, s] {
                m_drawing.views[i].scale = s;
                commit();
            });
            a->setCheckable(true);
            a->setChecked(std::abs(v.scale - s) < 1e-9);
        }
        if (v.kind != ViewKind::Iso) {
            QAction* hidden = menu.addAction(tr("Hidden Edges"), this, [this, i](bool on) {
                m_drawing.views[i].hidden = on;
                commit();
            });
            hidden->setCheckable(true);
            hidden->setChecked(v.hidden);
        }
        if (v.kind == ViewKind::Section) {
            menu.addAction(tr("Cut Position…"), this, [this, i] {
                bool ok = false;
                const double value = QInputDialog::getDouble(
                    this, tr("Section"), tr("Cut, from the middle of the part [%1]:").arg(QString::fromLatin1(lengthSymbol())),
                    toDisplayLength(m_drawing.views[i].offset), -1e5, 1e5, lengthDecimals(), &ok);
                if (!ok)
                    return;
                m_drawing.views[i].offset = fromDisplayLength(value);
                commit();
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete View"), this, &DrawingWidget::deleteSelected);
        break;
    }
    case SheetHighlight::Kind::Dimension: {
        using Kind = SheetDimension::Kind;
        const Kind kind = m_drawing.dimensions[i].kind;
        auto setKind = [this, i](Kind k) {
            m_drawing.dimensions[i].kind = k;
            commit();
        };
        if (kind == Kind::Radius || kind == Kind::Diameter) {
            menu.addAction(tr("Radius"), this, [setKind] { setKind(Kind::Radius); })->setEnabled(kind != Kind::Radius);
            menu.addAction(tr("Diameter"), this, [setKind] { setKind(Kind::Diameter); })->setEnabled(kind != Kind::Diameter);
        } else {
            menu.addAction(tr("Aligned"), this, [setKind] { setKind(Kind::Distance); })->setEnabled(kind != Kind::Distance);
            menu.addAction(tr("Horizontal"), this, [setKind] { setKind(Kind::Horizontal); })->setEnabled(kind != Kind::Horizontal);
            menu.addAction(tr("Vertical"), this, [setKind] { setKind(Kind::Vertical); })->setEnabled(kind != Kind::Vertical);
        }
        menu.addSeparator();
        menu.addAction(tr("Delete Dimension"), this, &DrawingWidget::deleteSelected);
        break;
    }
    case SheetHighlight::Kind::Note: {
        menu.addAction(tr("Edit Text…"), this, [this, i] {
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(this, tr("Note"), tr("Text:"),
                                                                QString::fromStdString(m_drawing.notes[i].text), &ok);
            if (!ok || text.trimmed().isEmpty())
                return;
            m_drawing.notes[i].text = text.toStdString();
            commit();
        });
        QMenu* sizes = menu.addMenu(tr("Text Size"));
        for (double h : {2.5, 3.5, 5.0, 7.0, 10.0}) {
            QAction* a = sizes->addAction(QStringLiteral("%1 mm").arg(h), this, [this, i, h] {
                m_drawing.notes[i].height = h;
                commit();
            });
            a->setCheckable(true);
            a->setChecked(std::abs(m_drawing.notes[i].height - h) < 1e-9);
        }
        menu.addSeparator();
        menu.addAction(tr("Delete Note"), this, &DrawingWidget::deleteSelected);
        break;
    }
    case SheetHighlight::Kind::None:
        menu.addAction(tr("Title…"), this, &DrawingWidget::editTitle);
        menu.addAction(tr("Fit the Sheet"), this, &DrawingWidget::fitSheet);
        break;
    }
    menu.exec(global);
}

// --- Events -------------------------------------------------------------------------

void DrawingWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window).darker(115));
    p.translate(m_origin);
    p.scale(m_zoom, m_zoom);
    // A soft shadow under the paper.
    p.fillRect(QRectF(1.2, 1.2, m_drawing.width, m_drawing.height), QColor(0, 0, 0, 50));
    SheetHighlight shown = m_selected.kind != SheetHighlight::Kind::None ? m_selected : m_hover;
    if (m_tool == Tool::Dimension && m_snap)
        shown.snap = m_snap->sheet;
    paintSheet(p, content(), shown);
    if (m_first) {
        p.setPen(QPen(QColor(37, 99, 235), 0.5));
        p.setBrush(QColor(37, 99, 235));
        p.drawEllipse(toPaint(m_drawing, m_first->sheet), 1.0, 1.0);
    }
}

void DrawingWidget::resizeEvent(QResizeEvent*)
{
    if (!m_fitted || isVisible())
        fitSheet();
}

void DrawingWidget::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    m_pressPos = e->position();
    m_pressSheet = sheetAt(m_pressPos);
    m_dragging = false;
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton) {
        m_panning = true;
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    if (m_tool == Tool::Select) {
        const SheetHighlight hit = hitAt(m_pressPos);
        if (hit.kind != SheetHighlight::Kind::None) {
            m_selected = hit;
            m_beforeDrag = m_drawing;
            emit selectionChanged();
            update();
        }
    }
}

void DrawingWidget::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (m_panning) {
        m_origin += pos - m_pressPos;
        m_pressPos = pos;
        update();
        return;
    }
    if (m_pressed && m_tool == Tool::Select && m_selected.kind != SheetHighlight::Kind::None) {
        if (!m_dragging && QLineF(pos, m_pressPos).length() > 4.0)
            m_dragging = true;
        if (m_dragging)
            dragTo(sheetAt(pos));
        return;
    }
    if (m_tool == Tool::Select) {
        const SheetHighlight hit = hitAt(pos);
        if (hit.kind != m_hover.kind || hit.index != m_hover.index) {
            m_hover = hit;
            update();
        }
    } else if (m_tool == Tool::Dimension) {
        m_snap = snapAt(pos);
        update();
    }
}

void DrawingWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panning) {
        m_panning = false;
        return;
    }
    if (e->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    if (m_dragging) {
        m_dragging = false;
        commit();
        return;
    }
    const QPointF pos = e->position();
    switch (m_tool) {
    case Tool::Select:
        m_selected = hitAt(pos);
        emit selectionChanged();
        update();
        break;
    case Tool::Dimension: {
        const auto snap = snapAt(pos);
        if (!snap) {
            m_first.reset();
            break;
        }
        const SheetView& v = m_drawing.views[snap->view];
        const QRectF r = viewRect(content(), snap->view);
        if (snap->radius > 0.0 && !m_first) {
            // A hole's (or round edge's) size, its leader pointing away from the view's middle.
            SheetDimension m;
            m.kind = snap->full ? SheetDimension::Kind::Diameter : SheetDimension::Kind::Radius;
            m.view = snap->view;
            m.a = m.b = snap->model;
            m.radius = snap->radius;
            const gp_Pnt2d c = toSheet(v, m_geometry[snap->view], projectPoint(v.kind == ViewKind::Section ? v.base : v.kind, snap->model));
            m.angle = c.X() >= r.center().x() ? 45.0 : 135.0;
            m.offset = 6.0;
            m_drawing.dimensions.push_back(m);
            commit();
            break;
        }
        if (snap->radius > 0.0)
            break;
        if (!m_first || m_first->view != snap->view) {
            m_first = snap;
            break;
        }
        if (m_first->model.Distance(snap->model) < 1e-9)
            break;
        // Level or upright pairs get that kind; the line goes outside the view.
        SheetDimension m;
        m.view = snap->view;
        m.a = m_first->model;
        m.b = snap->model;
        const gp_Pnt2d a = m_first->sheet, b = snap->sheet;
        const gp_Pnt2d mid((a.XY() + b.XY()) / 2.0);
        if (std::abs(a.Y() - b.Y()) < 1e-6) {
            m.kind = SheetDimension::Kind::Horizontal;
            m.offset = mid.Y() >= r.center().y() ? r.bottom() - a.Y() + 8.0 : r.top() - a.Y() - 8.0;
        } else if (std::abs(a.X() - b.X()) < 1e-6) {
            m.kind = SheetDimension::Kind::Vertical;
            m.offset = mid.X() >= r.center().x() ? r.right() - a.X() + 8.0 : r.left() - a.X() - 8.0;
        } else {
            m.kind = SheetDimension::Kind::Distance;
            const gp_Vec2d u(a, b);
            const gp_Vec2d n(-u.Y() / u.Magnitude(), u.X() / u.Magnitude());
            m.offset = gp_Vec2d(gp_Pnt2d(r.center().x(), r.center().y()), mid).Dot(n) >= 0.0 ? 8.0 : -8.0;
        }
        m_drawing.dimensions.push_back(m);
        m_first.reset();
        commit();
        break;
    }
    case Tool::Note: {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Note"), tr("Text:"), QString(), &ok);
        if (ok && !text.trimmed().isEmpty()) {
            m_drawing.notes.push_back({sheetAt(pos), text.toStdString(), 3.5});
            commit();
        }
        break;
    }
    }
    updateHint();
    update();
}

void DrawingWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = false;
    contextMenuFor(hitAt(e->position()), e->globalPosition().toPoint());
}

void DrawingWidget::wheelEvent(QWheelEvent* e)
{
    // A wheel zooms about the pointer; a touchpad's two-finger scroll pans.
    if (!e->pixelDelta().isNull() && !(e->modifiers() & Qt::ControlModifier)) {
        m_origin += QPointF(e->pixelDelta());
        update();
        return;
    }
    const double factor = std::pow(1.0015, e->angleDelta().y());
    const QPointF at = e->position();
    m_origin = at - (at - m_origin) * factor;
    m_zoom *= factor;
    update();
}

bool DrawingWidget::event(QEvent* e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            const double factor = 1.0 + g->value();
            const QPointF at = g->position();
            m_origin = at - (at - m_origin) * factor;
            m_zoom *= factor;
            update();
            return true;
        }
    }
    return QWidget::event(e);
}

// --- Output -------------------------------------------------------------------------

QString DrawingWidget::exportPdf(const QString& path) const
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QSizeF(m_drawing.width, m_drawing.height), QPageSize::Millimeter));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(600);
    writer.setTitle(QString::fromStdString(m_drawing.title));
    writer.setCreator(QStringLiteral("Nibform"));
    QPainter p;
    if (!p.begin(&writer))
        return tr("Could not write %1").arg(path);
    p.scale(600.0 / 25.4, 600.0 / 25.4);
    paintSheet(p, content());
    p.end();
    return {};
}

QString DrawingWidget::exportSvg(const QString& path) const
{
    constexpr double perMm = 10.0; // 254 dpi: the SVG's size comes out in real millimetres
    QSvgGenerator svg;
    svg.setFileName(path);
    svg.setResolution(254);
    svg.setSize(QSize(qRound(m_drawing.width * perMm), qRound(m_drawing.height * perMm)));
    svg.setViewBox(QRectF(0, 0, m_drawing.width * perMm, m_drawing.height * perMm));
    svg.setTitle(QString::fromStdString(m_drawing.title));
    svg.setDescription(QStringLiteral("Nibform"));
    QPainter p;
    if (!p.begin(&svg))
        return tr("Could not write %1").arg(path);
    p.scale(perMm, perMm);
    paintSheet(p, content());
    p.end();
    return {};
}

void DrawingWidget::print()
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QSizeF(m_drawing.width, m_drawing.height), QPageSize::Millimeter));
    printer.setPageOrientation(m_drawing.width >= m_drawing.height ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer.setDocName(QString::fromStdString(m_drawing.title));
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QPainter p;
    if (!p.begin(&printer))
        return;
    // As large as fits the printable area, keeping the sheet's proportions.
    const QRectF page = printer.pageLayout().paintRectPixels(printer.resolution());
    const double k = std::min(page.width() / m_drawing.width, page.height() / m_drawing.height);
    p.scale(k, k);
    paintSheet(p, content());
    p.end();
}
