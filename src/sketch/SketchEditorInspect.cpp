// SketchEditor, model mode: moving / copying bodies (a gizmo, lining up, typed values),
// measuring, the section view, and how bodies look (names, visibility, colors).

#include "sketch/SketchEditor.h"

#include "model/Measure.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/UnitText.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Prs3d_Drawer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <V3d_View.hxx>
#include <gp_Pln.hxx>

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

QString number(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

QString areaText(double mm2)
{
    const double k = toDisplayLength(1.0);
    return QStringLiteral("%1 %2²").arg(number(mm2 * k * k, lengthDecimals()), QString::fromLatin1(lengthSymbol()));
}

QString volumeText(double mm3)
{
    const double k = toDisplayLength(1.0);
    return QStringLiteral("%1 %2³").arg(number(mm3 * k * k * k, lengthDecimals()), QString::fromLatin1(lengthSymbol()));
}

QString pointText(const gp_Pnt& p)
{
    return QStringLiteral("(%1, %2, %3)").arg(lengthText(p.X(), false), lengthText(p.Y(), false), lengthText(p.Z(), false));
}

QColor toQColor(const Quantity_Color& c)
{
    double r = 0, g = 0, b = 0;
    c.Values(r, g, b, Quantity_TOC_sRGB);
    return QColor::fromRgbF(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
}

} // namespace

// --- Body styles --------------------------------------------------------------------

void SketchEditor::matchStyles()
{
    m_styleOf = matchBodyStyles(m_bodies, m_bodyStyles);
    // Follow the bodies as they change: a fresh point inside each one.
    for (int b = 0; b < static_cast<int>(m_bodies.size()); ++b) {
        if (m_styleOf[b] < 0)
            continue;
        if (const auto inside = insidePoint(m_bodies[b]))
            m_bodyStyles[m_styleOf[b]].inside = *inside;
    }
}

BodyStyle& SketchEditor::styleFor(int body)
{
    if (m_styleOf.size() != m_bodies.size())
        m_styleOf.assign(m_bodies.size(), -1);
    if (m_styleOf[body] < 0) {
        BodyStyle style;
        style.inside = insidePoint(m_bodies[body]).value_or(gp_Pnt());
        m_bodyStyles.push_back(style);
        m_styleOf[body] = static_cast<int>(m_bodyStyles.size()) - 1;
    }
    return m_bodyStyles[m_styleOf[body]];
}

Quantity_Color SketchEditor::bodyColor(int body) const
{
    if (body >= 0 && body < static_cast<int>(m_styleOf.size()) && m_styleOf[body] >= 0) {
        const auto& color = m_bodyStyles[m_styleOf[body]].color;
        if (color)
            return Quantity_Color((*color)[0], (*color)[1], (*color)[2], Quantity_TOC_sRGB);
    }
    return m_colors.body;
}

std::vector<SketchEditor::BodyInfo> SketchEditor::bodyInfos() const
{
    std::vector<BodyInfo> out;
    for (int b = 0; b < bodyCount(); ++b) {
        BodyInfo info;
        const BodyStyle* style = b < static_cast<int>(m_styleOf.size()) && m_styleOf[b] >= 0 ? &m_bodyStyles[m_styleOf[b]] : nullptr;
        info.name = style && !style->name.empty() ? QString::fromStdString(style->name) : tr("Body %1").arg(b + 1);
        info.hidden = style && style->hidden;
        info.customColor = style && style->color;
        info.color = toQColor(bodyColor(b));
        out.push_back(info);
    }
    return out;
}

void SketchEditor::setBodyName(int body, const QString& name)
{
    if (body < 0 || body >= bodyCount())
        return;
    styleFor(body).name = name.trimmed().toStdString();
    ++m_changes;
    emit historyChanged();
    emit bodiesChanged();
}

void SketchEditor::setBodyHidden(int body, bool hidden)
{
    if (body < 0 || body >= bodyCount())
        return;
    styleFor(body).hidden = hidden;
    ++m_changes;
    refreshBodies();
    emit historyChanged();
    m_viewer->update();
}

void SketchEditor::setBodyColor(int body, std::optional<QColor> color)
{
    if (body < 0 || body >= bodyCount())
        return;
    BodyStyle& style = styleFor(body);
    if (color)
        style.color = std::array<double, 3>{color->redF(), color->greenF(), color->blueF()};
    else
        style.color.reset();
    ++m_changes;
    m_viewer->aisContext()->SetColor(m_bodyPrs[body], bodyColor(body), false);
    emit historyChanged();
    emit bodiesChanged();
    m_viewer->update();
}

void SketchEditor::unitsChanged()
{
    refreshOverlay();
    refreshMeasure();
    updateHint();
    emit historyChanged(); // the history shows values
    m_viewer->update();
}

// --- Tool changes -------------------------------------------------------------------

void SketchEditor::inspectToolChanged()
{
    endMove();
    if (!m_measured.empty() || !m_measurePrs.IsNull()) {
        m_measured.clear();
        refreshMeasure();
    }
}

// --- Measure ------------------------------------------------------------------------

void SketchEditor::measureTap(const Hit& hit)
{
    const bool item = hit.kind == Hit::Kind::Face || hit.kind == Hit::Kind::Edge || hit.kind == Hit::Kind::Vertex;
    if (!item) {
        m_measured.clear();
    } else {
        const auto it = std::find_if(m_measured.begin(), m_measured.end(), [&](const BodyPick& p) { return p.owner == hit.owner; });
        if (it != m_measured.end()) {
            m_measured.erase(it);
        } else {
            if (m_measured.size() >= 2)
                m_measured.clear(); // a third pick starts a new measurement
            m_measured.push_back({hit.owner, hit.shape, hit.id});
        }
    }
    refreshMeasure();
}

void SketchEditor::refreshMeasure()
{
    const auto& ctx = m_viewer->aisContext();
    ctx->ClearSelected(false);
    for (const BodyPick& p : m_measured)
        ctx->AddOrRemoveSelected(p.owner, false);
    if (!m_measurePrs.IsNull())
        ctx->Remove(m_measurePrs, false);
    m_measurePrs.Nullify();
    if (m_measured.size() == 2) {
        const Measurement m = measure(m_measured[0].shape, m_measured[1].shape);
        if (m.distance && *m.distance > 1e-9) {
            m_measurePrs = new AIS_Shape(BRepBuilderAPI_MakeEdge(m.from, m.to).Shape());
            m_measurePrs->SetColor(m_colors.accent);
            m_measurePrs->SetWidth(2.5);
            m_measurePrs->SetZLayer(Graphic3d_ZLayerId_Topmost);
            ctx->Display(m_measurePrs, AIS_WireFrame, -1, false);
        }
    }
    updateHint();
    m_viewer->update();
}

QStringList SketchEditor::measureLines() const
{
    QStringList lines;
    if (m_measured.empty())
        return lines;
    auto describe = [&](const Measurement& m) {
        if (m.position)
            lines << tr("Point: %1").arg(pointText(*m.position));
        if (m.length)
            lines << tr("Length: %1").arg(lengthText(*m.length));
        if (m.area)
            lines << tr("Area: %1").arg(areaText(*m.area));
        if (m.radius)
            lines << tr("Radius: %1  ·  diameter %2").arg(lengthText(*m.radius), lengthText(2.0 * *m.radius));
    };
    if (m_measured.size() == 1) {
        describe(measure(m_measured[0].shape));
    } else {
        const Measurement m = measure(m_measured[0].shape, m_measured[1].shape);
        if (m.distance)
            lines << tr("Distance: %1").arg(lengthText(*m.distance));
        if (m.angle)
            lines << tr("Angle: %1°").arg(number(*m.angle, 2));
        if (m.centers)
            lines << tr("Between centers: %1").arg(lengthText(*m.centers));
    }
    const int body = m_measured[0].body;
    if (body >= 0 && body < bodyCount()) {
        const MassProperties mass = massProperties(m_bodies[body]);
        lines << QString() << bodyInfos()[body].name;
        lines << tr("Volume: %1  ·  surface %2").arg(volumeText(mass.volume), areaText(mass.area));
        lines << tr("Center of mass: %1").arg(pointText(mass.center));
        lines << tr("Size: %1 × %2 × %3")
                     .arg(lengthText(mass.size.X(), false), lengthText(mass.size.Y(), false), lengthText(mass.size.Z()));
        lines << tr("Mass at 1.24 g/cm³ (PLA): %1 g").arg(number(mass.volume * 1.24e-3, 2));
    }
    return lines;
}

void SketchEditor::paintMeasure(QPainter& p) const
{
    const QStringList lines = measureLines();
    if (lines.isEmpty())
        return;
    QFont font = p.font();
    font.setPointSizeF(10.0);
    p.setFont(font);
    const QFontMetricsF metrics(font);
    double width = 0.0;
    for (const QString& line : lines)
        width = std::max(width, metrics.horizontalAdvance(line));
    const double lineHeight = metrics.height() + 2.0;
    const QRectF card(12, 12, width + 24, lines.size() * lineHeight + 16);
    p.setPen(QPen(m_colors.dimensionInk, 1.0));
    QColor fill = m_colors.badgeFill;
    fill.setAlpha(235);
    p.setBrush(fill);
    p.drawRoundedRect(card, 6, 6);
    p.setPen(m_colors.dimensionInk);
    for (int i = 0; i < lines.size(); ++i)
        p.drawText(QPointF(card.left() + 12, card.top() + 8 + metrics.ascent() + i * lineHeight), lines[i]);

    // The distance, beside the line it measures.
    if (m_measured.size() == 2) {
        const Measurement m = measure(m_measured[0].shape, m_measured[1].shape);
        if (m.distance && *m.distance > 1e-9) {
            const QPointF a = m_viewer->project(m.from), b = m_viewer->project(m.to);
            const QString text = lengthText(*m.distance);
            const QSizeF size = metrics.size(Qt::TextSingleLine, text) + QSizeF(10, 4);
            const QRectF badge((a + b) / 2.0 - QPointF(size.width() / 2.0, size.height() + 6), size);
            p.setPen(Qt::NoPen);
            p.setBrush(m_colors.overlayAccent);
            p.drawRoundedRect(badge, 4, 4);
            p.setPen(Qt::white);
            p.drawText(badge, Qt::AlignCenter, text);
        }
    }
}

// --- Section ------------------------------------------------------------------------

void SketchEditor::sectionTap(const Hit& hit)
{
    const auto ref = planeOf(hit);
    if (!ref) {
        emit message(tr("Section: tap a flat face or a plane"));
        return;
    }
    const auto plane = resolvePlane(m_bodies, *ref, m_context);
    if (!plane)
        return;
    m_sectionBase = *plane;
    m_sectionOffset = 0.0;
    m_sectionFlip = false;
    applySection();
    m_valueEditor->open(
        [] { return tr("Section offset [mm]"); }, 0.0, -1e5, 1e5, m_tapPos - QPointF(0, 60),
        [this](double value) {
            m_sectionOffset = value;
            applySection();
            updateHint();
        },
        [this] { updateHint(); }, // Esc keeps the cut where it is
        [this](double value) {
            m_sectionOffset = value;
            applySection();
        });
    m_valueEditor->setChoices([] { return QStringList{tr("Keep this side"), tr("Keep the other side")}; }, 0,
                              [this](int choice) {
                                  m_sectionFlip = choice == 1;
                                  applySection();
                              });
    updateHint();
}

void SketchEditor::applySection()
{
    const gp_Dir normal = m_sectionFlip ? m_sectionBase.Direction().Reversed() : m_sectionBase.Direction();
    const gp_Pnt at = m_sectionBase.Location().Translated(gp_Vec(m_sectionBase.Direction()) * m_sectionOffset);
    const bool fresh = m_section.IsNull();
    if (fresh) {
        m_section = new Graphic3d_ClipPlane();
        m_section->SetCapping(true);
        m_section->SetUseObjectMaterial(false);
    }
    // The material side gets the cut face in the trim color; the plane keeps what its
    // normal points away from.
    m_section->SetCappingColor(m_colors.trim);
    m_section->SetEquation(gp_Pln(at, normal.Reversed()));
    if (fresh) {
        m_viewer->view()->AddClipPlane(m_section);
        emit sectionChanged(true);
    }
    m_viewer->update();
}

void SketchEditor::clearSection()
{
    if (m_section.IsNull())
        return;
    if (m_toolKind == ToolKind::Section && m_valueEditor->isOpen())
        m_valueEditor->cancel();
    m_viewer->view()->RemoveClipPlane(m_section);
    m_section.Nullify();
    emit sectionChanged(false);
    m_viewer->update();
}

// --- Move ---------------------------------------------------------------------------

gp_Pnt SketchEditor::moveCenter() const
{
    Bnd_Box box;
    for (int b : m_move.bodies)
        BRepBndLib::Add(m_bodies[b], box);
    if (box.IsVoid())
        return gp::Origin();
    return gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0).Transformed(m_move.draft);
}

void SketchEditor::movePickBody(int body)
{
    const auto it = std::find(m_move.bodies.begin(), m_move.bodies.end(), body);
    if (it != m_move.bodies.end())
        m_move.bodies.erase(it);
    else
        m_move.bodies.push_back(body);
    m_move.anchor.reset();
    refreshMovePreview();
}

void SketchEditor::refreshMovePreview()
{
    const auto& ctx = m_viewer->aisContext();
    if (!m_move.gizmo.IsNull()) {
        m_move.gizmo->Detach();
        ctx->Remove(m_move.gizmo, false);
        m_move.gizmo.Nullify();
    }
    if (!m_move.preview.IsNull())
        ctx->Remove(m_move.preview, false);
    m_move.preview.Nullify();
    // The bodies being moved show only where they are going.
    for (int b = 0; b < bodyCount(); ++b) {
        const bool moving = std::find(m_move.bodies.begin(), m_move.bodies.end(), b) != m_move.bodies.end();
        const bool hidden = b < static_cast<int>(m_styleOf.size()) && m_styleOf[b] >= 0 && m_bodyStyles[m_styleOf[b]].hidden;
        if ((moving || hidden) && ctx->IsDisplayed(m_bodyPrs[b]))
            ctx->Erase(m_bodyPrs[b], false);
        else if (!moving && !hidden && !ctx->IsDisplayed(m_bodyPrs[b]))
            ctx->Display(m_bodyPrs[b], AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
    }
    updateSelectionModes();
    if (m_move.bodies.empty()) {
        updateHint();
        m_viewer->update();
        return;
    }
    TopoDS_Compound moving;
    BRep_Builder().MakeCompound(moving);
    for (int b : m_move.bodies)
        BRep_Builder().Add(moving, m_bodies[b]);
    m_move.preview = new AIS_Shape(moving);
    m_move.preview->SetColor(m_colors.accent);
    m_move.preview->SetLocalTransformation(m_move.draft);
    ctx->Display(m_move.preview, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
    ctx->Activate(m_move.preview, AIS_Shape::SelectionMode(TopAbs_EDGE));
    ctx->Activate(m_move.preview, AIS_Shape::SelectionMode(TopAbs_VERTEX));

    m_move.gizmo = new AIS_Manipulator();
    m_move.gizmo->SetPart(AIS_MM_Scaling, false);
    m_move.gizmo->SetPart(AIS_MM_TranslationPlane, false);
    m_move.gizmo->SetModeActivationOnDetection(true);
    m_move.gizmo->Attach(m_move.preview, AIS_Manipulator::OptionsForAttach().SetAdjustPosition(true).SetAdjustSize(false));
    // The same size on screen however far the view is zoomed, drawn over the bodies.
    m_move.gizmo->SetZoomPersistence(true);
    m_move.gizmo->SetSize(static_cast<float>(90.0 * m_touchScale * m_viewer->devicePixelRatioF()));
    m_move.gizmo->SetZLayer(Graphic3d_ZLayerId_Topmost);
    if (!ctx->IsDisplayed(m_move.gizmo))
        ctx->Display(m_move.gizmo, false);
    updateHint();
    m_viewer->update();
}

void SketchEditor::endMove()
{
    if (m_move.bodies.empty() && m_move.preview.IsNull())
        return;
    // No bodies: the refresh takes the preview and gizmo away and shows the bodies again.
    m_move.bodies.clear();
    refreshMovePreview();
    m_move = {};
}

bool SketchEditor::movePress(const PointerEvent& e)
{
    m_tapPos = e.pos;
    if (m_move.gizmo.IsNull())
        return false;
    const auto& ctx = m_viewer->aisContext();
    ctx->MoveTo(e.pixel.x(), e.pixel.y(), m_viewer->view(), false);
    if (!m_move.gizmo->HasActiveMode())
        return false;
    m_move.gizmo->StartTransform(e.pixel.x(), e.pixel.y(), m_viewer->view());
    m_move.dragging = true;
    return true;
}

bool SketchEditor::moveDrag(const PointerEvent& e)
{
    if (!m_move.dragging)
        return false;
    m_move.gizmo->Transform(e.pixel.x(), e.pixel.y(), m_viewer->view());
    m_move.draft = m_move.preview->LocalTransformation();
    m_viewer->update();
    return true;
}

bool SketchEditor::moveRelease(const PointerEvent& e)
{
    if (m_move.dragging) {
        m_move.gizmo->StopTransform(true);
        m_move.dragging = false;
        m_move.draft = m_move.preview->LocalTransformation();
        m_move.anchor.reset();
        updateHint();
        return true;
    }
    moveTap(e);
    return true;
}

void SketchEditor::moveTap(const PointerEvent& e)
{
    const auto& ctx = m_viewer->aisContext();
    ctx->MoveTo(e.pixel.x(), e.pixel.y(), m_viewer->view(), false);
    const Handle(StdSelect_ViewerSelector3d)& selector = ctx->MainSelector();
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const auto owner = Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(i));
        if (owner.IsNull())
            continue;
        if (!m_move.preview.IsNull() && owner->Selectable() == m_move.preview) {
            // Part of what is moving (oriented as in its body, so a face knows which way is
            // out): where it is now is what gets lined up.
            TopoDS_Shape part = owner->Shape();
            for (TopExp_Explorer f(m_move.preview->Shape(), part.ShapeType()); f.More(); f.Next()) {
                if (f.Current().IsSame(part))
                    part = f.Current();
            }
            m_move.anchor = BRepBuilderAPI_Transform(part, m_move.draft, true).Shape();
            updateHint();
            return;
        }
        const auto it = std::find(m_bodyPrs.begin(), m_bodyPrs.end(), owner->Selectable());
        if (it == m_bodyPrs.end())
            continue;
        const int body = static_cast<int>(it - m_bodyPrs.begin());
        if (!m_move.anchor) {
            movePickBody(body);
            return;
        }
        // The shape as it sits in the body (orientation gives a face's outward normal).
        TopoDS_Shape target;
        for (TopExp_Explorer f(m_bodies[body], owner->Shape().ShapeType()); f.More(); f.Next()) {
            if (f.Current().IsSame(owner->Shape()))
                target = f.Current();
        }
        const auto line = alignment(*m_move.anchor, target.IsNull() ? owner->Shape() : target);
        if (!line) {
            emit message(tr("Move: these don't line up: pick corners, round edges or flat faces"));
            return;
        }
        m_move.draft = *line * m_move.draft;
        m_move.anchor.reset();
        refreshMovePreview();
        return;
    }
}

void SketchEditor::askMoveValues()
{
    if (m_move.bodies.empty())
        return;
    struct Typed
    {
        double x = 0.0, y = 0.0, z = 0.0, turn = 0.0;
    };
    auto typed = std::make_shared<Typed>();
    const gp_Trsf base = m_move.draft;
    const gp_Pnt center = moveCenter();
    auto composed = [base, center, typed] {
        gp_Trsf shift, turn;
        shift.SetTranslation(gp_Vec(typed->x, typed->y, typed->z));
        turn.SetRotation(gp_Ax1(center, gp::DZ()), typed->turn * M_PI / 180.0);
        return shift * turn * base;
    };
    auto show = [this, composed] {
        if (m_move.preview.IsNull())
            return;
        m_move.preview->SetLocalTransformation(composed());
        if (!m_move.gizmo.IsNull())
            m_move.gizmo->Attach(m_move.preview, AIS_Manipulator::OptionsForAttach().SetAdjustPosition(true).SetAdjustSize(false));
        m_viewer->update();
    };
    auto restore = [this, base] {
        m_move.draft = base;
        if (!m_move.preview.IsNull())
            m_move.preview->SetLocalTransformation(base);
        updateHint();
        m_viewer->update();
    };
    auto apply = [this, composed] {
        Feature f;
        f.kind = Feature::Kind::Move;
        f.copy = m_move.copy;
        f.matrix = matrixOf(composed());
        for (int b : m_move.bodies) {
            if (const auto inside = insidePoint(m_bodies[b]))
                f.bodies.push_back(*inside);
        }
        // Styles go along: a moved body keeps its name and color.
        if (const auto t = trsfOf(f.matrix); t && !f.copy) {
            for (int b : m_move.bodies) {
                if (b < static_cast<int>(m_styleOf.size()) && m_styleOf[b] >= 0)
                    m_bodyStyles[m_styleOf[b]].inside.Transform(*t);
            }
        }
        endMove();
        commitFeature(f);
    };

    struct Step
    {
        std::function<QString()> label;
        double Typed::*field;
        double min, max;
    };
    const std::vector<Step> steps = {
        {[] { return tr("Move along X [mm]"); }, &Typed::x, -1e5, 1e5},
        {[] { return tr("Move along Y [mm]"); }, &Typed::y, -1e5, 1e5},
        {[] { return tr("Move along Z [mm]"); }, &Typed::z, -1e5, 1e5},
        {[] { return tr("Turn about Z [°]"); }, &Typed::turn, -360.0, 360.0},
    };
    auto ask = std::make_shared<std::function<void(std::size_t)>>();
    *ask = [=, this](std::size_t i) {
        if (i >= steps.size()) {
            apply();
            return;
        }
        const Step step = steps[i];
        m_valueEditor->open(
            step.label, 0.0, step.min, step.max, m_tapPos - QPointF(0, 60),
            [=](double v) {
                typed.get()->*step.field = v;
                show();
                (*ask)(i + 1);
            },
            restore,
            [=](double v) {
                typed.get()->*step.field = v;
                show();
            });
        if (i == 0) {
            m_valueEditor->setChoices([] { return QStringList{tr("Move"), tr("Copy")}; }, m_move.copy ? 1 : 0,
                                      [this](int choice) { m_move.copy = choice == 1; });
        }
        updateHint();
    };
    (*ask)(0);
}
