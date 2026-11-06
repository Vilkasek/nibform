#include "app/MainWindow.h"

#include "viewer/OcctViewerWidget.h"

#include <QAction>
#include <QLabel>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <AIS_Shape.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Shapr Linux"));

    m_viewer = new OcctViewerWidget(this);
    setCentralWidget(m_viewer);

    m_inputLabel = new QLabel(QStringLiteral("Wejście: —"), this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_inputLabel, 1);
    statusBar()->addPermanentWidget(m_glLabel);

    buildToolbar();
    showDemoScene();

    connect(m_viewer, &OcctViewerWidget::glReady, this, [this](const QString& info) {
        m_glLabel->setText(info);
        // Defer until the first frame has sized the view, otherwise FitAll sees a 0x0 viewport.
        QTimer::singleShot(0, this, [this] { m_viewer->setProjection(V3d_XposYnegZpos); });

        // Dev aid: SHAPR_SCREENSHOT=/path.png renders one frame, saves it and quits.
        const QString shotPath = qEnvironmentVariable("SHAPR_SCREENSHOT");
        if (!shotPath.isEmpty()) {
            QTimer::singleShot(1500, this, [this, shotPath] {
                m_viewer->fitAll();
                QCoreApplication::processEvents();
                grab().save(shotPath);
                QApplication::quit();
            });
        }
    });
    connect(m_viewer, &OcctViewerWidget::glFailed, this, [this](const QString& reason) {
        QMessageBox::critical(this, QStringLiteral("Błąd OpenGL"), reason);
    });
    connect(m_viewer, &OcctViewerWidget::penInput, this, &MainWindow::onPenInput);
}

void MainWindow::buildToolbar()
{
    QToolBar* bar = addToolBar(QStringLiteral("Widok"));
    bar->setMovable(false);
    bar->setIconSize(QSize(28, 28));
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto addView = [this, bar](const QString& name, V3d_TypeOfOrientation orientation) {
        bar->addAction(name, this, [this, orientation] { m_viewer->setProjection(orientation); });
    };

    QAction* fit = bar->addAction(QStringLiteral("Dopasuj"), m_viewer, &OcctViewerWidget::fitAll);
    fit->setToolTip(QStringLiteral("Dopasuj widok (F)"));
    bar->addSeparator();
    addView(QStringLiteral("Izometria"), V3d_XposYnegZpos);
    addView(QStringLiteral("Góra"), V3d_Zpos);
    addView(QStringLiteral("Przód"), V3d_Yneg);
    addView(QStringLiteral("Prawo"), V3d_Xpos);
}

void MainWindow::showDemoScene()
{
    // Placeholder part so there is something to orbit around until sketching lands (M1).
    TopoDS_Shape block = BRepPrimAPI_MakeBox(gp_Pnt(-30, -20, 0), 60.0, 40.0, 20.0).Shape();
    TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp::DZ()), 10.0, 22.0).Shape();
    TopoDS_Shape part = BRepAlgoAPI_Cut(block, hole).Shape();

    BRepFilletAPI_MakeFillet fillet(part);
    for (TopExp_Explorer ex(part, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(ex.Current());
        fillet.Add(2.0, edge);
    }
    try {
        fillet.Build();
        if (fillet.IsDone())
            part = fillet.Shape();
    } catch (const Standard_Failure&) {
        // keep the sharp-edged part
    }

    Handle(AIS_Shape) shape = new AIS_Shape(part);
    shape->SetColor(Quantity_Color(0.62, 0.68, 0.76, Quantity_TOC_sRGB));
    shape->SetMaterial(Graphic3d_NameOfMaterial_Plastified);
    // SetColor() resets the boundary aspect inherited from the context, so set it after.
    shape->Attributes()->SetFaceBoundaryDraw(true);
    shape->Attributes()->SetFaceBoundaryAspect(
        new Prs3d_LineAspect(Quantity_Color(0.15, 0.17, 0.20, Quantity_TOC_sRGB), Aspect_TOL_SOLID, 1.5));
    m_viewer->aisContext()->Display(shape, AIS_Shaded, 0, false);
}

void MainWindow::onPenInput(const PenSample& sample)
{
    const char* phase = "";
    switch (sample.phase) {
    case PenSample::Phase::Hover: phase = "nad ekranem"; break;
    case PenSample::Phase::Down:  phase = "dotyk"; break;
    case PenSample::Phase::Move:  phase = "rysowanie"; break;
    case PenSample::Phase::Up:    phase = "uniesienie"; break;
    }

    m_inputLabel->setText(QStringLiteral("Rysik%1: %2  (%3, %4)  nacisk %5%6")
                              .arg(sample.eraser ? QStringLiteral(" [gumka]") : QString())
                              .arg(QLatin1String(phase))
                              .arg(sample.pos.x(), 0, 'f', 0)
                              .arg(sample.pos.y(), 0, 'f', 0)
                              .arg(sample.pressure, 0, 'f', 2)
                              .arg(sample.barrelButton ? QStringLiteral("  [przycisk]") : QString()));
}
