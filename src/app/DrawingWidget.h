#pragma once

#include "app/DrawingSheet.h"

#include <QWidget>

#include <optional>
#include <vector>

class SketchEditor;

// The drawing sheet on screen: views, dimensions and notes to place and edit with the
// pen or the mouse. Edits go to the editor's drawing (one undo step each); the views are
// projected again when the model or a view changes, and dimensions follow the model.
class DrawingWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Select, Dimension, Note };

    explicit DrawingWidget(SketchEditor* editor, QWidget* parent = nullptr);

    // Shows drawing `index` (making a first one when there is none), projected anew.
    void open(int index = 0);
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void addView(ViewKind kind);
    void setSheet(double width, double height);
    void editTitle();
    void deleteSelected();
    void cancel();
    void fitSheet();
    bool hasSelection() const { return m_selected.kind != SheetHighlight::Kind::None; }
    void setScrollZooms(bool zooms) { m_scrollZooms = zooms; } // as the 3D view

    QString exportPdf(const QString& path) const; // error, empty on success
    QString exportSvg(const QString& path) const;
    void print();

signals:
    void hintChanged(const QString& hint);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool event(QEvent* e) override;

private:
    struct Snap
    {
        int view = -1;
        gp_Pnt model;
        gp_Pnt2d sheet;
        double radius = 0.0;    // a circle's rim (Radius / Diameter), 0 for a point
        bool full = true;
    };

    void reload();            // the editor's drawing changed (undo, another edit)
    void regenerate();        // projects the views that changed
    void commit();            // the working copy becomes an undo step
    SheetContent content() const;
    gp_Pnt2d sheetAt(const QPointF& pos) const; // widget → sheet (Y up)
    double pixels(double px) const { return px / m_zoom; } // widget px in sheet mm
    SheetHighlight hitAt(const QPointF& pos) const;
    std::optional<Snap> snapAt(const QPointF& pos) const;
    bool dimensionEnds(int index, gp_Pnt2d& a, gp_Pnt2d& b, double& radius) const;
    void dragTo(const gp_Pnt2d& p);
    void contextMenuFor(const SheetHighlight& hit, const QPoint& global);
    void updateHint();

    SketchEditor* m_editor;
    int m_index = 0;
    Drawing m_drawing; // working copy (drags change it before they are committed)
    std::vector<ViewGeometry> m_geometry;
    std::vector<std::string> m_keys; // what each view was projected for
    std::vector<TopoDS_Shape> m_bodies; // what the views were projected from
    gp_Pnt m_middle;

    double m_zoom = 3.0;   // widget px per sheet mm
    QPointF m_origin;      // where the sheet's top left is
    bool m_fitted = false;
    bool m_scrollZooms = true;

    Tool m_tool = Tool::Select;
    SheetHighlight m_selected, m_hover;
    std::optional<Snap> m_snap, m_first; // dimension tool: under the pointer, and picked first

    // Pointer
    QPointF m_pressPos;
    gp_Pnt2d m_pressSheet;
    bool m_pressed = false, m_dragging = false, m_panning = false;
    Drawing m_beforeDrag;
};
