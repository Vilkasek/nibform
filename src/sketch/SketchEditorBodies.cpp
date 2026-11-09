// SketchEditor, model mode: sketch regions, solid bodies, extrude (with its arrow),
// fillet and chamfer.

#include "sketch/SketchEditor.h"

#include "model/Profiles.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_TexturedShape.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <Graphic3d_ZLayerSettings.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Image_PixMap.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Pln.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <Graphic3d_Camera.hxx>
#include <Prs3d_Drawer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <QFile>
#include <QImage>
#include <QLineF>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

namespace {

constexpr double kArrowPx = 70.0;    // on-screen length of the extrude arrow
constexpr double kArrowGrabPx = 14.0; // how close to it a press grabs it

TopoDS_Compound compound()
{
    TopoDS_Compound c;
    BRep_Builder().MakeCompound(c);
    return c;
}

// Where the line through `p` along `d` meets the plane through `origin` with normal `n`.
std::optional<gp_Pnt> onPlane(const gp_Pnt& p, const gp_Vec& d, const gp_Pnt& origin, const gp_Dir& n)
{
    const double denom = d.Dot(gp_Vec(n));
    if (std::abs(denom) < 1e-9)
        return std::nullopt;
    return p.Translated(d * (gp_Vec(p, origin).Dot(gp_Vec(n)) / denom));
}

gp_Pnt centroidOf(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

double segmentDistance(const QPointF& a, const QPointF& b, const QPointF& p)
{
    const QPointF d = b - a;
    const double len2 = QPointF::dotProduct(d, d);
    const double t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, d) / len2, 0.0, 1.0) : 0.0;
    return QLineF(a + d * t, p).length();
}

} // namespace

// --- History ------------------------------------------------------------------------

SketchEditor::Snapshot SketchEditor::snapshot() const
{
    Snapshot s;
    for (const Slot& slot : m_slots)
        s.sketches.push_back({slot.id, slot.sketch});
    s.features = m_features;
    return s;
}

void SketchEditor::restore(Snapshot snapshot)
{
    const auto& ctx = m_viewer->aisContext();
    while (m_slots.size() > snapshot.sketches.size()) {
        ctx->Remove(m_slots.back().prs, false);
        m_slots.pop_back();
    }
    for (std::size_t i = 0; i < snapshot.sketches.size(); ++i) {
        if (i >= m_slots.size()) {
            Slot slot;
            slot.prs = new PlanarPrs(snapshot.sketches[i].sketch.plane(),
                                     {m_colors.sketch, m_colors.accent, 2.0 * m_viewer->devicePixelRatioF()});
            ctx->Display(slot.prs, 0, -1, false);
            m_slots.push_back(std::move(slot));
        }
        m_slots[i].id = snapshot.sketches[i].id;
        m_slots[i].sketch = std::move(snapshot.sketches[i].sketch);
        m_slots[i].prs->setPlane(m_slots[i].sketch.plane());
        m_slots[i].regions = findRegions(m_slots[i].sketch);
        m_nextSketchId = std::max(m_nextSketchId, m_slots[i].id + 1);
    }
    m_regionSelection.clear();
    m_features = std::move(snapshot.features);
    rebuildModel();
    refreshSketches();
    showRegions();
}

void SketchEditor::pushUndo()
{
    m_undo.push_back(snapshot());
    m_redo.clear();
    ++m_changes;
}

SketchLookup SketchEditor::sketchLookup() const
{
    return [this](int id) -> const Sketch* {
        for (const Slot& slot : m_slots) {
            if (slot.id == id)
                return &slot.sketch;
        }
        return nullptr;
    };
}

void SketchEditor::rebuildModel()
{
    const bool rolledBack = m_active >= 0 && m_rollback >= 0;
    const std::size_t count = rolledBack ? static_cast<std::size_t>(m_rollback) : m_features.size();
    RebuildResult result =
        rebuild(sketchLookup(), std::vector<Feature>(m_features.begin(), m_features.begin() + static_cast<long>(count)));
    m_bodies = std::move(result.bodies);
    m_context = std::move(result.context);
    // Features past the rollback point keep what they reported last time.
    m_featureErrors.resize(m_features.size());
    std::copy(result.errors.begin(), result.errors.end(), m_featureErrors.begin());
    refreshBodies();
    showRegions(); // what is extruded now hides, what failed shows
    showDatums();
    updateStatus();
    m_viewer->update();
}

bool SketchEditor::commitFeature(const Feature& made)
{
    Feature feature = made;
    feature.id = nextFeatureId();
    // Applied on top of the current bodies: the same as a full rebuild, without replaying.
    FeatureResult result = applyFeature(sketchLookup(), m_bodies, feature, m_context);
    if (!result.ok()) {
        emit message(modelText(result.error));
        return false;
    }
    m_valueEditor->cancel();
    pushUndo();
    remember(m_context, feature, result);
    m_features.push_back(feature);
    m_featureErrors.emplace_back();
    m_bodies = std::move(result.bodies);
    refreshBodies();
    clearModelSelection();
    showRegions(); // the extruded regions hide
    showDatums();
    updateStatus();
    emit historyChanged();
    m_viewer->update();
    return true;
}

std::optional<Feature> SketchEditor::featureFromSession(double value) const
{
    // The picks, written down as geometry that can be found again after rebuilds.
    using Kind = ValueSession::Kind;
    Feature f;
    f.value = value;
    if (m_session.kind == Kind::Extrude) {
        f.kind = Feature::Kind::Extrude;
        f.operation = m_bodyOperation;
        for (const RegionRef& r : m_regionSelection) {
            const Slot& slot = m_slots[r.sketch];
            const auto inside = insidePoint(slot.sketch, slot.regions[r.index]);
            if (!inside)
                return std::nullopt;
            f.regions.push_back({slot.id, *inside});
        }
        for (const BodyPick& p : m_bodySelection) {
            if (p.shape.ShapeType() == TopAbs_FACE && planarNormal(TopoDS::Face(p.shape)))
                f.faces.push_back(facePick(TopoDS::Face(p.shape)));
        }
    } else if (m_session.kind == Kind::Shell) {
        f.kind = Feature::Kind::Shell;
        for (const auto& [body, face] : selectedFaces())
            f.faces.push_back(facePick(face));
    } else {
        f.kind = m_session.kind == Kind::Fillet ? Feature::Kind::Fillet : Feature::Kind::Chamfer;
        for (const EdgeRef& e : selectedEdges())
            f.edges.push_back(edgePick(e.edge));
    }
    return f;
}

// --- Presentation -------------------------------------------------------------------

void SketchEditor::refreshBodies()
{
    const auto& ctx = m_viewer->aisContext();
    // Picked faces / edges belong to the old presentations.
    ctx->ClearSelected(false);
    m_bodySelection.clear();
    for (const Handle(AIS_Shape)& prs : m_bodyPrs)
        ctx->Remove(prs, false);
    m_bodyPrs.clear();

    for (const TopoDS_Shape& body : m_bodies) {
        Handle(AIS_Shape) prs = new AIS_Shape(body);
        // Matte: with the light coming from the camera, a glossy face seen head-on burns to white.
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
        material.SetSpecularColor(Quantity_Color(0.06, 0.06, 0.06, Quantity_TOC_RGB));
        prs->SetMaterial(material);
        prs->SetColor(m_colors.body);
        // Displayed with a real selection mode: shown with -1 ("not selectable"), OCCT
        // ignores any later Activate() and the body can never be picked.
        ctx->Display(prs, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        m_bodyPrs.push_back(prs);
    }
    updateSelectionModes();
    emit selectionChanged();
}

void SketchEditor::updateSelectionModes()
{
    // Bodies offer faces and edges (edges win near the pointer); push / pull only faces;
    // nothing while a sketch is open, unless body edges are being projected into it.
    const auto& ctx = m_viewer->aisContext();
    const bool model = mode() == EditorMode::Model || m_toolKind == ToolKind::Project;
    const int faces = AIS_Shape::SelectionMode(TopAbs_FACE);
    const int edges = AIS_Shape::SelectionMode(TopAbs_EDGE);
    for (const Handle(AIS_Shape)& prs : m_bodyPrs) {
        // Mode by mode, asking the selection manager itself: the context's own list of
        // active modes can claim a mode the selector never got, and then nothing is picked.
        auto set = [&](int selectionMode, bool on) {
            const bool is = ctx->SelectionManager()->IsActivated(prs, selectionMode);
            if (on && !is)
                ctx->Activate(prs, selectionMode, true);
            else if (!on && is)
                ctx->Deactivate(prs, selectionMode);
        };
        // Tools that take corners, and those that don't want edges in the way.
        const bool corners = m_toolKind == ToolKind::ThreePointPlane || m_toolKind == ToolKind::Axis;
        const bool noEdges = m_toolKind == ToolKind::PushPull || m_toolKind == ToolKind::Shell
                          || m_toolKind == ToolKind::Hole || m_toolKind == ToolKind::Loft
                          || m_toolKind == ToolKind::Sweep || m_toolKind == ToolKind::OffsetPlane
                          || m_toolKind == ToolKind::ThreePointPlane;
        set(faces, model);
        set(edges, model && !noEdges);
        set(AIS_Shape::SelectionMode(TopAbs_VERTEX), model && corners);
    }
    rehighlightPicks();
}

void SketchEditor::rehighlightPicks()
{
    const auto& ctx = m_viewer->aisContext();
    ctx->ClearSelected(false);
    for (const BodyPick& p : m_bodySelection)
        ctx->AddOrRemoveSelected(p.owner, false);
}

Handle(SelectMgr_EntityOwner) SketchEditor::ownerOf(int body, const TopoDS_Shape& shape) const
{
    const Handle(AIS_Shape)& prs = m_bodyPrs[body];
    const int mode = AIS_Shape::SelectionMode(shape.ShapeType());
    if (!prs->HasSelection(mode))
        return {};
    for (const Handle(SelectMgr_SensitiveEntity)& entity : prs->Selection(mode)->Entities()) {
        const auto owner = Handle(StdSelect_BRepOwner)::DownCast(entity->BaseSensitive()->OwnerId());
        if (!owner.IsNull() && owner->Shape().IsSame(shape))
            return owner;
    }
    return {};
}

void SketchEditor::keepPicks(bool edges, bool faces, bool regions)
{
    const std::size_t before = m_bodySelection.size() + m_regionSelection.size();
    std::erase_if(m_bodySelection, [&](const BodyPick& p) {
        return p.shape.ShapeType() == TopAbs_EDGE ? !edges : !faces;
    });
    if (!regions)
        m_regionSelection.clear();
    if (m_bodySelection.size() + m_regionSelection.size() != before) {
        rehighlightPicks();
        showRegions();
        emit selectionChanged();
    }
}

bool SketchEditor::hasPickedEdges() const
{
    return !selectedEdges().empty();
}

bool SketchEditor::hasPickedFaces() const
{
    return !selectedFaces().empty();
}

std::vector<std::pair<int, TopoDS_Face>> SketchEditor::selectedFaces() const
{
    std::vector<std::pair<int, TopoDS_Face>> out;
    for (const BodyPick& p : m_bodySelection) {
        if (p.shape.ShapeType() == TopAbs_FACE && planarNormal(TopoDS::Face(p.shape)))
            out.push_back({p.body, TopoDS::Face(p.shape)});
    }
    return out;
}

void SketchEditor::refreshRegions(int sketch)
{
    if (sketch < 0 || sketch >= static_cast<int>(m_slots.size()))
        return;
    m_slots[sketch].regions = findRegions(m_slots[sketch].sketch);
    std::erase_if(m_regionSelection, [sketch](const RegionRef& r) { return r.sketch == sketch; });
    showRegions();
}

std::set<RegionRef> SketchEditor::usedRegions() const
{
    std::set<RegionRef> used;
    const std::size_t count = m_rollback >= 0 && m_active >= 0 ? static_cast<std::size_t>(m_rollback) : m_features.size();
    for (std::size_t f = 0; f < count && f < m_features.size(); ++f) {
        const Feature& feature = m_features[f];
        // One that failed keeps its region in sight, to see what went wrong.
        const bool fromRegions = feature.kind == Feature::Kind::Extrude || feature.kind == Feature::Kind::Revolve
                              || feature.kind == Feature::Kind::Sweep || feature.kind == Feature::Kind::Loft;
        if (!fromRegions || (f < m_featureErrors.size() && !m_featureErrors[f].empty()))
            continue;
        for (const RegionPick& pick : feature.regions) {
            for (int s = 0; s < static_cast<int>(m_slots.size()); ++s) {
                if (m_slots[s].id != pick.sketch)
                    continue;
                if (const auto index = ::regionAt(m_slots[s].regions, m_slots[s].sketch.to3d(pick.inside)))
                    used.insert({s, static_cast<int>(*index)});
            }
        }
    }
    return used;
}

bool SketchEditor::regionShown(const RegionRef& region) const
{
    // While a sketch is open, all of its regions show: it is being drawn, not used.
    return m_showUsedRegions || region.sketch == m_active || !m_usedRegions.count(region);
}

void SketchEditor::setShowUsedRegions(bool on)
{
    m_showUsedRegions = on;
    if (!on) {
        // Hidden regions can't stay picked.
        std::erase_if(m_regionSelection, [this](const RegionRef& r) { return !regionShown(r); });
        syncValueSession();
    }
    showRegions();
    m_viewer->update();
}

void SketchEditor::showRegions()
{
    const auto& ctx = m_viewer->aisContext();
    m_usedRegions = usedRegions();
    TopoDS_Compound plain = compound(), picked = compound();
    bool anyPlain = false, anyPicked = false;
    for (int s = 0; s < static_cast<int>(m_slots.size()); ++s) {
        for (int i = 0; i < static_cast<int>(m_slots[s].regions.size()); ++i) {
            if (!regionShown({s, i}))
                continue;
            const bool selected = m_regionSelection.count({s, i}) > 0;
            BRep_Builder().Add(selected ? picked : plain, m_slots[s].regions[i]);
            (selected ? anyPicked : anyPlain) = true;
        }
    }

    // A faint fill shows which areas are closed (and can be pulled); picked ones stand out.
    auto show = [&](Handle(AIS_Shape)& prs, const TopoDS_Shape& shape, bool any, const Quantity_Color& color,
                    double transparency) {
        if (!prs.IsNull())
            ctx->Remove(prs, false);
        prs.Nullify();
        if (!any)
            return;
        prs = new AIS_Shape(shape);
        prs->Attributes()->SetFaceBoundaryDraw(false);
        prs->SetColor(color);
        prs->SetTransparency(transparency);
        prs->SetZLayer(Graphic3d_ZLayerId_Top);
        ctx->Display(prs, AIS_Shaded, -1, false); // not pickable through AIS: hit-tested on the planes
    };
    show(m_regionPrs, plain, anyPlain, m_colors.region, 0.85);
    show(m_selectedRegionPrs, picked, anyPicked, m_colors.accent, 0.5);
    showHoverRegion();
}

void SketchEditor::showHoverRegion()
{
    const auto& ctx = m_viewer->aisContext();
    if (!m_hoverRegionPrs.IsNull())
        ctx->Remove(m_hoverRegionPrs, false);
    m_hoverRegionPrs.Nullify();
    const RegionRef r = m_hover.region;
    const bool region = mode() == EditorMode::Model && m_hover.kind == Hit::Kind::Region && r.sketch >= 0
                     && r.sketch < static_cast<int>(m_slots.size()) && r.index >= 0
                     && r.index < static_cast<int>(m_slots[r.sketch].regions.size()) && regionShown(r);
    // A face behind the region isn't what a tap would pick: no highlight for it.
    m_viewer->SetAllowHighlight(!region);
    if (region)
        ctx->ClearDetected(false);
    if (!region || m_regionSelection.count(r))
        return;
    m_hoverRegionPrs = new AIS_Shape(m_slots[r.sketch].regions[r.index]);
    m_hoverRegionPrs->Attributes()->SetFaceBoundaryDraw(false);
    m_hoverRegionPrs->SetColor(m_colors.preview);
    m_hoverRegionPrs->SetTransparency(0.7);
    m_hoverRegionPrs->SetZLayer(Graphic3d_ZLayerId_Top);
    ctx->Display(m_hoverRegionPrs, AIS_Shaded, -1, false);
}

void SketchEditor::applyModelColors()
{
    const auto& ctx = m_viewer->aisContext();
    for (const Handle(AIS_Shape)& prs : m_bodyPrs)
        ctx->SetColor(prs, m_colors.body, false);
    // Faces / edges under the pointer and picked, matching the sketch's hover and selection.
    ctx->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->SetColor(m_colors.preview);
    ctx->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(m_colors.accent);
    // Drawn in the body's own layer, so faces in front still hide a highlighted one
    // (by default highlights go on top and show through everything).
    for (auto type : {Prs3d_TypeOfHighlight_LocalDynamic, Prs3d_TypeOfHighlight_LocalSelected,
                      Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_Selected})
        ctx->HighlightStyle(type)->SetZLayer(Graphic3d_ZLayerId_UNKNOWN);
    showRegions();
    if (!m_previewPrs3d.IsNull())
        showSessionPreview();
}

// --- Picking ------------------------------------------------------------------------

SketchEditor::Hit SketchEditor::modelHitAt(const PointerEvent& e) const
{
    const Handle(V3d_View)& view = m_viewer->view();
    if (view.IsNull())
        return {};
    const Handle(Graphic3d_Camera)& camera = view->Camera();
    // Distance in front of the eye, to keep whichever pick is nearer.
    auto depth = [&](const gp_Pnt& p) { return gp_Vec(camera->Eye(), p).Dot(gp_Vec(camera->Direction())); };
    // Regions only where they can be used: pulled, turned, swept, lofted.
    const bool regionTool = m_toolKind == ToolKind::Select || m_toolKind == ToolKind::PushPull
                         || m_toolKind == ToolKind::Revolve || m_toolKind == ToolKind::Sweep
                         || m_toolKind == ToolKind::Loft;

    // Sketch curves (an axis to turn about, a path to sweep along) lie on top of the rest.
    const bool lineTool = m_toolKind == ToolKind::Revolve || m_toolKind == ToolKind::BodyLinearPattern
                       || m_toolKind == ToolKind::BodyCircularPattern || m_toolKind == ToolKind::AnglePlane;
    if (lineTool || m_toolKind == ToolKind::Sweep) {
        Hit curve;
        double nearest = 10.0 * m_touchScale;
        for (int s = 0; s < static_cast<int>(m_slots.size()); ++s) {
            const Sketch& sketch = m_slots[s].sketch;
            for (const SketchEntity& entity : sketch.entities()) {
                if (lineTool && entity.kind != EntityKind::Line)
                    continue;
                const std::vector<gp_Pnt2d> pts = sketch.polyline(entity);
                for (std::size_t i = 1; i < pts.size(); ++i) {
                    const double d = segmentDistance(m_viewer->project(sketch.to3d(pts[i - 1])),
                                                     m_viewer->project(sketch.to3d(pts[i])), e.pos);
                    if (d < nearest) {
                        nearest = d;
                        curve = {Hit::Kind::SketchCurve, entity.id};
                        curve.region = {s, -1};
                    }
                }
            }
        }
        if (curve.kind != Hit::Kind::None)
            return curve;
    }

    Hit best;
    double bestDepth = std::numeric_limits<double>::max();
    if (regionTool) {
        double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
        view->ConvertWithProj(e.pixel.x(), e.pixel.y(), x, y, z, dx, dy, dz);
        for (int s = 0; s < static_cast<int>(m_slots.size()); ++s) {
            const gp_Ax3& plane = m_slots[s].sketch.plane();
            const auto p = onPlane(gp_Pnt(x, y, z), gp_Vec(dx, dy, dz), plane.Location(), plane.Direction());
            if (!p)
                continue;
            const auto region = ::regionAt(m_slots[s].regions, *p);
            if (region && regionShown({s, static_cast<int>(*region)}) && depth(*p) < bestDepth) {
                best = {Hit::Kind::Region};
                best.region = {s, static_cast<int>(*region)};
                bestDepth = depth(*p);
            }
        }
    }

    const auto& ctx = m_viewer->aisContext();
    ctx->MoveTo(e.pixel.x(), e.pixel.y(), view, false);
    if (!ctx->HasDetected())
        return best;
    // Nearest first. A datum plane (a big see-through square) gives way to anything else
    // under the pointer; a datum axis is picked like an edge.
    const Handle(StdSelect_ViewerSelector3d)& selector = ctx->MainSelector();
    Hit plane;
    for (int i = 1; i <= selector->NbPicked(); ++i) {
        const Handle(SelectMgr_EntityOwner) owner = selector->Picked(i);
        if (owner.IsNull())
            continue;
        const auto datum = std::find_if(m_datumPrs.begin(), m_datumPrs.end(),
                                        [&](const auto& d) { return d.first == owner->Selectable(); });
        if (datum != m_datumPrs.end()) {
            const auto d = datumById(datum->second);
            Hit hit{Hit::Kind::Datum, datum->second};
            hit.owner = owner;
            if (d && !d->plane) {
                if (depth(selector->PickedPoint(i)) < bestDepth - 1e-6)
                    best = hit;
                return best;
            }
            if (plane.kind == Hit::Kind::None)
                plane = hit;
            continue;
        }
        const auto brep = Handle(StdSelect_BRepOwner)::DownCast(owner);
        const auto it = std::find(m_bodyPrs.begin(), m_bodyPrs.end(), owner->Selectable());
        if (brep.IsNull() || it == m_bodyPrs.end())
            continue;
        const TopAbs_ShapeEnum wanted = brep->Shape().ShapeType();
        if (wanted != TopAbs_FACE && wanted != TopAbs_EDGE && wanted != TopAbs_VERTEX)
            continue;
        const int body = static_cast<int>(it - m_bodyPrs.begin());

        // The face / edge as it sits in the body, so a face's orientation gives its outward normal.
        TopoDS_Shape shape;
        for (TopExp_Explorer f(m_bodies[body], wanted); f.More(); f.Next()) {
            if (f.Current().IsSame(brep->Shape()))
                shape = f.Current();
        }
        if (shape.IsNull())
            continue;
        if (depth(selector->PickedPoint(i)) < bestDepth - 1e-6) {
            best = {wanted == TopAbs_EDGE ? Hit::Kind::Edge : wanted == TopAbs_VERTEX ? Hit::Kind::Vertex : Hit::Kind::Face,
                    body};
            best.owner = owner;
            best.shape = std::move(shape);
        }
        return best;
    }
    return best.kind == Hit::Kind::None ? plane : best;
}

bool SketchEditor::isPicked(const Hit& hit) const
{
    if (hit.kind == Hit::Kind::Region)
        return m_regionSelection.count(hit.region) > 0;
    return std::any_of(m_bodySelection.begin(), m_bodySelection.end(),
                       [&](const BodyPick& p) { return p.owner == hit.owner; });
}

void SketchEditor::togglePick(const Hit& hit)
{
    if (hit.kind == Hit::Kind::Region) {
        if (!m_regionSelection.erase(hit.region))
            m_regionSelection.insert(hit.region);
        showRegions();
    } else if (hit.kind == Hit::Kind::Face || hit.kind == Hit::Kind::Edge) {
        const auto it = std::find_if(m_bodySelection.begin(), m_bodySelection.end(),
                                     [&](const BodyPick& p) { return p.owner == hit.owner; });
        if (it != m_bodySelection.end())
            m_bodySelection.erase(it);
        else
            m_bodySelection.push_back({hit.owner, hit.shape, hit.id});
        m_viewer->aisContext()->AddOrRemoveSelected(hit.owner, false);
    }
    emit selectionChanged();
    m_viewer->update();
}

void SketchEditor::clearModelSelection()
{
    if (m_selectedDatum != 0) {
        m_selectedDatum = 0;
        showDatums();
    }
    const bool had = !m_regionSelection.empty() || !m_bodySelection.empty();
    m_regionSelection.clear();
    m_bodySelection.clear();
    m_viewer->aisContext()->ClearSelected(false);
    if (had) {
        showRegions();
        emit selectionChanged();
    }
}

std::vector<int> SketchEditor::selectedBodies() const
{
    std::vector<int> out;
    for (const BodyPick& p : m_bodySelection) {
        if (std::find(out.begin(), out.end(), p.body) == out.end())
            out.push_back(p.body);
    }
    return out;
}

std::vector<Profile> SketchEditor::selectedProfiles() const
{
    std::vector<Profile> out;
    for (const RegionRef& r : m_regionSelection) {
        const Slot& slot = m_slots[r.sketch];
        if (r.index < static_cast<int>(slot.regions.size()))
            out.push_back({slot.regions[r.index], slot.sketch.plane().Direction(), -1});
    }
    for (const BodyPick& p : m_bodySelection) {
        if (p.shape.ShapeType() != TopAbs_FACE)
            continue;
        const TopoDS_Face face = TopoDS::Face(p.shape);
        if (const auto n = planarNormal(face))
            out.push_back({face, *n, p.body});
    }
    return out;
}

std::vector<EdgeRef> SketchEditor::selectedEdges() const
{
    std::vector<EdgeRef> out;
    for (const BodyPick& p : m_bodySelection) {
        if (p.shape.ShapeType() == TopAbs_EDGE)
            out.push_back({p.body, TopoDS::Edge(p.shape)});
    }
    return out;
}

bool SketchEditor::canSketchOnFace() const
{
    if (mode() == EditorMode::Model && m_selectedDatum != 0)
        return true;
    return mode() == EditorMode::Model && m_bodySelection.size() == 1
        && m_bodySelection[0].shape.ShapeType() == TopAbs_FACE
        && planarNormal(TopoDS::Face(m_bodySelection[0].shape)).has_value();
}

void SketchEditor::createSketchOnFace()
{
    if (!canSketchOnFace())
        return;
    if (m_selectedDatum != 0) {
        const auto d = datumById(m_selectedDatum);
        m_selectedDatum = 0;
        if (d && d->plane)
            createSketch(d->frame);
        return;
    }
    const TopoDS_Face face = TopoDS::Face(m_bodySelection[0].shape);
    // Centered on the face, facing out of the body, with the face's outline as fixed
    // reference geometry to snap and dimension to.
    const gp_Dir normal = *planarNormal(face);
    const gp_Dir xDir = BRepAdaptor_Surface(face).Plane().XAxis().Direction();
    createSketch(gp_Ax3(centroidOf(face), normal, xDir));
    commit([&](Sketch& s) { projectFaceEdges(s, face); });
}

std::optional<RegionRef> SketchEditor::regionAt(int sketch, const gp_Pnt2d& p) const
{
    if (sketch < 0 || sketch >= static_cast<int>(m_slots.size()))
        return std::nullopt;
    const Slot& slot = m_slots[sketch];
    if (const auto index = ::regionAt(slot.regions, slot.sketch.to3d(p)))
        return RegionRef{sketch, static_cast<int>(*index)};
    return std::nullopt;
}

// --- Input --------------------------------------------------------------------------

bool SketchEditor::modelPress(const PointerEvent& e)
{
    m_modelPress = {};
    m_tapPos = e.pos;
    // The arrow takes the press first: dragging it sets the distance / radius.
    if (hitsArrow(e.pos)) {
        m_session.dragging = true;
        m_session.dragStartValue = m_session.value;
        m_session.dragStartParam = arrowParam(e.pixel);
        return true;
    }
    m_modelPress = modelHitAt(e);
    return false;
}

bool SketchEditor::modelMove(const PointerEvent& e, bool pressed)
{
    if (!pressed) {
        // Hovering shows what a tap would pick: AIS highlights faces / edges by itself,
        // regions (which win when nearer) are lit here.
        const Hit hit = modelHitAt(e);
        if (hit == m_hover)
            return hit.kind == Hit::Kind::Region;
        m_hover = hit;
        showHoverRegion();
        return true;
    }
    if (!m_session.dragging)
        return false;
    m_session.value = m_session.dragStartValue + arrowParam(e.pixel) - m_session.dragStartParam;
    if (m_session.kind != ValueSession::Kind::Extrude)
        m_session.value = std::max(m_session.value, 0.01); // a round can't go below nothing
    m_valueEditor->setValue(std::round(m_session.value * 100.0) / 100.0);
    showSessionPreview();
    updateHint();
    return true;
}

bool SketchEditor::modelRelease(const PointerEvent&)
{
    if (m_session.dragging) {
        m_session.dragging = false;
        // Snap the dragged distance to what the field shows, and keep typing there.
        m_session.value = std::round(m_session.value * 100.0) / 100.0;
        showSessionPreview();
        m_valueEditor->refocus();
        return true;
    }
    const Hit hit = m_modelPress;
    m_modelPress = {};
    if (isSolidTool(m_toolKind)) {
        solidTap(hit);
        return true;
    }
    if (hit.kind == Hit::Kind::Datum) {
        // Select: a plane picked here is where New Sketch starts.
        const auto d = datumById(hit.id);
        m_selectedDatum = d && d->plane && m_selectedDatum != hit.id ? hit.id : 0;
        showDatums();
        emit selectionChanged();
        m_viewer->update();
        return true;
    }
    if (hit.kind == Hit::Kind::None) {
        endValueSession(true); // a tap on empty space drops everything
        clearModelSelection();
        return true;
    }
    const bool edgeTool = m_toolKind == ToolKind::Fillet || m_toolKind == ToolKind::Chamfer;
    if (edgeTool && hit.kind == Hit::Kind::Face)
        toggleFaceEdges(hit);
    else
        togglePick(hit);
    syncValueSession();
    if (m_valueEditor->isOpen())
        m_valueEditor->refocus();
    return true;
}

void SketchEditor::toggleFaceEdges(const Hit& face)
{
    // All of the face's edges: added if any is missing, otherwise all removed.
    std::vector<Hit> edges;
    for (TopExp_Explorer it(face.shape, TopAbs_EDGE); it.More(); it.Next()) {
        TopoDS_Shape inBody;
        for (TopExp_Explorer e(m_bodies[face.id], TopAbs_EDGE); e.More(); e.Next()) {
            if (e.Current().IsSame(it.Current()))
                inBody = e.Current();
        }
        const Handle(SelectMgr_EntityOwner) owner = ownerOf(face.id, inBody);
        if (inBody.IsNull() || owner.IsNull())
            continue;
        Hit edge{Hit::Kind::Edge, face.id};
        edge.owner = owner;
        edge.shape = inBody;
        if (std::none_of(edges.begin(), edges.end(), [&](const Hit& h) { return h.owner == owner; }))
            edges.push_back(edge);
    }
    const bool all = std::all_of(edges.begin(), edges.end(), [&](const Hit& h) { return isPicked(h); });
    for (const Hit& edge : edges) {
        if (all || !isPicked(edge))
            togglePick(edge);
    }
}

// --- Value sessions: extrude / fillet / chamfer -------------------------------------

namespace {
// In the order the value field offers them.
constexpr BodyOperation kOperations[] = {BodyOperation::Auto, BodyOperation::NewBody, BodyOperation::Join,
                                         BodyOperation::Cut};

int operationIndex(BodyOperation operation)
{
    return static_cast<int>(std::find(std::begin(kOperations), std::end(kOperations), operation)
                            - std::begin(kOperations));
}
} // namespace

QStringList SketchEditor::operationLabels(std::optional<BodyOperation> automatic)
{
    QString autoLabel = tr("Automatic");
    if (automatic == BodyOperation::Cut)
        autoLabel = tr("Automatic: cut");
    else if (automatic == BodyOperation::Join)
        autoLabel = tr("Automatic: join");
    return {autoLabel, tr("New Body"), tr("Join"), tr("Cut")};
}

std::optional<BodyOperation> SketchEditor::sessionAutoOperation()
{
    if (m_session.kind != ValueSession::Kind::Extrude || std::abs(m_session.value) < 1e-6)
        return std::nullopt;
    std::vector<Profile> sketchProfiles;
    for (const Profile& p : selectedProfiles()) {
        if (p.body < 0)
            sketchProfiles.push_back(p);
    }
    if (sketchProfiles.empty())
        return m_session.value > 0.0 ? BodyOperation::Join : BodyOperation::Cut; // body faces
    const bool forward = m_session.value > 0.0;
    if (!m_session.startsInside || m_session.startsInside->first != forward)
        m_session.startsInside = std::pair(forward, startsInside(m_bodies, sketchProfiles, m_session.value));
    if (m_session.startsInside->second)
        return BodyOperation::Cut;
    return std::nullopt; // joins what it reaches, or makes a new body
}

void SketchEditor::syncValueSession()
{
    using Kind = ValueSession::Kind;
    Kind want = Kind::None;
    if (m_toolKind == ToolKind::Shell && !selectedFaces().empty())
        want = Kind::Shell;
    else if (m_toolKind == ToolKind::Fillet && !selectedEdges().empty())
        want = Kind::Fillet;
    else if (m_toolKind == ToolKind::Chamfer && !selectedEdges().empty())
        want = Kind::Chamfer;
    else if ((m_toolKind == ToolKind::Select && !m_regionSelection.empty())
             || (m_toolKind == ToolKind::PushPull && !selectedProfiles().empty()))
        want = Kind::Extrude; // picking a sketch region is enough to start pulling it

    if (want == Kind::None) {
        endValueSession(true);
        return;
    }
    // Extrude: the arrow starts at the first profile's middle, along its normal.
    // Fillet / chamfer: at the first edge's middle, into the material.
    auto placeArrow = [this, want] {
        m_session.arrow = false;
        if (want == Kind::Extrude) {
            const std::vector<Profile> profiles = selectedProfiles();
            m_session.base = centroidOf(profiles.front().face);
            m_session.normal = profiles.front().normal;
            m_session.arrow = true;
        } else if (want == Kind::Shell) {
            // Into the material from the first opened face: dragging in thickens the walls.
            const TopoDS_Face face = selectedFaces().front().second;
            m_session.base = centroidOf(face);
            m_session.normal = planarNormal(face)->Reversed();
            m_session.arrow = true;
        } else {
            const EdgeRef first = selectedEdges().front();
            if (const auto handle = edgeHandle(m_bodies[first.body], first.edge)) {
                m_session.base = handle->first;
                m_session.normal = handle->second;
                m_session.arrow = true;
            }
        }
    };
    if (m_session.kind == want) {
        m_session.startsInside.reset(); // other picks
        placeArrow();
        showSessionPreview(); // more / fewer picks, same value
        return;
    }

    endValueSession(true);
    m_session.kind = want;
    placeArrow();
    m_session.value = want == Kind::Extrude ? 10.0 : 1.0;
    showSessionPreview();

    const double min = want == Kind::Extrude ? -1e5 : 1e-3;
    QPointF anchor = m_pointerPos - QPointF(0, 40);
    auto label = [want] {
        return want == Kind::Shell     ? tr("Wall thickness [mm]")
             : want == Kind::Fillet    ? tr("Radius [mm]")
             : want == Kind::Chamfer   ? tr("Chamfer [mm]")
                                       : tr("Distance [mm]");
    };
    m_valueEditor->open(
        label, m_session.value, min, 1e5, anchor, [this](double value) { applySession(value); },
        [this] {
            // Esc / ✕: drop the operation and what was picked for it.
            if (m_session.kind != ValueSession::Kind::None) {
                endValueSession(true);
                clearModelSelection();
            }
        },
        [this](double value) {
            m_session.value = value;
            showSessionPreview();
        });
    if (want == Kind::Extrude) {
        // What the extrusion does, right where the value is typed.
        m_valueEditor->setChoices(
            [this] {
                return operationLabels(m_bodyOperation == BodyOperation::Auto ? sessionAutoOperation()
                                                                              : std::nullopt);
            },
            operationIndex(m_bodyOperation),
            [this](int index) {
                setBodyOperation(kOperations[index]);
                showSessionPreview();
            });
    }
    if (const auto arrow = arrowOnScreen())
        m_valueEditor->placeBeside(arrow->second); // next to the arrow's head, not over it
    updateHint();
}

void SketchEditor::endValueSession(bool restoreBodies)
{
    const bool had = m_session.kind != ValueSession::Kind::None;
    m_session = {}; // first, so dropping the field below doesn't come back here
    hideSessionPreview();
    if (restoreBodies) {
        for (const Handle(AIS_Shape)& prs : m_bodyPrs)
            m_viewer->aisContext()->UnsetTransparency(prs, false);
    }
    if (had && m_valueEditor->isOpen())
        m_valueEditor->cancel();
    if (had) {
        updateHint();
        m_viewer->update();
    }
}

void SketchEditor::showSessionPreview()
{
    using Kind = ValueSession::Kind;
    const auto& ctx = m_viewer->aisContext();
    m_session.error.clear();
    TopoDS_Compound shapes = compound();
    bool any = false;
    bool solid = false; // fillet / chamfer previews look like bodies, extrusions like ghosts

    if (m_session.kind == Kind::Extrude && std::abs(m_session.value) > 1e-6) {
        for (const Profile& p : selectedProfiles()) {
            BRepPrimAPI_MakePrism prism(p.face, gp_Vec(p.normal) * m_session.value);
            if (prism.IsDone()) {
                BRep_Builder().Add(shapes, prism.Shape());
                any = true;
            }
        }
    } else if (m_session.kind != Kind::None) {
        // Fillet, chamfer or shell: show the finished bodies over their faded originals.
        std::set<int> changed;
        ModelResult result;
        if (m_session.kind == Kind::Shell) {
            const auto faces = selectedFaces();
            result = shellBodies(m_bodies, faces, m_session.value);
            for (const auto& f : faces)
                changed.insert(f.first);
        } else {
            const std::vector<EdgeRef> edges = selectedEdges();
            result = m_session.kind == Kind::Fillet ? filletEdges(m_bodies, edges, m_session.value)
                                                    : chamferEdges(m_bodies, edges, m_session.value);
            for (const EdgeRef& e : edges)
                changed.insert(e.body);
        }
        // The originals fade out behind the preview, but stay pickable for more edges.
        for (int i = 0; i < bodyCount(); ++i) {
            if (changed.count(i) && result.ok())
                ctx->SetTransparency(m_bodyPrs[i], 0.85, false);
            else
                ctx->UnsetTransparency(m_bodyPrs[i], false);
        }
        if (result.ok()) {
            for (int i : changed)
                BRep_Builder().Add(shapes, result.bodies[i]);
            any = solid = !changed.empty();
        } else {
            m_session.error = result.error;
        }
    }

    // The field rides along with the arrow's head as the value changes.
    if (m_valueEditor->isOpen()) {
        if (const auto arrow = arrowOnScreen())
            m_valueEditor->placeBeside(arrow->second);
    }

    if (!any) {
        hideSessionPreview();
    } else {
        if (m_previewPrs3d.IsNull()) {
            m_previewPrs3d = new AIS_Shape(shapes);
            ctx->Display(m_previewPrs3d, AIS_Shaded, -1, false);
        } else {
            m_previewPrs3d->SetShape(shapes);
            ctx->Redisplay(m_previewPrs3d, false);
        }
        // A cut shows red, anything that adds material blue.
        const std::optional<BodyOperation> effective =
            m_bodyOperation == BodyOperation::Auto ? sessionAutoOperation() : m_bodyOperation;
        const bool cut = m_session.kind == Kind::Extrude && effective == BodyOperation::Cut;
        ctx->SetColor(m_previewPrs3d, solid ? m_colors.body : cut ? m_colors.trim : m_colors.preview, false);
        ctx->SetTransparency(m_previewPrs3d, solid ? 0.0 : 0.55, false);
    }
    if (m_valueEditor->isOpen())
        m_valueEditor->refreshChoices(); // "Automatic: cut" as the value turns around
    updateHint();
    m_viewer->update();
}

void SketchEditor::hideSessionPreview()
{
    if (m_previewPrs3d.IsNull())
        return;
    m_viewer->aisContext()->Remove(m_previewPrs3d, false);
    m_previewPrs3d.Nullify();
    m_viewer->update();
}

void SketchEditor::applySession(double value)
{
    if (m_session.kind == ValueSession::Kind::None)
        return;
    const std::optional<Feature> feature = featureFromSession(value);
    endValueSession(true);
    if (!feature) {
        emit message(tr("Couldn't record the picks"));
        syncValueSession();
        return;
    }
    // On failure the picks stay, so another value can be tried.
    if (!commitFeature(*feature))
        syncValueSession();
}

void SketchEditor::setBodyOperation(BodyOperation operation)
{
    if (m_bodyOperation == operation)
        return;
    m_bodyOperation = operation;
    emit bodyOperationChanged(operation);
}

void SketchEditor::extrudeRegions(const std::vector<RegionRef>& regions, double distance,
                                  std::optional<BodyOperation> operation)
{
    Feature f;
    f.kind = Feature::Kind::Extrude;
    f.value = distance;
    f.operation = operation.value_or(m_bodyOperation);
    for (const RegionRef& r : regions) {
        if (r.sketch < 0 || r.sketch >= sketchCount() || r.index >= static_cast<int>(m_slots[r.sketch].regions.size()))
            continue;
        if (const auto inside = insidePoint(m_slots[r.sketch].sketch, m_slots[r.sketch].regions[r.index]))
            f.regions.push_back({m_slots[r.sketch].id, *inside});
    }
    commitFeature(f);
}

void SketchEditor::selectRegion(const RegionRef& region)
{
    if (mode() != EditorMode::Model || isPicked({Hit::Kind::Region, kNoId, region}))
        return;
    togglePick({Hit::Kind::Region, kNoId, region});
    syncValueSession();
}

void SketchEditor::combineBodies(BooleanKind kind)
{
    Feature f;
    f.kind = Feature::Kind::Combine;
    f.boolean = kind;
    for (int body : selectedBodies()) {
        if (const auto inside = insidePoint(m_bodies[body]))
            f.bodies.push_back(*inside);
    }
    endValueSession(true);
    commitFeature(f);
}

// --- Timeline and project -----------------------------------------------------------

std::vector<TimelineItem> SketchEditor::timeline() const
{
    std::vector<TimelineItem> items;
    std::map<QString, int> counts;
    auto mm = [](double v) { return QString::number(v, 'f', 2).remove(QRegularExpression(QStringLiteral("\\.?0+$"))); };
    for (std::size_t i = 0; i < m_features.size(); ++i) {
        const Feature& f = m_features[i];
        TimelineItem item;
        item.kind = f.kind;
        QString base;
        switch (f.kind) {
        case Feature::Kind::Sketch:
            base = tr("Sketch");
            item.sketch = true;
            break;
        case Feature::Kind::Extrude:
            base = tr("Extrude");
            item.detail = mm(f.value) + QStringLiteral(" mm");
            break;
        case Feature::Kind::Fillet:
            base = tr("Fillet");
            item.detail = QStringLiteral("r ") + mm(f.value) + QStringLiteral(" mm");
            break;
        case Feature::Kind::Chamfer:
            base = tr("Chamfer");
            item.detail = mm(f.value) + QStringLiteral(" mm");
            break;
        case Feature::Kind::Combine:
            base = f.boolean == BooleanKind::Union      ? tr("Union")
                 : f.boolean == BooleanKind::Subtract   ? tr("Subtract")
                                                        : tr("Intersect");
            break;
        case Feature::Kind::Delete:
            base = tr("Delete");
            break;
        case Feature::Kind::Shell:
            base = tr("Shell");
            item.detail = mm(f.value) + QStringLiteral(" mm");
            break;
        case Feature::Kind::Revolve:
            base = tr("Revolve");
            item.detail = mm(f.value) + QStringLiteral("°");
            break;
        case Feature::Kind::Sweep:
            base = tr("Sweep");
            break;
        case Feature::Kind::Loft:
            base = tr("Loft");
            break;
        case Feature::Kind::Mirror:
            base = tr("Mirror");
            break;
        case Feature::Kind::Pattern:
            base = f.pattern == Feature::PatternKind::Circular ? tr("Circular Pattern") : tr("Linear Pattern");
            item.detail = f.pattern == Feature::PatternKind::Circular
                            ? QStringLiteral("%1 × %2°").arg(f.count).arg(mm(f.value))
                            : QStringLiteral("%1 × %2 mm").arg(f.count).arg(mm(f.value));
            break;
        case Feature::Kind::Hole:
            base = tr("Hole");
            item.detail = QStringLiteral("⌀ %1 mm").arg(mm(f.hole.diameter));
            break;
        case Feature::Kind::Datum:
            if (f.datum == Feature::DatumKind::Axis || f.datum == Feature::DatumKind::TwoPointAxis) {
                base = tr("Axis");
            } else {
                base = tr("Plane");
                if (f.datum == Feature::DatumKind::OffsetPlane)
                    item.detail = mm(f.value) + QStringLiteral(" mm");
                else if (f.datum == Feature::DatumKind::AnglePlane)
                    item.detail = mm(f.value) + QStringLiteral("°");
            }
            break;
        }
        item.name = QStringLiteral("%1 %2").arg(base).arg(++counts[base]);
        if (i < m_featureErrors.size())
            item.error = modelText(m_featureErrors[i]);
        item.rolledBack = m_active >= 0 && m_rollback >= 0 && static_cast<int>(i) > m_rollback;
        items.push_back(item);
    }
    return items;
}

void SketchEditor::activateTimelineItem(int index)
{
    if (index < 0 || index >= static_cast<int>(m_features.size()))
        return;
    const Feature feature = m_features[index];
    if (feature.kind == Feature::Kind::Sketch) {
        for (int i = 0; i < sketchCount(); ++i) {
            if (m_slots[i].id == feature.sketch) {
                if (mode() == EditorMode::Sketch)
                    closeSketch();
                enterSketch(i, true);
                return;
            }
        }
        return;
    }
    if (feature.kind == Feature::Kind::Revolve || feature.kind == Feature::Kind::Pattern
        || feature.kind == Feature::Kind::Hole || feature.kind == Feature::Kind::Datum) {
        editFeature(index);
        return;
    }
    const bool extrusion = feature.kind == Feature::Kind::Extrude;
    if (!extrusion && feature.kind != Feature::Kind::Fillet && feature.kind != Feature::Kind::Chamfer
        && feature.kind != Feature::Kind::Shell)
        return; // nothing to tune
    if (mode() == EditorMode::Sketch)
        closeSketch();
    endValueSession(true);
    clearModelSelection();

    // Frame the model. Seen straight along the feature's direction (or exactly side-on)
    // a changing depth is invisible, so then turn to an isometric view.
    gp_Dir eye = m_viewer->eyeSide();
    std::optional<gp_Dir> along;
    if (!feature.regions.empty()) {
        if (const Sketch* s = sketchLookup()(feature.regions.front().sketch))
            along = s->plane().Direction();
    } else if (!feature.faces.empty()) {
        along = feature.faces.front().normal;
    }
    if (along) {
        const double c = std::abs(eye.Dot(*along));
        if (c > 0.97 || c < 0.15)
            eye = gp_Dir(1, -1, 1);
    }
    const gp_Dir up = std::abs(eye.Dot(gp::DZ())) > 0.99 ? gp::DY() : gp::DZ();
    m_viewer->animateCamera(eye, up, bodiesBox());

    // The whole model follows the value (and for an extrusion, the operation) as it is
    // changed; Esc puts it back.
    auto operation = std::make_shared<BodyOperation>(feature.operation);
    auto typed = std::make_shared<double>(feature.value);
    auto preview = [this, index, operation, typed](double value) {
        *typed = value;
        std::vector<Feature> features = m_features;
        features[index].value = value;
        features[index].operation = *operation;
        RebuildResult result = rebuild(sketchLookup(), features);
        m_bodies = std::move(result.bodies);
        refreshBodies();
        m_viewer->update();
    };
    auto label = [extrusion, kind = feature.kind] {
        return extrusion                    ? tr("Distance [mm]")
             : kind == Feature::Kind::Fillet ? tr("Radius [mm]")
             : kind == Feature::Kind::Shell  ? tr("Wall thickness [mm]")
                                             : tr("Chamfer [mm]");
    };
    m_valueEditor->open(label, feature.value, extrusion ? -1e5 : 1e-3, 1e5,
                        QPointF(m_viewer->width() / 2.0, m_viewer->height() - 60.0),
                        [this, index, operation](double value) {
                            pushUndo();
                            m_features[index].value = value;
                            m_features[index].operation = *operation;
                            rebuildModel();
                            emit historyChanged();
                        },
                        [this] { rebuildModel(); }, preview);
    if (extrusion) {
        m_valueEditor->setChoices([] { return operationLabels(std::nullopt); }, operationIndex(*operation),
                                  [operation, typed, preview](int choice) {
                                      *operation = kOperations[choice];
                                      preview(*typed);
                                  });
    }
}

void SketchEditor::removeTimelineItem(int index)
{
    if (index < 0 || index >= static_cast<int>(m_features.size()))
        return;
    if (mode() == EditorMode::Sketch)
        closeSketch();
    endValueSession(true);
    clearModelSelection();
    pushUndo();
    const Feature removed = m_features[index];
    m_features.erase(m_features.begin() + index);
    if (removed.kind == Feature::Kind::Sketch) {
        // The sketch goes with its entry; features that used it will report it missing.
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].id == removed.sketch) {
                m_viewer->aisContext()->Remove(m_slots[i].prs, false);
                m_slots.erase(m_slots.begin() + static_cast<long>(i));
                break;
            }
        }
        showRegions();
    }
    rebuildModel();
    emit historyChanged();
}

Bnd_Box SketchEditor::sketchBox(int slot) const
{
    Bnd_Box box;
    const Sketch& s = m_slots[slot].sketch;
    for (const SketchPoint& p : s.points())
        box.Add(s.to3d(p.pos));
    for (const SketchEntity& e : s.entities()) {
        for (const gp_Pnt2d& p : s.polyline(e))
            box.Add(s.to3d(p));
    }
    if (const auto& image = s.image()) {
        box.Add(s.to3d(image->corner));
        box.Add(s.to3d(gp_Pnt2d(image->corner.X() + image->width, image->corner.Y() + image->height())));
    }
    if (box.IsVoid() || box.SquareExtent() < 1.0) {
        // Empty or tiny: a 100 mm patch of the plane around it.
        const gp_Pnt2d middle = box.IsVoid() ? gp_Pnt2d(0, 0) : s.to2d(gp_Pnt((box.CornerMin().XYZ() + box.CornerMax().XYZ()) / 2.0));
        box.Add(s.to3d(gp_Pnt2d(middle.X() - 50.0, middle.Y() - 50.0)));
        box.Add(s.to3d(gp_Pnt2d(middle.X() + 50.0, middle.Y() + 50.0)));
    }
    return box;
}

Bnd_Box SketchEditor::bodiesBox() const
{
    Bnd_Box box;
    for (const TopoDS_Shape& body : m_bodies)
        BRepBndLib::Add(body, box);
    return box;
}

ProjectData SketchEditor::project() const
{
    ProjectData data;
    for (const Slot& slot : m_slots)
        data.sketches.push_back({slot.id, slot.sketch});
    data.features = m_features;
    return data;
}

void SketchEditor::loadProject(ProjectData project)
{
    resetInteraction();
    setSelection({});
    clearModelSelection();
    if (m_active >= 0) {
        m_active = -1;
        updateGrid();
        emit modeChanged(EditorMode::Model);
    }
    for (const Slot& slot : m_slots)
        m_viewer->aisContext()->Remove(slot.prs, false);
    m_slots.clear();
    m_nextSketchId = 1;
    Snapshot loaded{std::move(project.sketches), std::move(project.features)};
    restore(std::move(loaded));
    m_undo.clear();
    m_redo.clear();
    m_changes = m_savedChanges = 0;
    setTool(ToolKind::Select);
    emit historyChanged();
}

void SketchEditor::newProject()
{
    loadProject({});
}

// --- The extrude arrow --------------------------------------------------------------

std::optional<std::pair<QPointF, QPointF>> SketchEditor::arrowOnScreen() const
{
    if (m_session.kind == ValueSession::Kind::None || !m_session.arrow || m_viewer->view().IsNull())
        return std::nullopt;
    // From where the value puts it (top of the extrusion / depth of the round), pointing
    // the way bigger values go.
    const gp_Pnt tip = m_session.base.Translated(gp_Vec(m_session.normal) * m_session.value);
    const QPointF from = m_viewer->project(tip);
    const QPointF ahead = m_viewer->project(tip.Translated(gp_Vec(m_session.normal) * pixelsToModel(kArrowPx)));
    QPointF dir = ahead - from;
    const double len = std::hypot(dir.x(), dir.y());
    // Looking straight along the normal it has no length on screen: point it up.
    dir = len < 15.0 ? QPointF(0, -kArrowPx) : dir * (kArrowPx / len);
    return std::pair(from, from + dir);
}

bool SketchEditor::hitsArrow(const QPointF& pos) const
{
    const auto arrow = arrowOnScreen();
    return arrow && segmentDistance(arrow->first, arrow->second, pos) <= kArrowGrabPx * m_touchScale;
}

double SketchEditor::arrowParam(const Graphic3d_Vec2i& pixel) const
{
    // Position along the arrow's axis closest to the pointer's view ray.
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    m_viewer->view()->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    gp_Vec ray(dx, dy, dz);
    if (ray.Magnitude() < 1e-12)
        return 0.0;
    ray.Normalize();
    const gp_Vec axis(m_session.normal);
    const gp_Vec w0(gp_Pnt(x, y, z), m_session.base);
    const double b = axis.Dot(ray);
    const double denom = 1.0 - b * b;
    if (denom < 1e-4) {
        // Looking straight along the axis: up the screen pulls toward the viewer.
        const double up = -pixel.y() / m_viewer->devicePixelRatioF() * pixelsToModel(1.0);
        return b < 0.0 ? up : -up;
    }
    return (b * ray.Dot(w0) - axis.Dot(w0)) / denom;
}

void SketchEditor::paintArrow(QPainter& p) const
{
    const auto arrow = arrowOnScreen();
    if (!arrow)
        return;
    const auto [from, to] = *arrow;
    const QColor ink = m_colors.overlayAccent;
    p.setPen(QPen(ink, 3.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(from, to);

    const QPointF d = (to - from) / QLineF(from, to).length();
    const QPointF n(-d.y(), d.x());
    QPainterPath head;
    head.moveTo(to + d * 14.0);
    head.lineTo(to - d * 2.0 + n * 8.0);
    head.lineTo(to - d * 2.0 - n * 8.0);
    head.closeSubpath();
    p.fillPath(head, ink);

    // A dot where the arrow sits on the extrusion; the whole arrow can be grabbed.
    p.setPen(QPen(Qt::white, 2.0));
    p.setBrush(ink);
    p.drawEllipse(from, 6.0, 6.0);
}

// --- Projecting body edges into the open sketch ----------------------------------------

void SketchEditor::projectAt(const PointerEvent& e)
{
    const Hit hit = modelHitAt(e);
    if (hit.kind != Hit::Kind::Edge && hit.kind != Hit::Kind::Face) {
        emit message(tr("Tap an edge or a face of a body"));
        return;
    }
    const TopoDS_Shape shape = hit.shape;
    std::size_t added = 0;
    Sketch probe = sketch();
    added = projectEdges(probe, shape, false).size();
    if (added == 0) {
        emit message(tr("Already in the sketch (or seen edge-on)"));
        return;
    }
    commit([&](Sketch& s) { projectEdges(s, shape, false); });
    refreshOverlay();
    m_viewer->update();
}

// --- Reference image -------------------------------------------------------------------

bool SketchEditor::insertImage(const QString& path)
{
    if (m_active < 0)
        return false;
    QFile file(path);
    QByteArray bytes;
    if (file.open(QIODevice::ReadOnly))
        bytes = file.readAll();
    QImage picture;
    if (bytes.isEmpty() || !picture.loadFromData(bytes) || picture.width() <= 0 || picture.height() <= 0) {
        emit message(tr("Can't read the picture %1").arg(path));
        return false;
    }
    ReferenceImage image;
    image.data = std::make_shared<const std::string>(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    image.pixelWidth = picture.width();
    image.pixelHeight = picture.height();
    // About two thirds of the view across, centered where the view looks.
    const double dpr = m_viewer->devicePixelRatioF();
    const Graphic3d_Vec2i middle(qRound(m_viewer->width() * dpr / 2.0), qRound(m_viewer->height() * dpr / 2.0));
    const gp_Pnt2d center = toSketch(middle).value_or(gp_Pnt2d(0.0, 0.0));
    const double viewWidth = pixelsToModel(m_viewer->width() * 0.6);
    image.width = viewWidth > 1e-6 ? std::round(viewWidth) : 100.0;
    image.corner = gp_Pnt2d(center.X() - image.width / 2.0, center.Y() - image.height() / 2.0);
    commit([&](Sketch& s) { s.setImage(image); });
    setTool(ToolKind::Image);
    return true;
}

void SketchEditor::removeImage()
{
    if (!hasImage())
        return;
    commit([](Sketch& s) { s.setImage(std::nullopt); });
    if (m_toolKind == ToolKind::Image)
        setTool(ToolKind::Select);
    m_viewer->update();
}

void SketchEditor::setImageOpacity(double opacity)
{
    if (!hasImage())
        return;
    commit([opacity](Sketch& s) {
        ReferenceImage image = *s.image();
        image.opacity = std::clamp(opacity, 0.05, 1.0);
        s.setImage(image);
    });
    m_viewer->update();
}

void SketchEditor::refreshImage()
{
    const std::optional<ReferenceImage> wanted = m_active >= 0 ? sketch().image() : std::nullopt;
    const gp_Ax3 plane = m_active >= 0 ? sketch().plane() : gp_Ax3();
    const bool samePlane = plane.Location().IsEqual(m_shownImagePlane.Location(), 1e-9)
                        && plane.Direction().IsEqual(m_shownImagePlane.Direction(), 1e-12)
                        && plane.XDirection().IsEqual(m_shownImagePlane.XDirection(), 1e-12);
    if (wanted == m_shownImage && (samePlane || !wanted))
        return;
    const auto& ctx = m_viewer->aisContext();
    if (!m_imagePrs.IsNull()) {
        ctx->Remove(m_imagePrs, false);
        m_imagePrs.Nullify();
    }
    m_shownImage = wanted;
    m_shownImagePlane = plane;
    if (!wanted || !wanted->data)
        return;

    QImage picture;
    if (!picture.loadFromData(reinterpret_cast<const uchar*>(wanted->data->data()),
                              static_cast<int>(wanted->data->size())))
        return;
    // Plenty for tracing, and kind to the graphics memory.
    if (picture.width() > 4096 || picture.height() > 4096)
        picture = picture.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    picture = picture.convertToFormat(QImage::Format_RGBA8888);
    Handle(Image_PixMap) pixmap = new Image_PixMap();
    if (!pixmap->InitTrash(Image_Format_RGBA, picture.width(), picture.height()))
        return;
    // Rows bottom-up in memory: the texture's first row is the picture's bottom edge,
    // which lands on the face's lower edge (the corner).
    pixmap->SetTopDown(false);
    // The opacity goes into the texture's alpha: unmodulated, the texture replaces the
    // material's color and transparency alike.
    const int alpha = qRound(std::clamp(wanted->opacity, 0.0, 1.0) * 256.0);
    for (int row = 0; row < picture.height(); ++row) {
        uchar* to = pixmap->ChangeRow(row);
        std::memcpy(to, picture.constScanLine(row), static_cast<std::size_t>(picture.width()) * 4);
        for (int x = 0; x < picture.width(); ++x)
            to[4 * x + 3] = static_cast<uchar>(std::min(255, to[4 * x + 3] * alpha / 256));
    }

    const gp_Ax3 at(sketch().to3d(wanted->corner), plane.Direction(), plane.XDirection());
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(at), 0.0, wanted->width, 0.0, wanted->height()).Face();
    Handle(AIS_TexturedShape) prs = new AIS_TexturedShape(face);
    prs->SetTexturePixMap(pixmap);
    prs->SetTextureMapOn();
    prs->SetTextureRepeat(false);
    prs->DisableTextureModulate();
    prs->SetDisplayMode(3); // shaded with the texture
    prs->Attributes()->SetupOwnShadingAspect();
    prs->Attributes()->ShadingAspect()->Aspect()->SetAlphaMode(Graphic3d_AlphaMode_Blend);

    if (m_imageLayer < 0) {
        // Its own layer between the bodies and the sketch: depth-tested against the bodies
        // (pulled forward, so a body face it lies on doesn't cover it), but writing no
        // depth, so the sketch lines drawn after it always show.
        Graphic3d_ZLayerId layer = Graphic3d_ZLayerId_UNKNOWN;
        Graphic3d_ZLayerSettings settings;
        settings.SetName("reference image");
        settings.SetEnableDepthTest(true);
        settings.SetEnableDepthWrite(false);
        settings.SetClearDepth(false);
        Graphic3d_PolygonOffset offset;
        offset.Mode = Aspect_POM_Fill;
        offset.Factor = -1.0f;
        offset.Units = -2.0f;
        settings.SetPolygonOffset(offset);
        if (m_viewer->viewer()->InsertLayerBefore(layer, settings, Graphic3d_ZLayerId_Top))
            m_imageLayer = layer;
    }
    ctx->Display(prs, 3, -1, false);
    if (m_imageLayer >= 0)
        ctx->SetZLayer(prs, m_imageLayer);
    m_imagePrs = prs;
}
