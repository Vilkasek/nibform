#include "app/MainWindow.h"

#include "sketch/SketchEditor.h"
#include "viewer/OcctViewerWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Shapr Linux"));

    m_viewer = new OcctViewerWidget(this);
    setCentralWidget(m_viewer);

    m_editor = new SketchEditor(m_viewer, this);
    m_viewer->setInteraction(m_editor);

    m_hintLabel = new QLabel(this);
    m_inputLabel = new QLabel(this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_inputLabel);
    statusBar()->addPermanentWidget(m_glLabel);

    buildActions();

    connect(m_editor, &SketchEditor::hintChanged, m_hintLabel, &QLabel::setText);
    connect(m_editor, &SketchEditor::toolChanged, this, [this](ToolKind kind) {
        if (QAction* action = m_toolActions[kind])
            action->setChecked(true);
    });
    connect(m_editor, &SketchEditor::historyChanged, this, &MainWindow::updateEditActions);
    connect(m_editor, &SketchEditor::selectionChanged, this, &MainWindow::updateEditActions);
    m_editor->setTool(ToolKind::Line);
    updateEditActions();

    connect(m_viewer, &OcctViewerWidget::glReady, this, [this](const QString& info) {
        m_glLabel->setText(info);
        // Defer until the first frame has sized the view, otherwise FitAll sees a 0x0 viewport.
        QTimer::singleShot(0, this, [this] { m_viewer->setProjection(V3d_XposYnegZpos); });

        // Dev aid: SHAPR_SCREENSHOT=/path.png renders one frame, saves it and quits.
        const QString shotPath = qEnvironmentVariable("SHAPR_SCREENSHOT");
        if (!shotPath.isEmpty()) {
            QTimer::singleShot(1500, this, [this, shotPath] {
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

void MainWindow::buildActions()
{
    // --- Edit menu -------------------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edycja"));
    m_undoAction = edit->addAction(QStringLiteral("Cofnij"), m_editor, &SketchEditor::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = edit->addAction(QStringLiteral("Ponów"), m_editor, &SketchEditor::redo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    m_deleteAction = edit->addAction(QStringLiteral("Usuń zaznaczone"), m_editor, &SketchEditor::deleteSelection);
    m_deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    edit->addAction(QStringLiteral("Wyczyść szkic"), m_editor, &SketchEditor::clearActiveSketch);

    // --- Tool bar ----------------------------------------------------------------------
    QToolBar* bar = addToolBar(QStringLiteral("Narzędzia"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* tools = new QActionGroup(this);
    auto addTool = [&](ToolKind kind, const QString& name, Qt::Key key) {
        QAction* action = bar->addAction(name);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(key));
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, QKeySequence(key).toString()));
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, kind] { m_editor->setTool(kind); });
        m_toolActions[kind] = action;
    };
    addTool(ToolKind::Select, QStringLiteral("Zaznacz"), Qt::Key_S);
    addTool(ToolKind::Line, QStringLiteral("Linia"), Qt::Key_L);
    addTool(ToolKind::Rectangle, QStringLiteral("Prostokąt"), Qt::Key_R);
    addTool(ToolKind::Circle, QStringLiteral("Okrąg"), Qt::Key_C);
    addTool(ToolKind::Arc, QStringLiteral("Łuk"), Qt::Key_A);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral(" Płaszczyzna: "), bar));
    auto* plane = new QComboBox(bar);
    plane->setFocusPolicy(Qt::NoFocus);
    plane->addItem(QStringLiteral("Podłoże (XY)"), static_cast<int>(SketchPlane::XY));
    plane->addItem(QStringLiteral("Przód (XZ)"), static_cast<int>(SketchPlane::XZ));
    plane->addItem(QStringLiteral("Prawo (YZ)"), static_cast<int>(SketchPlane::YZ));
    connect(plane, &QComboBox::currentIndexChanged, this, [this, plane](int index) {
        m_editor->setPlane(static_cast<SketchPlane>(plane->itemData(index).toInt()));
    });
    bar->addWidget(plane);

    bar->addSeparator();
    QAction* fit = bar->addAction(QStringLiteral("Dopasuj"), m_viewer, &OcctViewerWidget::fitAll);
    fit->setToolTip(QStringLiteral("Dopasuj widok (F)"));
    auto addView = [this, bar](const QString& name, V3d_TypeOfOrientation orientation) {
        bar->addAction(name, this, [this, orientation] { m_viewer->setProjection(orientation); });
    };
    addView(QStringLiteral("Izometria"), V3d_XposYnegZpos);
    addView(QStringLiteral("Góra"), V3d_Zpos);
    addView(QStringLiteral("Przód"), V3d_Yneg);
    addView(QStringLiteral("Prawo"), V3d_Xpos);
}

void MainWindow::updateEditActions()
{
    m_undoAction->setEnabled(m_editor->canUndo());
    m_redoAction->setEnabled(m_editor->canRedo());
}

void MainWindow::onPenInput(const PenSample& sample)
{
    m_inputLabel->setText(QStringLiteral("Rysik%1: nacisk %2%3")
                              .arg(sample.eraser ? QStringLiteral(" [gumka]") : QString())
                              .arg(sample.pressure, 0, 'f', 2)
                              .arg(sample.barrelButton ? QStringLiteral(" [przycisk]") : QString()));
}
