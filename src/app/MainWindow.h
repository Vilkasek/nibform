#pragma once

#include "app/Icons.h"
#include "app/Theme.h"
#include "io/Recovery.h"
#include "sketch/ConstraintCommands.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchTools.h"
#include "model/Units.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <vector>

class OcctViewerWidget;
class QAction;
class QCloseEvent;
class QFrame;
class QToolButton;
class QLabel;
class QMenu;
class QToolBar;
class QDockWidget;
class QTreeWidget;
class RadialMenu;
struct PenSample;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openPath(const QString& path);
    // Offers the work of sessions that ended without closing; true if one was restored.
    bool offerRecovery();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();   // menus, bars and actions (again after a language change)
    void syncUi();    // ... set to the current state
    void rebuildUi();
    void buildActions();
    void onPenInput(const PenSample& sample);
    // The tools of the current mode in a ring at `pos` (viewer coordinates).
    void showRadialMenu(const QPointF& pos);
    // Two fingers undo, three redo.
    void onFingerTap(int fingers);
    // The Bodies panel: names, visibility and colors of the bodies.
    void buildBodyList();
    void updateBodyList();
    void setUnits(LengthUnit unit);
    void updateEditActions();
    void updateMode(EditorMode mode);
    void loadDemo();
    void setTheme(ThemeMode mode);
    void applyTheme();
    void applyStyle(bool dark);
    void iconize(QAction* action, Icon icon);
    void iconize(QAction* action, ConstraintCommand command);
    void buildContextBar();
    void updateContextBar();
    void updateTimeline();
    void updateTitle();
    void showMessage(const QString& text);
    void applyTabletMode();
    void updateLayout();
    // Files: maybeSave() asks about unsaved changes, false means stay.
    bool maybeSave();
    void newFile();
    void openFile();
    bool save();
    bool saveAs();
    enum class ExportFormat { Step, Iges, Stl, ThreeMf };
    void exportBodies(ExportFormat format);
    void importModel();
    // A STEP / IGES / STL opened as a new, unsaved project.
    bool openModel(const QString& path);
    static bool isModelFile(const QString& path);
    void autosave();
    void addRecentFile(const QString& path);
    void fillRecentMenu(QMenu* menu);
    void showAbout();
    QString lastDirectory() const;
    void rememberDirectory(const QString& path);

    OcctViewerWidget* m_viewer = nullptr;
    SketchEditor* m_editor = nullptr;

    std::map<ToolKind, QAction*> m_toolActions;
    std::map<ConstraintCommand, QAction*> m_constraintActions;
    std::vector<QAction*> m_bodyActions;
    QToolBar* m_constraintBar = nullptr;
    QToolBar* m_modelBar = nullptr;
    QToolBar* m_sketchBar = nullptr;
    QMenu* m_constraintMenu = nullptr;
    QMenu* m_bodiesMenu = nullptr;
    QAction* m_newSketchAction = nullptr;
    QAction* m_faceSketchAction = nullptr;
    QAction* m_editSketchAction = nullptr;
    QAction* m_closeSketchAction = nullptr;
    QAction* m_clearSketchAction = nullptr;
    std::map<ThemeMode, QAction*> m_themeActions;
    std::vector<std::pair<QAction*, std::function<QIcon(const QColor&, const QColor&)>>> m_icons;
    QFrame* m_contextBar = nullptr;
    QObject* m_uiRoot = nullptr; // owns the window-level actions of the current UI build
    bool m_rebuildPending = false;
    QToolBar* m_timelineBar = nullptr;
    QToolBar* m_quickBar = nullptr;
    QToolBar* m_viewBar = nullptr;
    QAction* m_tabletAction = nullptr;
    QAction* m_cancelAction = nullptr;
    QAction* m_confirmAction = nullptr;
    QAction* m_fitAction = nullptr;
    std::optional<bool> m_portrait;
    bool m_viewReady = false; // the first view has been set up
    QToolButton* m_newSketchButton = nullptr;
    QAction* m_operationLabel = nullptr;
    int m_timelinePick = -1; // the history chip last tapped
    QString m_path;   // the open project file, empty until saved
    std::unique_ptr<RecoverySession> m_recovery;
    QTimer m_autosaveTimer;
    int m_autosavedChanges = -1; // the editor's change count at the last autosave
    QString m_accent; // current icon accent color
    QString m_hint;   // the editor's current hint, shown again after a message
    QTimer m_messageTimer;
    QAction* m_exportStepAction = nullptr;
    QAction* m_exportStlAction = nullptr;
    QAction* m_exportIgesAction = nullptr;
    QAction* m_export3mfAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_importDrawingAction = nullptr;
    QAction* m_exportDrawingAction = nullptr;
    std::vector<std::pair<QToolButton*, Icon>> m_contextButtons;
    QToolButton* m_ctxFillet = nullptr;
    QToolButton* m_ctxChamfer = nullptr;
    QToolButton* m_ctxSketch = nullptr;
    QToolButton* m_ctxPull = nullptr;
    QToolButton* m_ctxShell = nullptr;
    QAction* m_constructionAction = nullptr;
    QMenu* m_sketchMenu = nullptr;
    QMenu* m_imageMenu = nullptr;
    QAction* m_removeImageAction = nullptr;
    std::vector<QAction*> m_imageItems; // need an image to work on
    std::vector<QToolButton*> m_groupButtons; // sketch bar buttons with a menu of tools
    ThemeMode m_theme = ThemeMode::System;
    bool m_applyingTheme = false;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QPointer<RadialMenu> m_radialMenu;
    QDockWidget* m_bodyDock = nullptr;
    QTreeWidget* m_bodyList = nullptr;
    bool m_fillingBodyList = false;
    QAction* m_sectionAction = nullptr;
    std::map<LengthUnit, QAction*> m_unitActions;

    QLabel* m_hintLabel = nullptr;
    QLabel* m_dofLabel = nullptr;
    QLabel* m_inputLabel = nullptr;
    QLabel* m_glLabel = nullptr;
};
