// SketchEditor, sheet metal (M18): base flanges from a sketch region or an open line,
// edge flanges and hems from an edge, tabs, corner reliefs, shelled bodies converted, and
// the flat patterns shown beside the model.

#include "sketch/SketchEditor.h"

#include "model/SheetMetal.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Prs3d_Drawer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>

namespace {

// What the next feature of a kind starts with: the last values used.
SheetSpec s_sheet{1.5, 1.5, 0.44};
double s_sheetDepth = 40.0;
double s_flangeLength = 20.0;
FlangeSpec s_flange;
bool s_flangeFlip = false;
double s_hemLength = 8.0;
double s_reliefSize = 4.0;

} // namespace

bool SketchEditor::sheetTap(const Hit& hit)
{
    auto pickRegion = [&] {
        m_solid.regions = {hit.region};
        m_regionSelection = {hit.region};
        m_solid.pathSketch = m_solid.pathEntity = -1;
        showRegions();
    };
    switch (m_toolKind) {
    case ToolKind::SheetBase:
        if (hit.kind == Hit::Kind::Region) {
            pickRegion();
        } else if (hit.kind == Hit::Kind::SketchCurve) {
            m_solid.regions.clear();
            m_regionSelection.clear();
            showRegions();
            m_solid.pathSketch = m_slots[hit.region.sketch].id;
            m_solid.pathEntity = hit.id;
        }
        return !m_solid.regions.empty() || m_solid.pathEntity >= 0;
    case ToolKind::Tab:
        if (hit.kind == Hit::Kind::Region)
            pickRegion();
        return !m_solid.regions.empty();
    case ToolKind::Flange:
    case ToolKind::Hem:
        if (hit.kind == Hit::Kind::Edge) {
            const TopoDS_Edge edge = TopoDS::Edge(hit.shape);
            if (BRepAdaptor_Curve(edge).GetType() != GeomAbs_Line) {
                emit message(tr("Tap a straight edge along an end of the sheet"));
                return false;
            }
            m_solid.edge = edgePick(edge);
        }
        return m_solid.edge.has_value();
    case ToolKind::CornerRelief:
        if (hit.kind == Hit::Kind::Vertex)
            m_solid.points = {BRep_Tool::Pnt(TopoDS::Vertex(hit.shape))};
        return !m_solid.points.empty();
    case ToolKind::SheetConvert:
        if (hit.kind == Hit::Kind::Face) {
            const TopoDS_Face face = TopoDS::Face(hit.shape);
            if (!planarNormal(face)) {
                emit message(tr("Convert: tap a flat side of a wall"));
                return false;
            }
            m_solid.face = facePick(face);
        }
        return m_solid.face.has_value();
    default:
        return false;
    }
}

bool SketchEditor::sheetFeature(Feature& f) const
{
    switch (m_toolKind) {
    case ToolKind::SheetBase:
        f.kind = Feature::Kind::SheetBase;
        f.sheet = s_sheet;
        if (m_solid.pathEntity >= 0) {
            f.regions.clear();
            f.pathSketch = m_solid.pathSketch;
            f.pathEntity = m_solid.pathEntity;
            f.value = s_sheetDepth;
        }
        return !f.regions.empty() || f.pathEntity >= 0;
    case ToolKind::Tab:
        f.kind = Feature::Kind::Tab;
        return !f.regions.empty();
    case ToolKind::Flange:
        if (!m_solid.edge)
            return false;
        f.kind = Feature::Kind::Flange;
        f.edges = {*m_solid.edge};
        f.value = s_flangeLength;
        f.flange = s_flange;
        f.flip = s_flangeFlip;
        return true;
    case ToolKind::Hem:
        if (!m_solid.edge)
            return false;
        f.kind = Feature::Kind::Hem;
        f.edges = {*m_solid.edge};
        f.value = s_hemLength;
        f.flange.gap = s_flange.gap;
        return true;
    case ToolKind::CornerRelief:
        if (m_solid.points.empty())
            return false;
        f.kind = Feature::Kind::Relief;
        f.points = m_solid.points;
        f.value = s_reliefSize;
        f.flange.square = s_flange.square;
        return true;
    case ToolKind::SheetConvert: {
        if (!m_solid.face)
            return false;
        f.kind = Feature::Kind::SheetConvert;
        f.faces = {*m_solid.face};
        f.sheet = s_sheet;
        // As thick as the walls are.
        if (const auto face = findFace(m_bodies, *m_solid.face)) {
            if (const auto t = wallThickness(m_bodies[face->second], face->first))
                f.sheet.thickness = std::round(*t * 1e6) / 1e6;
        }
        return true;
    }
    default:
        return false;
    }
}

std::vector<SketchEditor::FeaturePrompt> SketchEditor::sheetPrompts(const Feature& feature) const
{
    std::vector<FeaturePrompt> prompts;
    auto sides = [](FeaturePrompt& p) {
        p.choices = [] { return QStringList{tr("This side"), tr("The other side")}; };
        p.choice = [](const Feature& f) { return f.flip ? 1 : 0; };
        p.choose = [](Feature& f, int i) { f.flip = i == 1; };
    };
    // The sheet's rules, on what makes it.
    auto rules = [&](bool sideChoice) {
        FeaturePrompt thickness;
        thickness.label = [] { return tr("Sheet thickness [mm]"); };
        thickness.min = 0.01;
        thickness.max = 1e3;
        thickness.get = [](const Feature& f) { return f.sheet.thickness; };
        thickness.set = [](Feature& f, double v) { f.sheet.thickness = v; };
        thickness.field = "sheet.thickness";
        if (sideChoice)
            sides(thickness);
        prompts.push_back(thickness);
        FeaturePrompt radius;
        radius.label = [] { return tr("Bend radius, inside [mm]"); };
        radius.min = 0.0;
        radius.max = 1e3;
        radius.get = [](const Feature& f) { return f.sheet.radius; };
        radius.set = [](Feature& f, double v) { f.sheet.radius = v; };
        radius.field = "sheet.radius";
        prompts.push_back(radius);
        FeaturePrompt k;
        k.label = [] { return tr("K-factor (where the neutral layer lies: 0 inside … 1 outside)"); };
        k.min = 0.0;
        k.max = 1.0;
        k.get = [](const Feature& f) { return f.sheet.kFactor; };
        k.set = [](Feature& f, double v) { f.sheet.kFactor = v; };
        k.field = "sheet.kFactor";
        prompts.push_back(k);
    };
    switch (feature.kind) {
    case Feature::Kind::SheetBase:
        if (feature.regions.empty()) {
            FeaturePrompt depth;
            depth.label = [] { return tr("Depth [mm]"); };
            depth.get = [](const Feature& f) { return f.value; };
            depth.set = [](Feature& f, double v) { f.value = v; };
            depth.field = "value";
            prompts.push_back(depth);
        }
        rules(true);
        break;
    case Feature::Kind::SheetConvert:
        rules(false);
        break;
    case Feature::Kind::Flange: {
        FeaturePrompt length;
        length.label = [] { return tr("Flange length, outside [mm]"); };
        length.min = 0.01;
        length.get = [](const Feature& f) { return f.value; };
        length.set = [](Feature& f, double v) { f.value = v; };
        length.field = "value";
        length.choices = [] { return QStringList{tr("Bend outside"), tr("Material inside")}; };
        length.choice = [](const Feature& f) { return f.flange.inside ? 1 : 0; };
        length.choose = [](Feature& f, int i) { f.flange.inside = i == 1; };
        length.moreChoices = [] { return QStringList{tr("This way"), tr("The other way")}; };
        length.moreChoice = [](const Feature& f) { return f.flip ? 1 : 0; };
        length.chooseMore = [](Feature& f, int i) { f.flip = i == 1; };
        prompts.push_back(length);
        FeaturePrompt angle;
        angle.label = [] { return tr("Flange angle [°]"); };
        angle.min = 1.0;
        angle.max = 179.0;
        angle.get = [](const Feature& f) { return f.flange.angle; };
        angle.set = [](Feature& f, double v) { f.flange.angle = v; };
        angle.field = "flange.angle";
        prompts.push_back(angle);
        FeaturePrompt inset;
        inset.label = [] { return tr("Inset at both ends [mm] (material inside: with bend reliefs)"); };
        inset.min = 0.0;
        inset.get = [](const Feature& f) { return f.flange.inset; };
        inset.set = [](Feature& f, double v) { f.flange.inset = v; };
        inset.field = "flange.inset";
        prompts.push_back(inset);
        break;
    }
    case Feature::Kind::Hem: {
        FeaturePrompt length;
        length.label = [] { return tr("Hem length [mm]"); };
        length.min = 0.01;
        length.get = [](const Feature& f) { return f.value; };
        length.set = [](Feature& f, double v) { f.value = v; };
        length.field = "value";
        length.choices = [] { return QStringList{tr("This way"), tr("The other way")}; };
        length.choice = [](const Feature& f) { return f.flip ? 1 : 0; };
        length.choose = [](Feature& f, int i) { f.flip = i == 1; };
        prompts.push_back(length);
        FeaturePrompt gap;
        gap.label = [] { return tr("Gap under the hem [mm]"); };
        gap.min = 0.01;
        gap.get = [](const Feature& f) { return f.flange.gap; };
        gap.set = [](Feature& f, double v) { f.flange.gap = v; };
        gap.field = "flange.gap";
        prompts.push_back(gap);
        break;
    }
    case Feature::Kind::Relief: {
        FeaturePrompt size;
        size.label = [] { return tr("Relief size [mm]"); };
        size.min = 0.01;
        size.get = [](const Feature& f) { return f.value; };
        size.set = [](Feature& f, double v) { f.value = v; };
        size.field = "value";
        size.choices = [] { return QStringList{tr("Round"), tr("Square")}; };
        size.choice = [](const Feature& f) { return f.flange.square ? 1 : 0; };
        size.choose = [](Feature& f, int i) { f.flange.square = i == 1; };
        prompts.push_back(size);
        break;
    }
    default:
        break;
    }
    return prompts;
}

void SketchEditor::rememberSheetValues(const Feature& f)
{
    switch (f.kind) {
    case Feature::Kind::SheetBase:
        s_sheet = f.sheet;
        if (f.regions.empty())
            s_sheetDepth = f.value;
        break;
    case Feature::Kind::SheetConvert:
        s_sheet.radius = f.sheet.radius;
        s_sheet.kFactor = f.sheet.kFactor;
        break;
    case Feature::Kind::Flange:
        s_flangeLength = f.value;
        s_flange.angle = f.flange.angle;
        s_flange.inset = f.flange.inset;
        s_flange.inside = f.flange.inside;
        s_flangeFlip = f.flip;
        break;
    case Feature::Kind::Hem:
        s_hemLength = f.value;
        s_flange.gap = f.flange.gap;
        break;
    case Feature::Kind::Relief:
        s_reliefSize = f.value;
        s_flange.square = f.flange.square;
        break;
    default:
        break;
    }
}

QString SketchEditor::sheetHint() const
{
    const QString error = modelText(m_solidError);
    const bool asking = m_valueEditor->isOpen();
    auto with = [&](const QString& name, const QString& text) {
        return QStringLiteral("%1: %2").arg(name, !error.isEmpty() && asking ? error : text);
    };
    switch (m_toolKind) {
    case ToolKind::SheetBase:
        if (asking)
            return with(tr("Base flange"), tr("type the values, pick the side  ·  Enter accepts each"));
        return tr("Base flange: tap a sketch region for a flat sheet, or an open sketch line to bend along");
    case ToolKind::Flange:
        if (asking)
            return with(tr("Flange"), tr("type the length, angle and inset  ·  Enter accepts each"));
        return tr("Flange: tap an edge along an end of the sheet (where its thickness shows); it bends toward "
                  "the side you tapped it on");
    case ToolKind::Hem:
        if (asking)
            return with(tr("Hem"), tr("type the length and the gap  ·  Enter accepts each"));
        return tr("Hem: tap an edge along an end of the sheet to fold back");
    case ToolKind::Tab:
        return tr("Tab: tap a sketch region drawn on a side of the sheet");
    case ToolKind::CornerRelief:
        if (asking)
            return with(tr("Corner relief"), tr("type the size, round or square  ·  Enter accepts"));
        return tr("Corner relief: tap the corner where bends meet");
    case ToolKind::SheetConvert:
        if (asking)
            return with(tr("Convert to sheet metal"), tr("type the thickness, bend radius and k-factor  ·  Enter accepts each"));
        return tr("Convert to sheet metal: tap the outside of the wall that stays (a shelled body's floor, say)");
    default:
        return {};
    }
}

// --- Flat patterns --------------------------------------------------------------------

std::vector<SketchEditor::NamedFlat> SketchEditor::flatPatterns() const
{
    std::vector<NamedFlat> out;
    const std::vector<BodyInfo> infos = bodyInfos();
    for (const SheetFound& s : findSheets(m_bodies, m_context)) {
        if (s.body < 0 || s.body >= static_cast<int>(infos.size()) || infos[s.body].hidden)
            continue;
        out.push_back({infos[s.body].name, unfoldSheet(m_bodies[s.body], s.fixed, s.sheet.spec)});
    }
    return out;
}

void SketchEditor::setFlatPatternsShown(bool shown)
{
    m_flatShown = shown;
    showFlatPatterns();
    if (shown && flatPatterns().empty())
        emit message(tr("No sheet metal bodies yet: start with a Base Flange, or convert a shelled body"));
    m_viewer->update();
}

void SketchEditor::showFlatPatterns()
{
    const auto& ctx = m_viewer->aisContext();
    for (Handle(AIS_Shape)* prs : {&m_flatPrs, &m_flatBendPrs}) {
        if (!prs->IsNull())
            ctx->Remove(*prs, false);
        prs->Nullify();
    }
    if (!m_flatShown || mode() != EditorMode::Model)
        return;
    const std::vector<NamedFlat> flats = flatPatterns();
    if (flats.empty())
        return;
    // Beside the model, on the ground (or its floor, above it), one behind the other.
    const Bnd_Box box = bodiesBox();
    double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    if (!box.IsVoid())
        box.Get(x0, y0, z0, x1, y1, z1);
    const double gap = 20.0;
    double y = y0;
    TopoDS_Compound solids, bends;
    BRep_Builder builder;
    builder.MakeCompound(solids);
    builder.MakeCompound(bends);
    bool anySolid = false, anyBend = false;
    for (const NamedFlat& named : flats) {
        if (!named.flat.ok()) {
            emit message(tr("%1: %2").arg(named.name, modelText(named.flat.error)));
            continue;
        }
        Bnd_Box flatBox;
        double t = 0.0;
        {
            double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
            BRepBndLib::Add(named.flat.solid, flatBox);
            flatBox.Get(a, b, c, d, e, f);
            t = f - c;
        }
        gp_Trsf place;
        place.SetTranslation(gp_Vec(x1 + gap, y, std::max(z0, 0.0) + t)); // on the ground, above the grid
        builder.Add(solids, BRepBuilderAPI_Transform(named.flat.solid, place, true).Shape());
        anySolid = true;
        for (const FlatBend& b : named.flat.bends) {
            const gp_Pnt a = gp_Pnt(b.a.X(), b.a.Y(), 0.02).Transformed(place);
            const gp_Pnt c = gp_Pnt(b.b.X(), b.b.Y(), 0.02).Transformed(place);
            if (a.Distance(c) > 1e-6) {
                builder.Add(bends, BRepBuilderAPI_MakeEdge(a, c).Edge());
                anyBend = true;
            }
        }
        y += named.flat.height + gap;
    }
    if (anySolid) {
        m_flatPrs = new AIS_Shape(solids);
        m_flatPrs->Attributes()->SetFaceBoundaryDraw(true);
        ctx->Display(m_flatPrs, AIS_Shaded, -1, false); // to look at, not to pick
        ctx->SetColor(m_flatPrs, m_colors.preview, false);
    }
    if (anyBend) {
        m_flatBendPrs = new AIS_Shape(bends);
        ctx->Display(m_flatBendPrs, AIS_WireFrame, -1, false);
        ctx->SetColor(m_flatBendPrs, m_colors.accent, false);
        ctx->SetWidth(m_flatBendPrs, 2.0, false);
    }
}
