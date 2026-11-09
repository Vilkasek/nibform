// SketchEditor: getting geometry in and out – imported models (bodies or references),
// 2D drawings into / out of the open sketch, and the clipboard.

#include "sketch/SketchEditor.h"

#include "io/SketchExchange.h"
#include "sketch/SketchOps.h"
#include "viewer/OcctViewerWidget.h"

#include <AIS_InteractiveContext.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>

#include <algorithm>

void SketchEditor::showReferences()
{
    const auto& ctx = m_viewer->aisContext();
    for (const Handle(AIS_Shape)& prs : m_referencePrs)
        ctx->Remove(prs, false);
    m_referencePrs.clear();
    for (const TopoDS_Shape& shape : m_context.references) {
        Handle(AIS_Shape) prs = new AIS_Shape(shape);
        prs->SetColor(m_colors.inactive);
        prs->SetTransparency(0.6);
        ctx->Display(prs, AIS_Shaded, -1, false); // to look at, not to pick
        m_referencePrs.push_back(prs);
    }
}

bool SketchEditor::importModel(const QString& path, bool reference)
{
    std::string error;
    const auto shape = importShape(path.toStdString(), error);
    if (!shape) {
        emit message(modelText(error));
        return false;
    }
    if (mode() == EditorMode::Sketch)
        closeSketch();
    endValueSession(true);
    Feature f;
    f.kind = Feature::Kind::Import;
    f.shape = std::make_shared<const std::string>(shapeToText(*shape));
    f.label = QFileInfo(path).fileName().toStdString();
    f.reference = reference;
    if (!commitFeature(f))
        return false;
    emit message(reference ? tr("Imported %1 as a reference").arg(QFileInfo(path).fileName())
                           : tr("Imported %1").arg(QFileInfo(path).fileName()));
    return true;
}

std::vector<PrintBody> SketchEditor::printBodies() const
{
    std::vector<PrintBody> out;
    const std::vector<BodyInfo> infos = bodyInfos();
    for (int b = 0; b < bodyCount(); ++b) {
        if (infos[b].hidden)
            continue;
        PrintBody body;
        body.shape = m_bodies[b];
        body.name = infos[b].name.toStdString();
        body.color = {infos[b].color.redF(), infos[b].color.greenF(), infos[b].color.blueF()};
        out.push_back(body);
    }
    return out;
}

bool SketchEditor::importDrawing(const QString& path)
{
    if (m_active < 0) {
        emit message(tr("Open a sketch to import a drawing into"));
        return false;
    }
    // Read into a copy first: a file that can't be read leaves no undo step behind.
    Sketch probe = sketch();
    QString error;
    const auto made = importSketchFile(probe, path, &error);
    if (!made) {
        emit message(error);
        return false;
    }
    setSelection({});
    commit([&](Sketch& s) { s = probe; });
    SketchSelection picked;
    picked.entities.insert(made->begin(), made->end());
    setSelection(picked);
    refreshSketches();
    emit message(tr("Imported from %1: %2 curves").arg(QFileInfo(path).fileName()).arg(made->size()));
    m_viewer->update();
    return true;
}

QString SketchEditor::exportDrawing(const QString& path) const
{
    if (m_active < 0)
        return tr("Open a sketch to export");
    return QFileInfo(path).suffix().toLower() == QLatin1String("svg") ? exportSketchSvg(sketch(), path)
                                                                     : exportSketchDxf(sketch(), path);
}

bool SketchEditor::canCopy() const
{
    return mode() == EditorMode::Sketch ? !m_selection.entities.empty() : !selectedBodies().empty();
}

void SketchEditor::copy()
{
    auto* data = new QMimeData;
    if (mode() == EditorMode::Sketch) {
        if (m_selection.entities.empty())
            return;
        data->setData(QString::fromLatin1(kClipboardMime), sketchClipboard(copyEntities(sketch(), m_selection.entities)));
        emit message(tr("Copied curves: %1").arg(m_selection.entities.size()));
    } else {
        const std::vector<int> bodies = selectedBodies();
        if (bodies.empty())
            return;
        TopoDS_Compound all;
        BRep_Builder().MakeCompound(all);
        for (int b : bodies)
            BRep_Builder().Add(all, m_bodies[b]);
        data->setData(QString::fromLatin1(kClipboardMime), bodiesClipboard(shapeToText(all)));
        emit message(tr("Copied bodies: %1").arg(bodies.size()));
    }
    QGuiApplication::clipboard()->setMimeData(data);
}

void SketchEditor::paste()
{
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    const QByteArray bytes = data ? data->data(QString::fromLatin1(kClipboardMime)) : QByteArray();
    if (const auto part = sketchFromClipboard(bytes)) {
        if (m_active < 0) {
            emit message(tr("Open a sketch to paste curves into"));
            return;
        }
        // Pasted where they were, unless that is right on top of themselves.
        gp_Vec2d offset(0.0, 0.0);
        const bool onTop = std::any_of(part->points.begin(), part->points.end(), [&](const SketchPoint& p) {
            return sketch().findPoint(p.pos, 1e-6).has_value();
        });
        if (onTop) {
            const double step = pixelsToModel(30.0);
            offset = gp_Vec2d(step, -step);
        }
        std::vector<EntityId> made;
        commit([&](Sketch& s) { made = pasteContents(s, *part, offset); });
        SketchSelection picked;
        picked.entities.insert(made.begin(), made.end());
        setSelection(picked);
        refreshSketches();
        m_viewer->update();
        return;
    }
    if (const auto shape = bodiesFromClipboard(bytes)) {
        if (mode() == EditorMode::Sketch)
            closeSketch();
        Feature f;
        f.kind = Feature::Kind::Import;
        f.shape = std::make_shared<const std::string>(*shape);
        f.label = tr("pasted").toStdString();
        commitFeature(f);
        return;
    }
    emit message(tr("Nothing to paste: copy curves or bodies first"));
}

void SketchEditor::setDrawing(int index, const Drawing& drawing, bool undoable)
{
    if (index < 0 || index > static_cast<int>(m_drawings.size()))
        return;
    if (undoable)
        pushUndo();
    if (index == static_cast<int>(m_drawings.size()))
        m_drawings.push_back(drawing);
    else
        m_drawings[index] = drawing;
    if (undoable) {
        emit historyChanged();
        emit drawingsChanged();
    }
}

std::vector<TopoDS_Shape> SketchEditor::visibleBodies() const
{
    std::vector<TopoDS_Shape> out;
    for (const PrintBody& b : printBodies())
        out.push_back(b.shape);
    return out;
}
