// The assembly mode (M17): components of parts placed and joined, dragged as their joints
// allow, checked for overlaps, exploded.

#include "sketch/SketchEditor.h"

#include "io/ComponentFiles.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <Prs3d_Drawer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <V3d_View.hxx>

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

TopoDS_Compound newCompound()
{
    TopoDS_Compound c;
    BRep_Builder().MakeCompound(c);
    return c;
}

double diagonal(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    return box.IsVoid() ? 10.0 : std::sqrt(box.SquareExtent());
}

gp_Trsf translation(const gp_Vec& v)
{
    gp_Trsf t;
    t.SetTranslation(v);
    return t;
}

// Whether a picked face or edge is round: two round picks make a revolute joint.
bool isRound(const TopoDS_Shape& shape)
{
    if (shape.ShapeType() == TopAbs_EDGE)
        return BRepAdaptor_Curve(TopoDS::Edge(shape)).GetType() == GeomAbs_Circle;
    if (shape.ShapeType() == TopAbs_FACE) {
        const GeomAbs_SurfaceType type = BRepAdaptor_Surface(TopoDS::Face(shape)).GetType();
        return type == GeomAbs_Cylinder || type == GeomAbs_Cone;
    }
    return false;
}

QColor toQColor(const Quantity_Color& c)
{
    double r = 0, g = 0, b = 0;
    c.Values(r, g, b, Quantity_TOC_sRGB);
    return QColor::fromRgbF(static_cast<float>(r), static_cast<float>(g), static_cast<float>(b));
}

constexpr JointKind kJointKindOrder[] = {JointKind::Rigid,       JointKind::Revolute, JointKind::Slider,
                                         JointKind::Cylindrical, JointKind::Ball,     JointKind::Planar};

} // namespace

// --- Names ------------------------------------------------------------------------------

QString SketchEditor::jointKindName(JointKind kind)
{
    switch (kind) {
    case JointKind::Rigid: return tr("Rigid");
    case JointKind::Revolute: return tr("Revolute");
    case JointKind::Slider: return tr("Slider");
    case JointKind::Cylindrical: return tr("Cylindrical");
    case JointKind::Ball: return tr("Ball");
    case JointKind::Planar: return tr("Planar");
    }
    return {};
}

QString SketchEditor::jointValueLabel(JointKind kind, int value)
{
    switch (kind) {
    case JointKind::Revolute: return tr("Angle [°]");
    case JointKind::Slider: return tr("Distance [mm]");
    case JointKind::Cylindrical: return value == 0 ? tr("Angle [°]") : tr("Distance [mm]");
    case JointKind::Ball: return value == 0 ? tr("Turn about X [°]") : value == 1 ? tr("Turn about Y [°]") : tr("Turn about Z [°]");
    case JointKind::Planar: return value == 0 ? tr("X [mm]") : value == 1 ? tr("Y [mm]") : tr("Angle [°]");
    case JointKind::Rigid: break;
    }
    return {};
}

QString SketchEditor::sourceName(int sourceId) const
{
    const ComponentSource* source = m_assembly.source(sourceId);
    if (!source)
        return tr("Part");
    if (!source->name.empty())
        return QString::fromStdString(source->name);
    if (source->kind == ComponentSource::Kind::File)
        return QFileInfo(QString::fromStdString(source->path)).completeBaseName();
    const auto bodies = bodySources(m_assembly, m_bodies);
    if (const auto it = bodies.find(sourceId); it != bodies.end()) {
        const std::vector<BodyInfo> infos = bodyInfos();
        if (it->second < static_cast<int>(infos.size()))
            return infos[it->second].name;
    }
    return tr("Missing body");
}

QString SketchEditor::componentName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_assembly.components.size()))
        return {};
    const int source = m_assembly.components[index].source;
    int number = 0;
    for (int i = 0; i <= index; ++i)
        number += m_assembly.components[i].source == source ? 1 : 0;
    return QStringLiteral("%1:%2").arg(sourceName(source)).arg(number);
}

std::vector<SketchEditor::ComponentInfo> SketchEditor::componentInfos() const
{
    std::vector<ComponentInfo> out;
    for (int i = 0; i < static_cast<int>(m_assembly.components.size()); ++i) {
        const Component& c = m_assembly.components[i];
        ComponentInfo info;
        info.name = componentName(i);
        const ComponentSource* source = m_assembly.source(c.source);
        info.source = source && source->kind == ComponentSource::Kind::File
                        ? QString::fromStdString(source->path)
                        : tr("Body: %1").arg(sourceName(c.source));
        info.grounded = c.grounded;
        info.hidden = c.hidden;
        info.missing = !m_sourceShapes.count(c.source);
        if (const auto it = m_sourceProblems.find(c.source); it != m_sourceProblems.end())
            info.problem = it->second;
        out.push_back(info);
    }
    return out;
}

std::vector<SketchEditor::JointInfo> SketchEditor::jointInfos() const
{
    std::vector<JointInfo> out;
    std::map<JointKind, int> numbers;
    for (std::size_t j = 0; j < m_assembly.joints.size(); ++j) {
        const Joint& joint = m_assembly.joints[j];
        JointInfo info;
        info.kind = joint.kind;
        info.name = QStringLiteral("%1 %2").arg(jointKindName(joint.kind)).arg(++numbers[joint.kind]);
        info.between = QStringLiteral("%1 – %2")
                           .arg(componentName(m_assembly.componentIndex(joint.a.component)),
                                componentName(m_assembly.componentIndex(joint.b.component)));
        info.values = joint.values;
        info.min = joint.min;
        info.max = joint.max;
        info.limited = joint.limited;
        info.flip = joint.flip;
        if (j < m_solution.jointErrors.size())
            info.error = modelText(m_solution.jointErrors[j]);
        out.push_back(info);
    }
    return out;
}

QStringList SketchEditor::bodyNames() const
{
    QStringList out;
    for (const BodyInfo& info : bodyInfos())
        out << info.name;
    return out;
}

// --- Entering and leaving -----------------------------------------------------------------

void SketchEditor::enterAssembly()
{
    if (mode() == EditorMode::Assembly)
        return;
    if (mode() == EditorMode::Sketch)
        closeSketch();
    setTool(ToolKind::Select); // ends moves, measures, value sessions
    endValueSession(true);
    clearModelSelection();
    settleModel();
    m_assemblyMode = true;
    if (!m_fileWatcher) {
        m_fileWatcher = new QFileSystemWatcher(this);
        connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            m_fileShapes.erase(path); // read again
            if (mode() == EditorMode::Assembly) {
                refreshAssembly();
                emit assemblyChanged();
            }
        });
    }
    hidePartScene();
    refreshAssembly();
    emit modeChanged(EditorMode::Assembly);
    updateHint();
    m_viewer->update();
}

void SketchEditor::leaveAssembly()
{
    if (mode() != EditorMode::Assembly)
        return;
    setTool(ToolKind::Select);
    m_valueEditor->cancel();
    m_jointFirst.reset();
    m_componentDrag.reset();
    const auto& ctx = m_viewer->aisContext();
    for (Handle(AIS_Shape)& prs : m_componentPrs) {
        if (!prs.IsNull())
            ctx->Remove(prs, false);
    }
    m_componentPrs.clear();
    for (Handle(AIS_Shape)* prs : {&m_interferencePrs, &m_jointMarkPrs}) {
        if (!prs->IsNull())
            ctx->Remove(*prs, false);
        prs->Nullify();
    }
    m_pickedComponent = -1;
    m_assemblyMode = false;
    showPartScene();
    emit modeChanged(EditorMode::Model);
    updateHint();
    m_viewer->update();
}

void SketchEditor::hidePartScene()
{
    // The functions that show the part model show nothing in the assembly mode: calling
    // them takes their presentations away.
    const auto& ctx = m_viewer->aisContext();
    for (const Handle(AIS_Shape)& prs : m_bodyPrs)
        ctx->Erase(prs, false);
    for (const Slot& slot : m_slots)
        ctx->Erase(slot.prs, false);
    showRegions();
    showHoverRegion();
    showDatums();
    showReferences();
    showThreads();
    refreshSolidMarks();
}

void SketchEditor::showPartScene()
{
    const auto& ctx = m_viewer->aisContext();
    for (const Slot& slot : m_slots)
        ctx->Display(slot.prs, 0, -1, false);
    refreshBodies(); // displays them again, as they are now (and the threads)
    showRegions();
    showDatums();
    showReferences();
    refreshSketches();
}

// --- Sources, solving and showing ---------------------------------------------------------

void SketchEditor::updateSourceShapes()
{
    m_sourceShapes.clear();
    m_sourceProblems.clear();
    const std::map<int, int> bodies = bodySources(m_assembly, m_bodies);
    for (ComponentSource& source : m_assembly.sources) {
        if (source.kind == ComponentSource::Kind::Body) {
            const auto it = bodies.find(source.id);
            if (it == bodies.end())
                continue;
            const TopoDS_Shape& body = m_bodies[it->second];
            m_sourceShapes[source.id] = body;
            // Kept inside the body as it changes, as body styles are.
            if (const auto inside = insidePoint(body))
                source.inside = *inside;
            continue;
        }
        const QString path = QString::fromStdString(source.path);
        const QFileInfo info(path);
        const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
        FileShape& known = m_fileShapes[path];
        if (known.shape.IsNull() || known.modified != modified) {
            known = {modified, {}, {}};
            QString error;
            if (modified >= 0) {
                if (const auto shape = projectShape(path, &error)) {
                    known.shape = *shape;
                    // The copy kept in this project, for when the file is gone.
                    source.cache = std::make_shared<const std::string>(shapeToText(*shape));
                }
            } else {
                error = tr("The file is gone");
            }
            known.error = error;
            if (known.shape.IsNull() && source.cache) {
                if (const auto kept = shapeFromText(*source.cache))
                    known.shape = *kept;
            }
        }
        if (m_fileWatcher && modified >= 0 && !m_fileWatcher->files().contains(path))
            m_fileWatcher->addPath(path);
        if (!known.error.isEmpty())
            m_sourceProblems[source.id] = known.error;
        if (!known.shape.IsNull())
            m_sourceShapes[source.id] = known.shape;
    }
}

void SketchEditor::refreshAssembly()
{
    if (mode() != EditorMode::Assembly)
        return;
    updateSourceShapes();
    m_solution = solveAssembly(m_assembly);
    if (m_pickedComponent >= static_cast<int>(m_assembly.components.size()))
        m_pickedComponent = -1;
    showComponents();
    showJointMark();
    updateHint();
}

void SketchEditor::showComponents()
{
    const auto& ctx = m_viewer->aisContext();
    const int count = static_cast<int>(m_assembly.components.size());
    while (static_cast<int>(m_componentPrs.size()) > count) {
        if (!m_componentPrs.back().IsNull())
            ctx->Remove(m_componentPrs.back(), false);
        m_componentPrs.pop_back();
    }
    m_componentPrs.resize(count);
    const std::map<int, int> bodies = bodySources(m_assembly, m_bodies);
    const bool joining = m_toolKind == ToolKind::Joint;
    for (int i = 0; i < count; ++i) {
        const Component& c = m_assembly.components[i];
        Handle(AIS_Shape)& prs = m_componentPrs[i];
        const auto shape = m_sourceShapes.find(c.source);
        if (c.hidden || shape == m_sourceShapes.end()) {
            if (!prs.IsNull())
                ctx->Remove(prs, false);
            prs.Nullify();
            continue;
        }
        if (prs.IsNull() || !prs->Shape().IsEqual(shape->second)) {
            if (!prs.IsNull())
                ctx->Remove(prs, false);
            prs = new AIS_Shape(shape->second);
            Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
            material.SetSpecularColor(Quantity_Color(0.06, 0.06, 0.06, Quantity_TOC_RGB));
            prs->SetMaterial(material);
            ctx->Display(prs, AIS_Shaded, 0, false);
        }
        // Where it sits; apart from the others in the exploded view.
        gp_Trsf where = i < static_cast<int>(m_solution.placements.size()) ? m_solution.placements[i] : gp_Trsf();
        if (m_assembly.exploded) {
            if (const auto e = m_assembly.explode.find(c.id); e != m_assembly.explode.end())
                where = translation(e->second) * where;
        }
        ctx->SetLocation(prs, TopLoc_Location(where));
        const auto body = bodies.find(c.source);
        const Quantity_Color color = i == m_pickedComponent ? m_colors.accent
                                   : body != bodies.end()    ? bodyColor(body->second)
                                                             : m_colors.body;
        ctx->SetColor(prs, color, false);
        // Joint picks faces, round edges and corners; otherwise a part is picked whole.
        auto set = [&](int selectionMode, bool on) {
            const bool is = ctx->SelectionManager()->IsActivated(prs, selectionMode);
            if (on && !is)
                ctx->Activate(prs, selectionMode, true);
            else if (!on && is)
                ctx->Deactivate(prs, selectionMode);
        };
        set(0, !joining);
        set(AIS_Shape::SelectionMode(TopAbs_FACE), joining);
        set(AIS_Shape::SelectionMode(TopAbs_EDGE), joining);
        set(AIS_Shape::SelectionMode(TopAbs_VERTEX), joining);
    }
    m_viewer->update();
}

void SketchEditor::showJointMark()
{
    const auto& ctx = m_viewer->aisContext();
    if (!m_jointMarkPrs.IsNull())
        ctx->Remove(m_jointMarkPrs, false);
    m_jointMarkPrs.Nullify();
    if (!m_jointFirst || m_jointFirst->component >= static_cast<int>(m_solution.placements.size()))
        return;
    // The first pick's frame where it is: a dot and its axis.
    const gp_Ax3 frame = m_jointFirst->frame.Transformed(m_solution.placements[m_jointFirst->component]);
    const double size = std::max(2.0, 0.08 * diagonal(m_sourceShapes.count(m_assembly.components[m_jointFirst->component].source)
                                                         ? m_sourceShapes.at(m_assembly.components[m_jointFirst->component].source)
                                                         : TopoDS_Shape()));
    TopoDS_Compound mark = newCompound();
    BRep_Builder().Add(mark, BRepPrimAPI_MakeSphere(frame.Location(), 0.15 * size).Shape());
    BRep_Builder().Add(mark, BRepBuilderAPI_MakeEdge(frame.Location(),
                                                     frame.Location().Translated(gp_Vec(frame.Direction()) * size))
                                 .Edge());
    m_jointMarkPrs = new AIS_Shape(mark);
    m_jointMarkPrs->SetColor(m_colors.accent);
    m_jointMarkPrs->SetWidth(3.0);
    m_jointMarkPrs->SetZLayer(Graphic3d_ZLayerId_Topmost);
    ctx->Display(m_jointMarkPrs, AIS_Shaded, -1, false);
    m_viewer->update();
}

// --- Picking and dragging ----------------------------------------------------------------

std::pair<int, TopoDS_Shape> SketchEditor::componentAt(const PointerEvent& e) const
{
    const auto& ctx = m_viewer->aisContext();
    ctx->MoveTo(e.pixel.x(), e.pixel.y(), m_viewer->view(), false);
    if (!ctx->HasDetected())
        return {-1, {}};
    const Handle(StdSelect_ViewerSelector3d)& selector = ctx->MainSelector();
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(SelectMgr_EntityOwner) owner = selector->Picked(i);
        if (owner.IsNull())
            continue;
        const auto it = std::find(m_componentPrs.begin(), m_componentPrs.end(), owner->Selectable());
        if (it == m_componentPrs.end() || it->IsNull())
            continue;
        const int index = static_cast<int>(it - m_componentPrs.begin());
        const TopoDS_Shape& whole = (*it)->Shape();
        const auto brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        if (brep.IsNull())
            return {index, whole};
        // As it sits in the part (its own coordinates), so a face's orientation gives its
        // outward normal.
        const TopAbs_ShapeEnum wanted = brep->Shape().ShapeType();
        if (wanted == TopAbs_FACE || wanted == TopAbs_EDGE || wanted == TopAbs_VERTEX) {
            for (TopExp_Explorer f(whole, wanted); f.More(); f.Next()) {
                if (f.Current().IsSame(brep->Shape()))
                    return {index, f.Current()};
            }
        }
        return {index, whole};
    }
    return {-1, {}};
}

void SketchEditor::pickComponent(int index)
{
    m_pickedComponent = index >= 0 && index < static_cast<int>(m_assembly.components.size()) ? index : -1;
    showComponents();
    emit assemblyChanged();
    emit selectionChanged();
    updateHint();
}

bool SketchEditor::assemblyPress(const PointerEvent& e)
{
    m_componentDrag.reset();
    if (m_toolKind != ToolKind::Select)
        return false; // the tap is taken on release
    const auto [index, shape] = componentAt(e);
    if (index < 0)
        return false;
    ComponentDrag drag;
    drag.component = index;
    drag.pressPos = e.pos;
    drag.startPlacement = m_solution.placements[index];
    if (!m_assembly.exploded && index < static_cast<int>(m_solution.placedBy.size())) {
        drag.joint = m_solution.placedBy[index];
        if (drag.joint >= 0)
            drag.start = m_assembly.joints[drag.joint].values;
    }
    // The point of the part under the pointer, in its own coordinates: it follows the pointer.
    const Handle(StdSelect_ViewerSelector3d)& selector = m_viewer->aisContext()->MainSelector();
    gp_Pnt grabbed = selector->NbPicked() > 0 ? selector->PickedPoint(1) : gp::Origin();
    drag.axis = gp_Ax3(grabbed.Transformed(drag.startPlacement.Inverted()), gp::DZ());
    m_componentDrag = drag;
    return true;
}

bool SketchEditor::assemblyMove(const PointerEvent& e, bool pressed)
{
    if (!pressed) {
        // Hovering lights up what a tap would pick.
        m_viewer->aisContext()->MoveTo(e.pixel.x(), e.pixel.y(), m_viewer->view(), false);
        return true;
    }
    if (!m_componentDrag)
        return false;
    if (!m_componentDrag->moved && QLineF(e.pos, m_componentDrag->pressPos).length() < 4.0)
        return true;
    if (!m_componentDrag->moved) {
        m_componentDrag->moved = true;
        m_assemblyBefore = m_assembly;
    }
    dragComponentTo(e.pos);
    return true;
}

bool SketchEditor::assemblyRelease(const PointerEvent& e)
{
    if (m_componentDrag && m_componentDrag->moved) {
        m_componentDrag.reset();
        if (m_assemblyBefore) {
            const Assembly before = *m_assemblyBefore;
            m_assemblyBefore.reset();
            commitAssembly(before);
        }
        return true;
    }
    m_componentDrag.reset();
    const auto [index, shape] = componentAt(e);
    if (m_toolKind == ToolKind::Joint) {
        jointTap(index, shape);
        return true;
    }
    pickComponent(index == m_pickedComponent ? -1 : index);
    return true;
}

void SketchEditor::dragComponentTo(const QPointF& pos)
{
    ComponentDrag& drag = *m_componentDrag;
    Component& component = m_assembly.components[drag.component];
    const gp_Pnt grabbed = drag.axis.Location(); // in the part's coordinates
    const QPointF delta = pos - drag.pressPos;

    if (drag.joint < 0 || m_assembly.exploded) {
        if (component.grounded && !m_assembly.exploded) {
            emit message(tr("%1 is grounded: it stays where it is").arg(componentName(drag.component)));
            m_componentDrag.reset();
            m_assemblyBefore.reset();
            return;
        }
        // Free (or exploded): along the screen, at the depth of the point grabbed.
        const Handle(Graphic3d_Camera)& camera = m_viewer->view()->Camera();
        const gp_Dir up = camera->Up();
        const gp_Dir right = camera->Direction().Crossed(up);
        const double scale = pixelsToModel(1.0);
        const gp_Vec move = gp_Vec(right) * (delta.x() * scale) - gp_Vec(up) * (delta.y() * scale);
        if (m_assembly.exploded) {
            const gp_Vec before =
                m_assemblyBefore && m_assemblyBefore->explode.count(component.id) ? m_assemblyBefore->explode.at(component.id) : gp_Vec();
            m_assembly.explode[component.id] = before + move;
        } else {
            component.matrix = matrixOf(translation(move) * drag.startPlacement);
        }
    } else {
        // Along the joint: the values that bring the grabbed point under the pointer (least
        // squares, so a turn, a slide, or both, as the joint allows).
        Joint& joint = m_assembly.joints[drag.joint];
        const int n = degreesOfFreedom(joint.kind);
        if (n == 0) {
            emit message(tr("%1 is joined rigidly: drag the part it is joined to").arg(componentName(drag.component)));
            m_componentDrag.reset();
            m_assemblyBefore.reset();
            return;
        }
        auto screenAt = [&](const std::array<double, 3>& values) {
            Joint& j = m_assembly.joints[drag.joint];
            const std::array<double, 3> kept = j.values;
            j.values = values;
            const AssemblySolution s = solveAssembly(m_assembly);
            j.values = kept;
            return m_viewer->project(grabbed.Transformed(s.placements[drag.component]));
        };
        std::array<double, 3> values = joint.values;
        for (int iteration = 0; iteration < 6; ++iteration) {
            const QPointF at = screenAt(values);
            const QPointF error = pos - at;
            if (std::hypot(error.x(), error.y()) < 0.5)
                break;
            // How the grabbed point moves on screen per unit of each value.
            double J[2][3] = {};
            for (int k = 0; k < n; ++k) {
                const double step = isAngle(joint.kind, k) ? 0.5 : 0.1;
                std::array<double, 3> nudged = values;
                nudged[k] += step;
                const QPointF d = (screenAt(nudged) - at) / step;
                J[0][k] = d.x();
                J[1][k] = d.y();
            }
            // (JᵀJ + λI) dv = Jᵀ e, solved by Gaussian elimination (at most 3 × 3).
            double A[3][4] = {};
            double trace = 0.0;
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c)
                    A[r][c] = J[0][r] * J[0][c] + J[1][r] * J[1][c];
                A[r][n] = J[0][r] * error.x() + J[1][r] * error.y();
                trace += A[r][r];
            }
            if (trace < 1e-12)
                break; // the point doesn't move on screen (looking along the motion)
            for (int r = 0; r < n; ++r)
                A[r][r] += 1e-3 * trace / n;
            for (int c = 0; c < n; ++c) {
                int pivot = c;
                for (int r = c + 1; r < n; ++r) {
                    if (std::abs(A[r][c]) > std::abs(A[pivot][c]))
                        pivot = r;
                }
                std::swap(A[c], A[pivot]);
                if (std::abs(A[c][c]) < 1e-15)
                    break;
                for (int r = 0; r < n; ++r) {
                    if (r == c)
                        continue;
                    const double f = A[r][c] / A[c][c];
                    for (int k = c; k <= n; ++k)
                        A[r][k] -= f * A[c][k];
                }
            }
            for (int k = 0; k < n; ++k) {
                if (std::abs(A[k][k]) > 1e-15)
                    values[k] += A[k][n] / A[k][k];
            }
            Joint limited = joint;
            limited.values = values;
            values = clampedValues(limited);
        }
        joint.values = values;
    }
    m_solution = solveAssembly(m_assembly);
    showComponents();
    emit assemblyChanged();
}

// --- Joints ---------------------------------------------------------------------------------

void SketchEditor::jointTap(int component, const TopoDS_Shape& picked)
{
    if (component < 0)
        return;
    const auto frame = jointFrame(picked);
    if (!frame) {
        emit message(tr("Pick a flat or round face, a round or straight edge, or a corner"));
        return;
    }
    if (!m_jointFirst || m_jointFirst->component == component) {
        m_jointFirst = JointPick{component, *frame, isRound(picked)};
        showJointMark();
        updateHint();
        return;
    }
    Joint joint;
    for (const Joint& j : m_assembly.joints)
        joint.id = std::max(joint.id, j.id);
    ++joint.id;
    // Two round picks (a pin in a hole): it turns; anything else holds still.
    joint.kind = m_jointFirst->round && isRound(picked) ? JointKind::Revolute : JointKind::Rigid;
    joint.a = {m_assembly.components[m_jointFirst->component].id, m_jointFirst->frame};
    joint.b = {m_assembly.components[component].id, *frame};
    m_jointFirst.reset();
    m_assemblyBefore = m_assembly;
    m_assembly.joints.push_back(joint);
    refreshAssembly();
    askJoint(static_cast<int>(m_assembly.joints.size()) - 1);
}

void SketchEditor::askJoint(int index)
{
    // Its kind and which way the frames face under the field, its first value in it; the
    // model follows; Enter keeps it, Esc drops it.
    auto label = [this, index] {
        if (index >= static_cast<int>(m_assembly.joints.size()))
            return QString();
        const Joint& j = m_assembly.joints[index];
        return degreesOfFreedom(j.kind) > 0 ? jointValueLabel(j.kind, 0) : tr("Rigid: Enter keeps it");
    };
    auto limits = [this, index] {
        const Joint& j = m_assembly.joints[index];
        return degreesOfFreedom(j.kind) > 0 ? std::pair(-1e5, 1e5) : std::pair(0.0, 0.0);
    };
    auto preview = [this, index](double value) {
        if (index >= static_cast<int>(m_assembly.joints.size()))
            return;
        Joint& j = m_assembly.joints[index];
        if (degreesOfFreedom(j.kind) > 0)
            j.values[0] = value;
        m_solution = solveAssembly(m_assembly);
        showComponents();
    };
    auto accept = [this, preview](double value) {
        preview(value);
        if (m_assemblyBefore) {
            const Assembly before = *m_assemblyBefore;
            m_assemblyBefore.reset();
            commitAssembly(before);
        }
        const auto errors = m_solution.jointErrors;
        if (!errors.empty() && !errors.back().empty())
            emit message(modelText(errors.back()));
    };
    auto cancel = [this] {
        if (m_assemblyBefore) {
            m_assembly = *m_assemblyBefore;
            m_assemblyBefore.reset();
            refreshAssembly();
            emit assemblyChanged();
        }
    };
    const QPointF anchor(m_viewer->width() / 2.0, m_viewer->height() - 60.0);
    m_valueEditor->open(label, 0.0, limits().first, limits().second, anchor, accept, cancel, preview);
    m_valueEditor->setChoices(
        [] {
            QStringList kinds;
            for (JointKind k : kJointKindOrder)
                kinds << jointKindName(k);
            return kinds;
        },
        static_cast<int>(std::find(std::begin(kJointKindOrder), std::end(kJointKindOrder), m_assembly.joints[index].kind)
                         - std::begin(kJointKindOrder)),
        [this, index, label, limits, accept, cancel, preview](int choice) {
            Joint& j = m_assembly.joints[index];
            j.kind = kJointKindOrder[choice];
            j.values = {};
            m_valueEditor->retarget(label, 0.0, limits().first, limits().second, accept, cancel, preview);
            preview(0.0);
            updateHint();
        });
    m_valueEditor->setMoreChoices([] { return QStringList{tr("Facing"), tr("Same way")}; },
                                  m_assembly.joints[index].flip ? 1 : 0, [this, index, preview](int choice) {
                                      m_assembly.joints[index].flip = choice == 1;
                                      preview(m_assembly.joints[index].values[0]);
                                  });
    emit assemblyChanged();
    updateHint();
}

// --- Changes --------------------------------------------------------------------------------

void SketchEditor::commitAssembly(const Assembly& before)
{
    Assembly now = std::move(m_assembly);
    m_assembly = before;
    pushUndo();
    m_assembly = std::move(now);
    refreshAssembly();
    emit assemblyChanged();
    emit historyChanged();
}

void SketchEditor::setAssembly(const Assembly& assembly)
{
    const Assembly before = m_assembly;
    m_assembly = assembly;
    commitAssembly(before);
}

void SketchEditor::insertBodyComponent(int body)
{
    if (body < 0 || body >= bodyCount())
        return;
    const Assembly before = m_assembly;
    // The body's source, or a new one.
    const std::map<int, int> sources = bodySources(m_assembly, m_bodies);
    int sourceId = 0;
    for (const auto& [id, b] : sources) {
        if (b == body)
            sourceId = id;
    }
    if (sourceId == 0) {
        ComponentSource source;
        for (const ComponentSource& s : m_assembly.sources)
            source.id = std::max(source.id, s.id);
        source.id += 1;
        source.inside = insidePoint(m_bodies[body]).value_or(gp::Origin());
        m_assembly.sources.push_back(source);
        sourceId = source.id;
    }
    Component c;
    for (const Component& k : m_assembly.components)
        c.id = std::max(c.id, k.id);
    c.id += 1;
    c.source = sourceId;
    c.grounded = m_assembly.components.empty(); // the first part holds still
    // Another copy goes beside the ones there are.
    int copies = 0;
    for (const Component& k : m_assembly.components)
        copies += k.source == sourceId ? 1 : 0;
    if (copies > 0) {
        Bnd_Box box;
        BRepBndLib::Add(m_bodies[body], box);
        const double width = box.IsVoid() ? 10.0 : box.CornerMax().X() - box.CornerMin().X();
        c.matrix = matrixOf(translation(gp_Vec(1.3 * width * copies + 5.0 * copies, 0, 0)));
    }
    m_assembly.components.push_back(c);
    commitAssembly(before);
    pickComponent(static_cast<int>(m_assembly.components.size()) - 1);
}

bool SketchEditor::insertFileComponent(const QString& path)
{
    QString error;
    const auto shape = projectShape(path, &error);
    if (!shape) {
        emit message(tr("Can't use %1: %2").arg(QFileInfo(path).fileName(), error));
        return false;
    }
    const Assembly before = m_assembly;
    const std::string absolute = QFileInfo(path).absoluteFilePath().toStdString();
    int sourceId = 0;
    for (const ComponentSource& s : m_assembly.sources) {
        if (s.kind == ComponentSource::Kind::File && s.path == absolute)
            sourceId = s.id;
    }
    if (sourceId == 0) {
        ComponentSource source;
        source.kind = ComponentSource::Kind::File;
        for (const ComponentSource& s : m_assembly.sources)
            source.id = std::max(source.id, s.id);
        source.id += 1;
        source.path = absolute;
        source.cache = std::make_shared<const std::string>(shapeToText(*shape));
        m_assembly.sources.push_back(source);
        sourceId = source.id;
    }
    Component c;
    for (const Component& k : m_assembly.components)
        c.id = std::max(c.id, k.id);
    c.id += 1;
    c.source = sourceId;
    c.grounded = m_assembly.components.empty();
    // Beside what is there.
    Bnd_Box there;
    for (const auto& placed : placedComponents(false))
        BRepBndLib::Add(placed.shape, there);
    if (!there.IsVoid()) {
        Bnd_Box own;
        BRepBndLib::Add(*shape, own);
        const double x = there.CornerMax().X() + 5.0 - (own.IsVoid() ? 0.0 : own.CornerMin().X());
        c.matrix = matrixOf(translation(gp_Vec(x, 0, 0)));
    }
    m_assembly.components.push_back(c);
    commitAssembly(before);
    pickComponent(static_cast<int>(m_assembly.components.size()) - 1);
    return true;
}

void SketchEditor::setComponentGrounded(int index, bool grounded)
{
    if (index < 0 || index >= static_cast<int>(m_assembly.components.size()))
        return;
    const Assembly before = m_assembly;
    Component& c = m_assembly.components[index];
    // Grounded where it is now (a joint may have put it there).
    if (grounded && index < static_cast<int>(m_solution.placements.size()))
        c.matrix = matrixOf(m_solution.placements[index]);
    c.grounded = grounded;
    commitAssembly(before);
}

void SketchEditor::setComponentHidden(int index, bool hidden)
{
    if (index < 0 || index >= static_cast<int>(m_assembly.components.size()))
        return;
    const Assembly before = m_assembly;
    m_assembly.components[index].hidden = hidden;
    commitAssembly(before);
}

void SketchEditor::removeComponent(int index)
{
    if (index < 0 || index >= static_cast<int>(m_assembly.components.size()))
        return;
    const Assembly before = m_assembly;
    const Component gone = m_assembly.components[index];
    m_assembly.components.erase(m_assembly.components.begin() + index);
    std::erase_if(m_assembly.joints, [&](const Joint& j) { return j.a.component == gone.id || j.b.component == gone.id; });
    m_assembly.explode.erase(gone.id);
    // A source no part uses any more goes too.
    if (std::none_of(m_assembly.components.begin(), m_assembly.components.end(),
                     [&](const Component& c) { return c.source == gone.source; }))
        std::erase_if(m_assembly.sources, [&](const ComponentSource& s) { return s.id == gone.source; });
    m_pickedComponent = -1;
    commitAssembly(before);
}

void SketchEditor::setJointValue(int joint, int value, double v, bool final)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()) || value < 0 || value > 2)
        return;
    if (!m_assemblyBefore)
        m_assemblyBefore = m_assembly;
    Joint& j = m_assembly.joints[joint];
    j.values[value] = v;
    j.values = clampedValues(j);
    if (final) {
        const Assembly before = *m_assemblyBefore;
        m_assemblyBefore.reset();
        commitAssembly(before);
        return;
    }
    m_solution = solveAssembly(m_assembly);
    showComponents();
    emit assemblyChanged();
}

void SketchEditor::setJointLimits(int joint, int value, bool limited, double min, double max)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()) || value < 0 || value > 2)
        return;
    const Assembly before = m_assembly;
    Joint& j = m_assembly.joints[joint];
    j.limited[value] = limited;
    j.min[value] = std::min(min, max);
    j.max[value] = std::max(min, max);
    j.values = clampedValues(j);
    commitAssembly(before);
}

void SketchEditor::setJointKind(int joint, JointKind kind)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()))
        return;
    const Assembly before = m_assembly;
    Joint& j = m_assembly.joints[joint];
    j.kind = kind;
    j.values = {};
    j.limited = {};
    commitAssembly(before);
}

void SketchEditor::flipJoint(int joint)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()))
        return;
    const Assembly before = m_assembly;
    m_assembly.joints[joint].flip = !m_assembly.joints[joint].flip;
    commitAssembly(before);
}

void SketchEditor::removeJoint(int joint)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()))
        return;
    const Assembly before = m_assembly;
    // The parts it held stay where it put them.
    for (int i = 0; i < static_cast<int>(m_assembly.components.size()); ++i) {
        if (i < static_cast<int>(m_solution.placedBy.size()) && m_solution.placedBy[i] == joint)
            m_assembly.components[i].matrix = matrixOf(m_solution.placements[i]);
    }
    m_assembly.joints.erase(m_assembly.joints.begin() + joint);
    commitAssembly(before);
}

// --- Checks and the exploded view ----------------------------------------------------------

void SketchEditor::checkInterference()
{
    const auto& ctx = m_viewer->aisContext();
    if (!m_interferencePrs.IsNull())
        ctx->Remove(m_interferencePrs, false);
    m_interferencePrs.Nullify();
    Assembly together = m_assembly;
    together.exploded = false;
    const std::vector<TopoDS_Shape> placed = placedShapes(together, solveAssembly(together).placements, m_sourceShapes);
    const std::vector<Interference> hits = interferences(placed);
    if (hits.empty()) {
        // How close they come: the smallest clearance, and between which parts.
        double nearest = std::numeric_limits<double>::max();
        int a = -1, b = -1;
        for (int i = 0; i < static_cast<int>(placed.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(placed.size()); ++j) {
                if (placed[i].IsNull() || placed[j].IsNull())
                    continue;
                const double gap = clearance(placed[i], placed[j]);
                if (gap < nearest) {
                    nearest = gap;
                    a = i;
                    b = j;
                }
            }
        }
        emit message(a < 0 ? tr("No parts overlap")
                           : tr("No parts overlap; the closest are %1 and %2, %3 mm apart")
                                 .arg(componentName(a), componentName(b))
                                 .arg(nearest, 0, 'f', 2));
        m_viewer->update();
        return;
    }
    TopoDS_Compound overlaps = newCompound();
    QStringList pairs;
    for (const Interference& hit : hits) {
        BRep_Builder().Add(overlaps, hit.common);
        pairs << tr("%1 and %2 (%3 mm³)").arg(componentName(hit.a), componentName(hit.b)).arg(hit.volume, 0, 'f', 2);
    }
    m_interferencePrs = new AIS_Shape(overlaps);
    m_interferencePrs->SetColor(m_colors.trim);
    m_interferencePrs->SetZLayer(Graphic3d_ZLayerId_Topmost);
    ctx->Display(m_interferencePrs, AIS_Shaded, -1, false);
    emit message(tr("Overlapping: %1").arg(pairs.join(QStringLiteral(", "))));
    m_viewer->update();
}

void SketchEditor::checkJointRange(int joint, int value)
{
    if (joint < 0 || joint >= static_cast<int>(m_assembly.joints.size()))
        return;
    const Joint& j = m_assembly.joints[joint];
    if (value >= degreesOfFreedom(j.kind))
        return;
    const auto hit = sweepJoint(m_assembly, m_sourceShapes, joint, value, isAngle(j.kind, value) ? 72 : 50);
    const QString name = jointInfos()[joint].name;
    if (!hit) {
        emit message(tr("%1 moves through its whole range without collisions").arg(name));
        return;
    }
    const QString unit = isAngle(j.kind, value) ? QStringLiteral("°") : QStringLiteral(" mm");
    emit message(tr("%1: %2 and %3 collide at %4%5")
                     .arg(name, componentName(hit->what.a), componentName(hit->what.b))
                     .arg(hit->at, 0, 'f', 1)
                     .arg(unit));
    // Shown where it happens.
    setJointValue(joint, value, hit->at, true);
    checkInterference();
}

void SketchEditor::setExploded(bool on)
{
    if (m_assembly.exploded == on)
        return;
    const Assembly before = m_assembly;
    m_assembly.exploded = on;
    if (on && m_assembly.explode.empty())
        autoExplode();
    commitAssembly(before);
}

void SketchEditor::autoExplode()
{
    m_assembly.explode = explodedOffsets(m_assembly, m_sourceShapes);
    m_assembly.exploded = true;
}

std::string SketchEditor::exportAssemblyStep(const std::string& path, const std::string& name) const
{
    std::vector<StepPart> parts;
    std::map<int, int> partOf; // by source id
    const std::map<int, int> bodies = bodySources(m_assembly, m_bodies);
    for (const ComponentSource& source : m_assembly.sources) {
        const auto shape = m_sourceShapes.find(source.id);
        if (shape == m_sourceShapes.end())
            continue;
        StepPart part{shape->second, sourceName(source.id).toStdString(), std::nullopt};
        if (const auto body = bodies.find(source.id); body != bodies.end()) {
            const QColor c = toQColor(bodyColor(body->second));
            part.color = std::array<double, 3>{c.redF(), c.greenF(), c.blueF()};
        }
        partOf[source.id] = static_cast<int>(parts.size());
        parts.push_back(part);
    }
    Assembly together = m_assembly;
    together.exploded = false;
    const AssemblySolution solution = solveAssembly(together);
    std::vector<StepInstance> instances;
    for (std::size_t i = 0; i < m_assembly.components.size(); ++i) {
        const Component& c = m_assembly.components[i];
        if (c.hidden || !partOf.count(c.source))
            continue;
        instances.push_back({partOf[c.source], solution.placements[i], componentName(static_cast<int>(i)).toStdString()});
    }
    return exportStepAssembly(parts, instances, name, path);
}

std::vector<PrintBody> SketchEditor::assemblyPrintBodies() const
{
    std::vector<PrintBody> out;
    const std::map<int, int> bodies = bodySources(m_assembly, m_bodies);
    Assembly together = m_assembly;
    together.exploded = false;
    const std::vector<TopoDS_Shape> placed = placedShapes(together, solveAssembly(together).placements, m_sourceShapes);
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].IsNull())
            continue;
        PrintBody body{placed[i], componentName(static_cast<int>(i)).toStdString(), {}};
        const auto b = bodies.find(m_assembly.components[i].source);
        const QColor c = toQColor(b != bodies.end() ? bodyColor(b->second) : m_colors.body);
        body.color = {c.redF(), c.greenF(), c.blueF()};
        out.push_back(body);
    }
    return out;
}

std::vector<SketchEditor::PlacedComponent> SketchEditor::placedComponents(bool exploded) const
{
    Assembly shown = m_assembly;
    shown.exploded = exploded;
    if (exploded && shown.explode.empty())
        shown.explode = explodedOffsets(shown, m_sourceShapes); // never exploded by hand: as Explode would
    const std::vector<TopoDS_Shape> placed = placedShapes(shown, solveAssembly(shown).placements, m_sourceShapes);
    std::vector<PlacedComponent> out;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (!placed[i].IsNull())
            out.push_back({placed[i], m_assembly.components[i].source, sourceName(m_assembly.components[i].source)});
    }
    return out;
}
