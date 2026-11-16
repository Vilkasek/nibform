// SketchEditor, details (M16): sketch text (placed with the Text tool and asked in a
// small dialog), letters standing for their whole text when picked as regions, cosmetic
// threads shown on the bodies, and fillets whose radius changes along the edge.

#include "sketch/SketchEditor.h"

#include "model/TextShapes.h"
#include "model/Threads.h"
#include "viewer/OcctViewerWidget.h"
#include "viewer/ValueEditor.h"

#include <AIS_InteractiveContext.hxx>
#include <BRep_Builder.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <Prs3d_Drawer.hxx>
#include <TopoDS_Compound.hxx>
#include <V3d_View.hxx>
#include <gp_Lin.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

#include <limits>

namespace {

// How a cosmetic thread is shown on a body: a see-through sleeve just off the threaded
// wall, on the air side (its real other diameter lies inside the material, unseen).
TopoDS_Shape threadSleeve(const ThreadMark& mark)
{
    const double gap = std::max(0.01, 0.015 * mark.radius);
    ThreadMark shown = mark;
    shown.spec.diameter = 2.0 * (mark.internal ? mark.radius - gap : mark.radius + gap);
    shown.internal = true; // threadSurface() draws an internal thread at its diameter
    return threadSurface(shown);
}

// What the next text starts with: the last one's look.
std::string s_textFont;
double s_textHeight = 10.0;
bool s_textBold = false;
bool s_textItalic = false;

} // namespace

// --- Text -------------------------------------------------------------------------------

void SketchEditor::textTap(const SnapResult& s)
{
    if (m_active < 0)
        return;
    if (const auto id = textAt(sketch(), s.pos, pickTolerance()))
        editText(*id);
    else
        editText(kNoId, s);
}

void SketchEditor::editText(EntityId id, std::optional<SnapResult> at)
{
    const SketchText* existing = id != kNoId ? sketch().text(id) : nullptr;
    if (id != kNoId && !existing)
        return;
    SketchText draft;
    draft.text = existing ? existing->text : std::string();
    draft.font = existing ? existing->font : s_textFont;
    draft.height = existing ? existing->height : s_textHeight;
    draft.bold = existing ? existing->bold : s_textBold;
    draft.italic = existing ? existing->italic : s_textItalic;
    const int sketchIndex = m_active;

    // Out of the pointer handler first: the dialog runs its own event loop.
    QTimer::singleShot(0, this, [=, this] {
        if (m_active != sketchIndex)
            return; // the sketch was closed meanwhile
        QDialog dialog(m_viewer);
        dialog.setWindowTitle(existing ? tr("Change Text") : tr("Text"));
        auto* form = new QFormLayout(&dialog);
        auto* words = new QLineEdit(QString::fromStdString(draft.text), &dialog);
        words->setPlaceholderText(tr("The words to write"));
        form->addRow(tr("Text:"), words);
        auto* font = new QComboBox(&dialog);
        font->addItem(tr("Default (sans-serif)"), QString());
        for (const std::string& family : fontFamilies())
            font->addItem(QString::fromStdString(family), QString::fromStdString(family));
        font->setCurrentIndex(std::max(0, font->findData(QString::fromStdString(draft.font))));
        form->addRow(tr("Font:"), font);
        auto* bold = new QCheckBox(tr("Bold"), &dialog);
        bold->setChecked(draft.bold);
        auto* italic = new QCheckBox(tr("Italic"), &dialog);
        italic->setChecked(draft.italic);
        form->addRow(QString(), bold);
        form->addRow(QString(), italic);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        bool remove = false;
        if (existing) {
            QPushButton* drop = buttons->addButton(tr("Delete Text"), QDialogButtonBox::DestructiveRole);
            connect(drop, &QPushButton::clicked, &dialog, [&] {
                remove = true;
                dialog.accept();
            });
        }
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        form->addRow(buttons);
        words->setFocus();
        if (dialog.exec() != QDialog::Accepted || m_active != sketchIndex)
            return;
        if (remove || words->text().trimmed().isEmpty()) {
            if (id != kNoId)
                commit([id](Sketch& s) { s.removeTexts({id}); });
            m_viewer->update();
            return;
        }
        SketchText chosen = draft;
        chosen.text = words->text().toStdString();
        chosen.font = font->currentData().toString().toStdString();
        chosen.bold = bold->isChecked();
        chosen.italic = italic->isChecked();
        // Then its size, in the floating field (in the current length unit).
        askValue([] { return tr("Text height [mm]"); }, chosen.height, 0.1, 1e4,
                 [=, this](double height) {
                     if (m_active != sketchIndex)
                         return;
                     SketchText made = chosen;
                     made.height = height;
                     s_textFont = made.font;
                     s_textHeight = made.height;
                     s_textBold = made.bold;
                     s_textItalic = made.italic;
                     if (id != kNoId) {
                         commit([id, made](Sketch& s) {
                             if (SketchText* t = s.text(id)) {
                                 const PointId keep = t->at;
                                 *t = made;
                                 t->id = id;
                                 t->at = keep;
                             }
                         });
                     } else if (at) {
                         const SnapResult place = *at;
                         commit([place, made](Sketch& s) {
                             const PointId point = place.pointId != kNoId ? place.pointId : s.pointAt(place.pos);
                             s.addText(point, made.text, made.height, made.font, made.bold, made.italic);
                         });
                     }
                     m_viewer->update();
                 },
                 {});
    });
}

std::vector<RegionRef> SketchEditor::letterRegions(const RegionRef& region) const
{
    if (region.sketch < 0 || region.sketch >= static_cast<int>(m_slots.size()))
        return {region};
    const Slot& slot = m_slots[region.sketch];
    if (slot.sketch.texts().empty() || region.index < 0 || region.index >= static_cast<int>(slot.regions.size()))
        return {region};
    const auto inside = insidePoint(slot.sketch, slot.regions[region.index]);
    if (!inside)
        return {region};
    for (const SketchText& text : slot.sketch.texts()) {
        if (!textContains(slot.sketch, text, *inside))
            continue;
        // The letters of this text: every region inside its glyphs (holes in letters aren't).
        std::vector<RegionRef> letters;
        for (int i = 0; i < static_cast<int>(slot.regions.size()); ++i) {
            const auto p = insidePoint(slot.sketch, slot.regions[i]);
            if (p && textContains(slot.sketch, text, *p))
                letters.push_back({region.sketch, i});
        }
        return letters;
    }
    return {region};
}

// --- Threads ------------------------------------------------------------------------------

void SketchEditor::addThreadSleeves(TopoDS_Compound& shapes, const std::vector<ThreadMark>& marks)
{
    for (const ThreadMark& mark : marks) {
        if (!mark.spec.modelled && mark.length > 0.0)
            BRep_Builder().Add(shapes, threadSleeve(mark));
    }
}

void SketchEditor::showThreads()
{
    const auto& ctx = m_viewer->aisContext();
    if (!m_threadPrs.IsNull())
        ctx->Remove(m_threadPrs, false);
    m_threadPrs.Nullify();
    if (mode() == EditorMode::Assembly)
        return; // the part model steps aside
    TopoDS_Compound shapes;
    BRep_Builder().MakeCompound(shapes);
    bool any = false;
    for (const ThreadMark& mark : m_context.allThreads()) {
        if (mark.spec.modelled || mark.length <= 0.0)
            continue; // a cut thread shows itself
        BRep_Builder().Add(shapes, threadSleeve(mark));
        any = true;
    }
    if (!any)
        return;
    m_threadPrs = new AIS_Shape(shapes);
    m_threadPrs->SetColor(m_colors.accent);
    m_threadPrs->SetTransparency(0.55);
    m_threadPrs->Attributes()->SetFaceBoundaryDraw(false);
    ctx->Display(m_threadPrs, AIS_Shaded, -1, false); // to look at, not to pick
}

std::optional<gp_Pnt> SketchEditor::pointOnFace(const TopoDS_Face& face, const QPointF& tap) const
{
    const Handle(V3d_View)& view = m_viewer->view();
    const qreal dpr = m_viewer->devicePixelRatioF();
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(qRound(tap.x() * dpr), qRound(tap.y() * dpr), x, y, z, dx, dy, dz);
    if (gp_Vec(dx, dy, dz).Magnitude() < 1e-12)
        return std::nullopt;
    IntCurvesFace_Intersector intersector(face, 1e-6);
    intersector.Perform(gp_Lin(gp_Pnt(x, y, z), gp_Dir(dx, dy, dz)), -std::numeric_limits<double>::max(),
                        std::numeric_limits<double>::max());
    if (!intersector.IsDone() || intersector.NbPnt() == 0)
        return std::nullopt;
    // The crossing nearest the eye (the ray starts behind it, so the smallest parameter).
    int nearest = 1;
    for (int i = 2; i <= intersector.NbPnt(); ++i) {
        if (intersector.WParameter(i) < intersector.WParameter(nearest))
            nearest = i;
    }
    return intersector.Pnt(nearest);
}

// --- Fillets of varying radius --------------------------------------------------------------

bool SketchEditor::canVaryFillet(int index) const
{
    return index >= 0 && index < static_cast<int>(m_features.size())
        && m_features[index].kind == Feature::Kind::Fillet;
}

void SketchEditor::varyFillet(int index)
{
    if (!canVaryFillet(index))
        return;
    FeaturePrompt start;
    start.label = [] { return tr("Radius where the edges start [mm]"); };
    start.min = 0.01;
    start.get = [](const Feature& f) { return f.value; };
    start.set = [](Feature& f, double v) { f.value = v; };
    start.field = "value";
    FeaturePrompt end;
    end.label = [] { return tr("Radius where they end [mm] (0: the same)"); };
    end.min = 0.0;
    end.get = [](const Feature& f) { return f.endValue > 0.0 ? f.endValue : f.value; };
    end.set = [](Feature& f, double v) { f.endValue = std::abs(v - f.value) < 1e-9 ? 0.0 : v; };
    end.field = "endValue";
    editFeature(index, {start, end});
}

void SketchEditor::setBackFillet(int index)
{
    if (!canVaryFillet(index))
        return;
    FeaturePrompt setback;
    setback.label = [] { return tr("Corner setback [mm] (where 3+ edges meet; 0: none)"); };
    setback.min = 0.0;
    setback.get = [](const Feature& f) { return f.setback; };
    setback.set = [](Feature& f, double v) { f.setback = v; };
    setback.field = "setback";
    editFeature(index, {setback});
}
