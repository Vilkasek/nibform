// SketchEditor, model mode: solid features (revolve, sweep, loft, holes, mirror and
// patterns of bodies or features) and construction planes / axes. Each tool gathers its
// picks in m_solid; once complete, the values are asked in the floating field with a
// live preview, the same prompts that edit the feature later from the history.

#include "sketch/SketchEditor.h"

#include "model/Profiles.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Prs3d_Drawer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <V3d_View.hxx>
#include <gp_Pln.hxx>

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSnapPx = 12.0;

// In the order the value field offers them (as for extrusions).
constexpr BodyOperation kOperationChoices[] = {BodyOperation::Auto, BodyOperation::NewBody, BodyOperation::Join,
                                               BodyOperation::Cut};

int operationChoice(BodyOperation operation)
{
    return static_cast<int>(std::find(std::begin(kOperationChoices), std::end(kOperationChoices), operation)
                            - std::begin(kOperationChoices));
}

// What the next feature of a kind starts with: the last values used.
double s_revolveAngle = 360.0;
int s_linearCount = 3;
double s_linearSpacing = 20.0;
int s_circularCount = 6;
double s_circularAngle = 360.0;
HoleSpec s_hole;
double s_planeOffset = 10.0;
double s_planeAngle = 45.0;

TopoDS_Compound compound()
{
    TopoDS_Compound c;
    BRep_Builder().MakeCompound(c);
    return c;
}

// A datum as something to see and pick: a square for a plane, a segment for an axis.
TopoDS_Shape datumShape(const Datum& d, double size)
{
    if (d.plane)
        return BRepBuilderAPI_MakeFace(gp_Pln(d.frame), -size, size, -size, size).Shape();
    const gp_Vec along(d.frame.Direction());
    return BRepBuilderAPI_MakeEdge(d.frame.Location().Translated(along * -size),
                                   d.frame.Location().Translated(along * size))
        .Shape();
}

} // namespace

// --- Datums -------------------------------------------------------------------------

std::optional<Datum> SketchEditor::datumById(int id) const
{
    if (id < 0)
        return baseDatum(id);
    if (const Datum* d = m_context.datum(id))
        return *d;
    return std::nullopt;
}

bool SketchEditor::wantsBaseDatums() const
{
    if (mode() != EditorMode::Model)
        return false;
    switch (m_toolKind) {
    case ToolKind::Revolve:
    case ToolKind::BodyMirror:
    case ToolKind::BodyLinearPattern:
    case ToolKind::BodyCircularPattern:
    case ToolKind::OffsetPlane:
    case ToolKind::AnglePlane:
        return true;
    default:
        return false;
    }
}

void SketchEditor::showDatums()
{
    const auto& ctx = m_viewer->aisContext();
    for (const auto& [prs, id] : m_datumPrs)
        ctx->Remove(prs, false);
    m_datumPrs.clear();
    if (mode() != EditorMode::Model)
        return;

    const Bnd_Box box = bodiesBox();
    const double size = box.IsVoid() ? 30.0 : std::max(20.0, 0.35 * std::sqrt(box.SquareExtent()));
    std::vector<Datum> shown = m_context.datums;
    if (wantsBaseDatums()) {
        for (int id : {kBaseXY, kBaseXZ, kBaseYZ, kBaseX, kBaseY, kBaseZ})
            shown.push_back(*baseDatum(id));
    }
    for (const Datum& d : shown) {
        const bool picked = d.id == m_selectedDatum
                         || (m_solid.axis && m_solid.axis->kind == AxisRef::Kind::Datum && m_solid.axis->datum == d.id)
                         || (m_solid.plane && m_solid.plane->kind == PlaneRef::Kind::Datum && m_solid.plane->datum == d.id);
        Handle(AIS_Shape) prs = new AIS_Shape(datumShape(d, d.id < 0 ? 0.8 * size : size));
        prs->Attributes()->SetFaceBoundaryDraw(true);
        prs->SetColor(picked ? m_colors.accent : d.id < 0 ? m_colors.inactive : m_colors.preview);
        if (d.plane)
            prs->SetTransparency(picked ? 0.6 : 0.82);
        else
            prs->SetWidth(picked ? 3.0 : 2.0);
        ctx->Display(prs, AIS_Shaded, 0, false); // pickable as a whole
        m_datumPrs.emplace_back(prs, d.id);
    }
}

// --- Picks --------------------------------------------------------------------------

std::optional<AxisRef> SketchEditor::axisOf(const Hit& hit) const
{
    AxisRef ref;
    if (hit.kind == Hit::Kind::Edge) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(hit.shape));
        if (curve.GetType() != GeomAbs_Line && curve.GetType() != GeomAbs_Circle)
            return std::nullopt;
        ref.kind = AxisRef::Kind::Edge;
        ref.edge = edgePick(TopoDS::Edge(hit.shape));
        return ref;
    }
    if (hit.kind == Hit::Kind::Datum) {
        const auto d = datumById(hit.id);
        if (!d || d->plane)
            return std::nullopt;
        ref.kind = AxisRef::Kind::Datum;
        ref.datum = hit.id;
        return ref;
    }
    if (hit.kind == Hit::Kind::SketchCurve) {
        const Slot& slot = m_slots[hit.region.sketch];
        const SketchEntity* e = slot.sketch.entity(hit.id);
        if (!e || e->kind != EntityKind::Line)
            return std::nullopt;
        ref.kind = AxisRef::Kind::SketchLine;
        ref.sketch = slot.id;
        ref.entity = hit.id;
        return ref;
    }
    return std::nullopt;
}

std::optional<PlaneRef> SketchEditor::planeOf(const Hit& hit) const
{
    PlaneRef ref;
    if (hit.kind == Hit::Kind::Face && planarNormal(TopoDS::Face(hit.shape))) {
        ref.kind = PlaneRef::Kind::Face;
        ref.face = facePick(TopoDS::Face(hit.shape));
        return ref;
    }
    if (hit.kind == Hit::Kind::Datum) {
        const auto d = datumById(hit.id);
        if (!d || !d->plane)
            return std::nullopt;
        ref.kind = PlaneRef::Kind::Datum;
        ref.datum = hit.id;
        return ref;
    }
    return std::nullopt;
}

void SketchEditor::placeHole(const Hit& hit, const QPointF& tap)
{
    const TopoDS_Face face = TopoDS::Face(hit.shape);
    const auto normal = planarNormal(face);
    if (!normal) {
        emit message(tr("Hole: pick a flat face"));
        return;
    }
    const FacePick pick = facePick(face);
    const gp_Pnt origin = pick.middle;
    // Another face starts over.
    if (!m_solid.face || m_solid.face->normal.Dot(*normal) < 0.9999
        || std::abs(gp_Vec(m_solid.face->middle, origin).Dot(gp_Vec(*normal))) > 1e-6) {
        m_solid.face = pick;
        m_solid.points.clear();
        m_solid.holeAt.clear();
    }

    // Snapping: a sketch point on the face's plane, else the center of a round edge.
    const double reach = kSnapPx * m_touchScale;
    double best = reach;
    std::optional<SketchPointRef> onPoint;
    std::optional<gp_Pnt> onCenter;
    for (const Slot& slot : m_slots) {
        const gp_Ax3& plane = slot.sketch.plane();
        if (std::abs(plane.Direction().Dot(*normal)) < 0.9999
            || std::abs(gp_Vec(origin, plane.Location()).Dot(gp_Vec(*normal))) > 1e-6)
            continue;
        for (const SketchPoint& p : slot.sketch.points()) {
            const double d = QLineF(m_viewer->project(slot.sketch.to3d(p.pos)), tap).length();
            if (d < best) {
                best = d;
                onPoint = SketchPointRef{slot.id, p.id};
                onCenter.reset();
            }
        }
    }
    for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if (curve.GetType() != GeomAbs_Circle)
            continue;
        const gp_Pnt center = curve.Circle().Location();
        const double d = QLineF(m_viewer->project(center), tap).length();
        if (d < best) {
            best = d;
            onCenter = center;
            onPoint.reset();
        }
    }
    if (onPoint) {
        const bool known = std::any_of(m_solid.holeAt.begin(), m_solid.holeAt.end(), [&](const SketchPointRef& r) {
            return r.sketch == onPoint->sketch && r.point == onPoint->point;
        });
        if (!known)
            m_solid.holeAt.push_back(*onPoint);
        return;
    }
    if (onCenter) {
        m_solid.points.push_back(*onCenter);
        return;
    }
    // Right where the pointer meets the face's plane.
    const Handle(V3d_View)& view = m_viewer->view();
    const qreal dpr = m_viewer->devicePixelRatioF();
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(qRound(tap.x() * dpr), qRound(tap.y() * dpr), x, y, z, dx, dy, dz);
    const gp_Vec ray(dx, dy, dz);
    const double denom = ray.Dot(gp_Vec(*normal));
    if (std::abs(denom) < 1e-9)
        return;
    const gp_Pnt eye(x, y, z);
    m_solid.points.push_back(eye.Translated(ray * (gp_Vec(eye, origin).Dot(gp_Vec(*normal)) / denom)));
}

void SketchEditor::solidTap(const Hit& hit)
{
    auto toggleRegion = [this](const RegionRef& r) {
        const auto it = std::find(m_solid.regions.begin(), m_solid.regions.end(), r);
        if (it != m_solid.regions.end()) {
            m_solid.regions.erase(it);
            m_regionSelection.erase(r);
        } else {
            m_solid.regions.push_back(r);
            m_regionSelection.insert(r);
        }
        showRegions();
    };
    auto toggleBody = [this](int body) {
        const auto it = std::find(m_solid.bodies.begin(), m_solid.bodies.end(), body);
        if (it != m_solid.bodies.end())
            m_solid.bodies.erase(it);
        else
            m_solid.bodies.push_back(body);
    };
    const bool onBody = hit.kind == Hit::Kind::Face || hit.kind == Hit::Kind::Edge || hit.kind == Hit::Kind::Vertex;
    bool ready = false;

    switch (m_toolKind) {
    case ToolKind::Revolve:
        if (hit.kind == Hit::Kind::Region)
            toggleRegion(hit.region);
        else if (const auto axis = axisOf(hit))
            m_solid.axis = axis;
        ready = !m_solid.regions.empty() && m_solid.axis;
        break;
    case ToolKind::Sweep:
        if (hit.kind == Hit::Kind::Region) {
            toggleRegion(hit.region);
        } else if (hit.kind == Hit::Kind::SketchCurve) {
            m_solid.pathSketch = m_slots[hit.region.sketch].id;
            m_solid.pathEntity = hit.id;
        }
        ready = !m_solid.regions.empty() && m_solid.pathEntity >= 0;
        break;
    case ToolKind::Loft:
        if (hit.kind == Hit::Kind::Region)
            toggleRegion(hit.region);
        break;
    case ToolKind::Hole:
        if (hit.kind == Hit::Kind::Face)
            placeHole(hit, m_tapPos);
        ready = !m_solid.points.empty() || !m_solid.holeAt.empty();
        break;
    case ToolKind::BodyMirror:
        if (!m_solid.reference && onBody) {
            toggleBody(hit.id);
        } else if (const auto plane = planeOf(hit); plane && (m_solid.reference || hit.kind == Hit::Kind::Datum)) {
            m_solid.plane = plane;
        }
        ready = !m_solid.bodies.empty() && m_solid.plane;
        break;
    case ToolKind::BodyLinearPattern:
    case ToolKind::BodyCircularPattern:
        // Faces pick bodies, edges (and datum axes, sketch lines) the direction.
        if (const auto axis = axisOf(hit))
            m_solid.axis = axis;
        else if ((hit.kind == Hit::Kind::Face || hit.kind == Hit::Kind::Vertex) && m_solid.source == 0)
            toggleBody(hit.id);
        ready = (m_solid.source != 0 || !m_solid.bodies.empty()) && m_solid.axis;
        break;
    case ToolKind::OffsetPlane:
        if (const auto plane = planeOf(hit))
            m_solid.plane = plane;
        ready = m_solid.plane.has_value();
        break;
    case ToolKind::AnglePlane:
        if (const auto axis = axisOf(hit))
            m_solid.axis = axis;
        else if (const auto plane = planeOf(hit))
            m_solid.plane = plane;
        ready = m_solid.plane && m_solid.axis;
        break;
    case ToolKind::ThreePointPlane:
        if (hit.kind == Hit::Kind::Vertex)
            m_solid.points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(hit.shape)));
        ready = m_solid.points.size() >= 3;
        break;
    case ToolKind::Axis:
        if (hit.kind == Hit::Kind::Edge) {
            if (const auto axis = axisOf(hit)) {
                m_solid.axis = axis;
                m_solid.points.clear();
            }
        } else if (hit.kind == Hit::Kind::Vertex) {
            m_solid.points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(hit.shape)));
        }
        ready = m_solid.axis || m_solid.points.size() >= 2;
        break;
    default:
        return;
    }
    refreshSolidMarks();
    if (ready)
        solidReady();
    updateHint();
    m_viewer->update();
}

void SketchEditor::refreshSolidMarks()
{
    const auto& ctx = m_viewer->aisContext();
    // Picked bodies in the accent color.
    for (int i = 0; i < static_cast<int>(m_bodyPrs.size()); ++i) {
        const bool picked = std::find(m_solid.bodies.begin(), m_solid.bodies.end(), i) != m_solid.bodies.end();
        ctx->SetColor(m_bodyPrs[i], picked ? m_colors.accent : m_colors.body, false);
    }
    // The axis, path and points picked so far.
    TopoDS_Compound marks = compound();
    bool any = false;
    auto add = [&](const TopoDS_Shape& shape) {
        if (!shape.IsNull()) {
            BRep_Builder().Add(marks, shape);
            any = true;
        }
    };
    if (m_solid.axis) {
        if (m_solid.axis->kind == AxisRef::Kind::Edge) {
            if (const auto edge = findEdge(m_bodies, m_solid.axis->edge))
                add(edge->edge);
        } else if (m_solid.axis->kind == AxisRef::Kind::SketchLine) {
            if (const auto axis = resolveAxis(sketchLookup(), m_bodies, *m_solid.axis, m_context)) {
                const Sketch* s = sketchLookup()(m_solid.axis->sketch);
                const SketchEntity* line = s ? s->entity(m_solid.axis->entity) : nullptr;
                if (line)
                    add(BRepBuilderAPI_MakeEdge(s->to3d(s->pos(line->a)), s->to3d(s->pos(line->b))).Shape());
            }
        }
    }
    if (m_solid.pathEntity >= 0) {
        if (const Sketch* s = sketchLookup()(m_solid.pathSketch)) {
            if (const auto path = sketchPath(*s, m_solid.pathEntity))
                add(*path);
        }
    }
    if (m_solid.plane && m_solid.plane->kind == PlaneRef::Kind::Face) {
        if (const auto face = findFace(m_bodies, m_solid.plane->face))
            add(face->first);
    }
    for (const gp_Pnt& p : m_solid.points)
        add(BRepBuilderAPI_MakeVertex(p).Shape());
    for (const SketchPointRef& r : m_solid.holeAt) {
        if (const Sketch* s = sketchLookup()(r.sketch); s && s->point(r.point))
            add(BRepBuilderAPI_MakeVertex(s->to3d(s->pos(r.point))).Shape());
    }

    if (!m_solidMarksPrs.IsNull())
        ctx->Remove(m_solidMarksPrs, false);
    m_solidMarksPrs.Nullify();
    if (any) {
        m_solidMarksPrs = new AIS_Shape(marks);
        m_solidMarksPrs->SetColor(m_colors.accent);
        m_solidMarksPrs->SetWidth(3.5);
        m_solidMarksPrs->Attributes()->SetFaceBoundaryDraw(false);
        m_solidMarksPrs->SetTransparency(0.5);
        m_solidMarksPrs->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ctx->Display(m_solidMarksPrs, AIS_Shaded, -1, false);
    }
    showDatums(); // a picked datum lights up
}

void SketchEditor::resetSolid()
{
    m_solid = {};
    m_solidError.clear();
    if (!m_regionSelection.empty() && isSolidTool(m_toolKind)) {
        m_regionSelection.clear();
        showRegions();
    }
    refreshSolidMarks();
}

bool SketchEditor::solidConfirm()
{
    if (mode() != EditorMode::Model || !isSolidTool(m_toolKind))
        return false;
    if (m_toolKind == ToolKind::Loft && m_solid.regions.size() >= 2) {
        solidReady();
        return true;
    }
    if (m_toolKind == ToolKind::BodyMirror && !m_solid.reference && !m_solid.bodies.empty()) {
        m_solid.reference = true;
        updateHint();
        return true;
    }
    if (m_toolKind == ToolKind::Hole && (!m_solid.points.empty() || !m_solid.holeAt.empty())) {
        solidReady();
        return true;
    }
    return false;
}

bool SketchEditor::solidCancel()
{
    if (mode() != EditorMode::Model || !isSolidTool(m_toolKind))
        return false;
    const bool any = !m_solid.regions.empty() || m_solid.axis || m_solid.plane || m_solid.pathEntity >= 0
                  || !m_solid.bodies.empty() || m_solid.source != 0 || !m_solid.points.empty()
                  || !m_solid.holeAt.empty() || m_solid.face;
    if (!any)
        return false;
    hideSessionPreview();
    resetSolid();
    updateHint();
    m_viewer->update();
    return true;
}

// --- Features -----------------------------------------------------------------------

int SketchEditor::nextFeatureId() const
{
    int highest = 0;
    for (const Feature& f : m_features)
        highest = std::max(highest, f.id);
    return highest + 1;
}

std::optional<Feature> SketchEditor::solidFeature() const
{
    Feature f;
    f.operation = m_bodyOperation;
    for (const RegionRef& r : m_solid.regions) {
        const Slot& slot = m_slots[r.sketch];
        if (r.index >= static_cast<int>(slot.regions.size()))
            return std::nullopt;
        const auto inside = insidePoint(slot.sketch, slot.regions[r.index]);
        if (!inside)
            return std::nullopt;
        f.regions.push_back({slot.id, *inside});
    }
    for (int b : m_solid.bodies) {
        if (b < 0 || b >= bodyCount())
            return std::nullopt;
        if (const auto inside = insidePoint(m_bodies[b]))
            f.bodies.push_back(*inside);
    }
    switch (m_toolKind) {
    case ToolKind::Revolve:
        f.kind = Feature::Kind::Revolve;
        f.axis = *m_solid.axis;
        f.value = s_revolveAngle;
        break;
    case ToolKind::Sweep:
        f.kind = Feature::Kind::Sweep;
        f.pathSketch = m_solid.pathSketch;
        f.pathEntity = m_solid.pathEntity;
        break;
    case ToolKind::Loft:
        f.kind = Feature::Kind::Loft;
        break;
    case ToolKind::Hole:
        f.kind = Feature::Kind::Hole;
        f.faces = {*m_solid.face};
        f.points = m_solid.points;
        f.holeAt = m_solid.holeAt;
        f.hole = s_hole;
        break;
    case ToolKind::BodyMirror:
        f.kind = Feature::Kind::Mirror;
        f.plane = *m_solid.plane;
        f.operation = BodyOperation::Auto; // joins the half it was mirrored from
        break;
    case ToolKind::BodyLinearPattern:
    case ToolKind::BodyCircularPattern: {
        const bool circular = m_toolKind == ToolKind::BodyCircularPattern;
        f.kind = Feature::Kind::Pattern;
        f.pattern = circular ? Feature::PatternKind::Circular : Feature::PatternKind::Linear;
        f.axis = *m_solid.axis;
        f.source = m_solid.source;
        f.count = circular ? s_circularCount : s_linearCount;
        f.value = circular ? s_circularAngle : s_linearSpacing;
        f.operation = m_solid.source != 0 ? BodyOperation::Auto : BodyOperation::NewBody;
        if (m_solid.source != 0)
            f.bodies.clear();
        break;
    }
    case ToolKind::OffsetPlane:
        f.kind = Feature::Kind::Datum;
        f.datum = Feature::DatumKind::OffsetPlane;
        f.plane = *m_solid.plane;
        f.value = s_planeOffset;
        break;
    case ToolKind::AnglePlane:
        f.kind = Feature::Kind::Datum;
        f.datum = Feature::DatumKind::AnglePlane;
        f.plane = *m_solid.plane;
        f.axis = *m_solid.axis;
        f.value = s_planeAngle;
        break;
    case ToolKind::ThreePointPlane:
        f.kind = Feature::Kind::Datum;
        f.datum = Feature::DatumKind::ThreePointPlane;
        f.points = m_solid.points;
        break;
    case ToolKind::Axis:
        f.kind = Feature::Kind::Datum;
        if (m_solid.axis) {
            f.datum = Feature::DatumKind::Axis;
            f.axis = *m_solid.axis;
        } else {
            f.datum = Feature::DatumKind::TwoPointAxis;
            f.points = m_solid.points;
        }
        break;
    default:
        return std::nullopt;
    }
    return f;
}

std::vector<SketchEditor::FeaturePrompt> SketchEditor::promptsFor(const Feature& feature) const
{
    std::vector<FeaturePrompt> prompts;
    auto operations = [](FeaturePrompt& p) {
        p.choices = [] { return operationLabels(std::nullopt); };
        p.choice = [](const Feature& f) { return operationChoice(f.operation); };
        p.choose = [](Feature& f, int i) { f.operation = kOperationChoices[i]; };
    };
    switch (feature.kind) {
    case Feature::Kind::Revolve: {
        FeaturePrompt angle;
        angle.label = [] { return tr("Angle [°]"); };
        angle.min = -360.0;
        angle.max = 360.0;
        angle.get = [](const Feature& f) { return f.value; };
        angle.set = [](Feature& f, double v) { f.value = v; };
        operations(angle);
        prompts.push_back(angle);
        break;
    }
    case Feature::Kind::Pattern: {
        FeaturePrompt count;
        count.label = [] { return tr("Count (with the original)"); };
        count.min = 2;
        count.max = 500;
        count.get = [](const Feature& f) { return f.count; };
        count.set = [](Feature& f, double v) { f.count = static_cast<int>(std::lround(v)); };
        prompts.push_back(count);
        FeaturePrompt step;
        const bool circular = feature.pattern == Feature::PatternKind::Circular;
        step.label = circular ? std::function<QString()>([] { return tr("Over [°] (360: a full turn)"); })
                              : std::function<QString()>([] { return tr("Spacing [mm]"); });
        step.min = circular ? 1.0 : -1e5;
        step.max = circular ? 360.0 : 1e5;
        step.get = [](const Feature& f) { return f.value; };
        step.set = [](Feature& f, double v) { f.value = v; };
        prompts.push_back(step);
        break;
    }
    case Feature::Kind::Hole: {
        FeaturePrompt diameter;
        diameter.label = [] { return tr("Hole diameter [mm]"); };
        diameter.min = 0.01;
        diameter.get = [](const Feature& f) { return f.hole.diameter; };
        diameter.set = [](Feature& f, double v) { f.hole.diameter = v; };
        diameter.choices = [] { return QStringList{tr("Through"), tr("Blind"), tr("Counterbore"), tr("Countersink")}; };
        diameter.choice = [](const Feature& f) {
            return f.hole.kind == HoleKind::Counterbore ? 2 : f.hole.kind == HoleKind::Countersink ? 3 : f.hole.depth > 0.0 ? 1 : 0;
        };
        diameter.choose = [](Feature& f, int i) {
            HoleSpec& h = f.hole;
            h.kind = i == 2 ? HoleKind::Counterbore : i == 3 ? HoleKind::Countersink : HoleKind::Simple;
            if (i == 0)
                h.depth = 0.0;
            else if (i == 1 && h.depth <= 0.0)
                h.depth = std::round(h.diameter * 20.0) / 10.0;
            if (i >= 2 && h.headDiameter <= h.diameter)
                h.headDiameter = std::round(h.diameter * (i == 2 ? 18.0 : 20.0)) / 10.0;
            if (i == 2 && h.headDepth <= 0.0)
                h.headDepth = std::round(h.diameter * 6.0) / 10.0;
        };
        prompts.push_back(diameter);
        FeaturePrompt depth;
        depth.label = [] { return tr("Depth [mm]"); };
        depth.min = 0.01;
        depth.get = [](const Feature& f) { return f.hole.depth; };
        depth.set = [](Feature& f, double v) { f.hole.depth = v; };
        depth.applies = [](const Feature& f) { return f.hole.depth > 0.0; };
        prompts.push_back(depth);
        FeaturePrompt head;
        head.label = [] { return tr("Head diameter [mm]"); };
        head.min = 0.01;
        head.get = [](const Feature& f) { return f.hole.headDiameter; };
        head.set = [](Feature& f, double v) { f.hole.headDiameter = v; };
        head.applies = [](const Feature& f) { return f.hole.kind != HoleKind::Simple; };
        prompts.push_back(head);
        FeaturePrompt headDepth;
        headDepth.label = [] { return tr("Counterbore depth [mm]"); };
        headDepth.min = 0.01;
        headDepth.get = [](const Feature& f) { return f.hole.headDepth; };
        headDepth.set = [](Feature& f, double v) { f.hole.headDepth = v; };
        headDepth.applies = [](const Feature& f) { return f.hole.kind == HoleKind::Counterbore; };
        prompts.push_back(headDepth);
        break;
    }
    case Feature::Kind::Datum:
        if (feature.datum == Feature::DatumKind::OffsetPlane || feature.datum == Feature::DatumKind::AnglePlane) {
            const bool offset = feature.datum == Feature::DatumKind::OffsetPlane;
            FeaturePrompt value;
            value.label = offset ? std::function<QString()>([] { return tr("Offset [mm]"); })
                                 : std::function<QString()>([] { return tr("Angle [°]"); });
            value.min = offset ? -1e5 : -360.0;
            value.max = offset ? 1e5 : 360.0;
            value.get = [](const Feature& f) { return f.value; };
            value.set = [](Feature& f, double v) { f.value = v; };
            prompts.push_back(value);
        }
        break;
    default:
        break;
    }
    return prompts;
}

void SketchEditor::configure(std::shared_ptr<Feature> draft, std::vector<FeaturePrompt> prompts, std::size_t index,
                             std::function<void(const Feature&)> preview, std::function<void(const Feature&)> done,
                             std::function<void()> cancelled)
{
    while (index < prompts.size() && prompts[index].applies && !prompts[index].applies(*draft))
        ++index;
    if (index >= prompts.size()) {
        done(*draft);
        return;
    }
    const FeaturePrompt prompt = prompts[index];
    preview(*draft);
    const QRectF view(0, 0, m_viewer->width(), m_viewer->height());
    const QPointF anchor = view.adjusted(20, 80, -20, -20).contains(m_pointerPos)
                             ? m_pointerPos - QPointF(0, 60)
                             : QPointF(view.width() / 2.0, view.height() - 60.0);
    m_valueEditor->open(
        prompt.label, prompt.get(*draft), prompt.min, prompt.max, anchor,
        [=, this](double value) {
            prompt.set(*draft, value);
            configure(draft, prompts, index + 1, preview, done, cancelled);
        },
        cancelled,
        [=](double value) {
            prompt.set(*draft, value);
            preview(*draft);
        });
    if (prompt.choices) {
        m_valueEditor->setChoices(prompt.choices, prompt.choice(*draft), [=](int choice) {
            prompt.choose(*draft, choice);
            preview(*draft);
        });
    }
    updateHint();
}

void SketchEditor::previewNewFeature(const Feature& feature)
{
    const auto& ctx = m_viewer->aisContext();
    const FeatureResult result = applyFeature(sketchLookup(), m_bodies, feature, m_context);
    m_solidError = result.error;
    TopoDS_Compound shapes = compound();
    bool any = false;
    const bool bodies = result.ok() && !result.datum;
    if (result.ok() && result.datum) {
        const Bnd_Box box = bodiesBox();
        BRep_Builder().Add(shapes, datumShape(*result.datum, box.IsVoid() ? 30.0 : std::max(20.0, 0.35 * std::sqrt(box.SquareExtent()))));
        any = true;
    } else if (bodies) {
        for (const TopoDS_Shape& b : result.bodies)
            BRep_Builder().Add(shapes, b);
        any = !result.bodies.empty();
    }
    // The bodies as they would be, over their faded originals.
    for (const Handle(AIS_Shape)& prs : m_bodyPrs) {
        if (bodies)
            ctx->SetTransparency(prs, 0.85, false);
        else
            ctx->UnsetTransparency(prs, false);
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
        ctx->SetColor(m_previewPrs3d, bodies ? m_colors.body : m_colors.accent, false);
        ctx->SetTransparency(m_previewPrs3d, bodies ? 0.0 : 0.6, false);
    }
    updateHint();
    m_viewer->update();
}

void SketchEditor::solidReady()
{
    const std::optional<Feature> made = solidFeature();
    if (!made) {
        emit message(tr("Couldn't record the picks"));
        return;
    }
    auto finish = [this](const Feature& f) {
        hideSessionPreview();
        for (const Handle(AIS_Shape)& prs : m_bodyPrs)
            m_viewer->aisContext()->UnsetTransparency(prs, false);
        // The next one starts from these values.
        switch (f.kind) {
        case Feature::Kind::Revolve:
            s_revolveAngle = f.value;
            break;
        case Feature::Kind::Hole:
            s_hole = f.hole;
            break;
        case Feature::Kind::Pattern:
            if (f.pattern == Feature::PatternKind::Circular) {
                s_circularCount = f.count;
                s_circularAngle = f.value;
            } else {
                s_linearCount = f.count;
                s_linearSpacing = f.value;
            }
            break;
        case Feature::Kind::Datum:
            if (f.datum == Feature::DatumKind::OffsetPlane)
                s_planeOffset = f.value;
            else if (f.datum == Feature::DatumKind::AnglePlane)
                s_planeAngle = f.value;
            break;
        default:
            break;
        }
        if (commitFeature(f))
            resetSolid();
        updateHint();
    };
    const std::vector<FeaturePrompt> prompts = promptsFor(*made);
    if (prompts.empty()) {
        finish(*made);
        return;
    }
    configure(std::make_shared<Feature>(*made), prompts, 0, [this](const Feature& f) { previewNewFeature(f); },
              finish, [this] {
                  // Dropped: the picks stay, to change them or try again.
                  hideSessionPreview();
                  for (const Handle(AIS_Shape)& prs : m_bodyPrs)
                      m_viewer->aisContext()->UnsetTransparency(prs, false);
                  m_solidError.clear();
                  updateHint();
                  m_viewer->update();
              });
}

void SketchEditor::editFeature(int index)
{
    const Feature original = m_features[index];
    const std::vector<FeaturePrompt> prompts = promptsFor(original);
    if (prompts.empty())
        return;
    if (mode() == EditorMode::Sketch)
        closeSketch();
    endValueSession(true);
    clearModelSelection();
    if (isSolidTool(m_toolKind))
        resetSolid();
    // The whole model follows the values as they change; Esc puts it back.
    auto preview = [this, index](const Feature& f) {
        std::vector<Feature> features = m_features;
        features[index] = f;
        RebuildResult result = rebuild(sketchLookup(), features);
        m_bodies = std::move(result.bodies);
        m_context = std::move(result.context);
        m_solidError = index < static_cast<int>(result.errors.size()) ? result.errors[index] : std::string();
        refreshBodies();
        showDatums();
        updateHint();
        m_viewer->update();
    };
    configure(std::make_shared<Feature>(original), prompts, 0, preview,
              [this, index](const Feature& f) {
                  m_solidError.clear();
                  pushUndo();
                  m_features[index] = f;
                  rebuildModel();
                  emit historyChanged();
              },
              [this] {
                  m_solidError.clear();
                  rebuildModel();
              });
}

bool SketchEditor::canPattern(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_features.size()))
        return false;
    const Feature& f = m_features[index];
    switch (f.kind) {
    case Feature::Kind::Revolve:
    case Feature::Kind::Sweep:
    case Feature::Kind::Loft:
    case Feature::Kind::Hole:
        return true;
    case Feature::Kind::Extrude:
        return !f.regions.empty() && f.faces.empty();
    default:
        return false;
    }
}

void SketchEditor::patternFeature(int index, bool circular)
{
    if (!canPattern(index))
        return;
    if (mode() == EditorMode::Sketch)
        closeSketch();
    const int source = m_features[index].id;
    setTool(circular ? ToolKind::BodyCircularPattern : ToolKind::BodyLinearPattern);
    m_solid.source = source;
    updateHint();
}

QString SketchEditor::solidHint() const
{
    const QString error = modelText(m_solidError);
    const bool asking = m_valueEditor->isOpen();
    auto with = [&](const QString& name, const QString& text) {
        return !error.isEmpty() && asking ? QStringLiteral("%1: %2").arg(name, error) : QStringLiteral("%1: %2").arg(name, text);
    };
    switch (m_toolKind) {
    case ToolKind::Revolve:
        if (asking)
            return with(tr("Revolve"), tr("type the angle  ·  Enter accepts"));
        if (m_solid.regions.empty())
            return tr("Revolve: tap the sketch regions to turn");
        return tr("Revolve: tap the axis: a sketch line, a straight or round edge, or an axis");
    case ToolKind::Sweep:
        if (m_solid.regions.empty())
            return tr("Sweep: tap the profile (a sketch region)");
        return tr("Sweep: tap the path: a curve of another sketch (its connected curves follow)");
    case ToolKind::Loft:
        return m_solid.regions.size() < 2 ? tr("Loft: tap the profiles in order (regions of sketches on different planes)")
                                          : tr("Loft: %1 profiles  ·  tap more, or Enter to loft").arg(m_solid.regions.size());
    case ToolKind::Hole:
        if (asking)
            return with(tr("Hole"), tr("type the size, pick the kind  ·  Enter accepts  ·  tap the face for more holes"));
        return tr("Hole: tap a flat face where it goes (snaps to sketch points and round edges' centers)");
    case ToolKind::BodyMirror:
        if (!m_solid.reference)
            return m_solid.bodies.empty() ? tr("Mirror: tap the bodies to mirror, then Enter (or tap a plane)")
                                          : tr("Mirror: %1 bodies  ·  Enter, then tap the mirror plane").arg(m_solid.bodies.size());
        return tr("Mirror: tap the mirror plane: a flat face or a plane");
    case ToolKind::BodyLinearPattern:
    case ToolKind::BodyCircularPattern: {
        const bool circular = m_toolKind == ToolKind::BodyCircularPattern;
        const QString name = circular ? tr("Circular pattern") : tr("Linear pattern");
        if (asking)
            return with(name, tr("type the count, then the %1  ·  Enter accepts").arg(circular ? tr("angle") : tr("spacing")));
        if (m_solid.source == 0 && m_solid.bodies.empty())
            return tr("%1: tap the bodies to repeat (or pick a feature in the history: Pattern…)").arg(name);
        return circular ? tr("%1: tap the axis: a round or straight edge, an axis or a sketch line").arg(name)
                        : tr("%1: tap the direction: a straight edge, an axis or a sketch line").arg(name);
    }
    case ToolKind::OffsetPlane:
        if (asking)
            return with(tr("Offset plane"), tr("type the distance  ·  Enter accepts"));
        return tr("Offset plane: tap a flat face or a plane");
    case ToolKind::AnglePlane:
        if (asking)
            return with(tr("Plane at angle"), tr("type the angle  ·  Enter accepts"));
        if (!m_solid.plane)
            return tr("Plane at angle: tap the plane (a flat face or a plane) and the axis to turn it about");
        return tr("Plane at angle: tap the axis to turn it about (an edge, an axis or a sketch line)");
    case ToolKind::ThreePointPlane:
        return tr("Plane through 3 points: tap corners of a body (%1 of 3)").arg(m_solid.points.size());
    case ToolKind::Axis:
        return m_solid.points.empty() ? tr("Axis: tap a straight or round edge, or two corners")
                                      : tr("Axis: tap the second corner");
    default:
        return {};
    }
}
