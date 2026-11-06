#pragma once

#include "app/Icons.h"
#include "app/Theme.h"
#include "sketch/ConstraintCommands.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchTools.h"

#include <QMainWindow>
#include <QTimer>

#include <functional>
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
struct PenSample;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildActions();
    void onPenInput(const PenSample& sample);
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
    // Files: maybeSave() asks about unsaved changes, false means stay.
    bool maybeSave();
    void newFile();
    void openFile();
    bool save();
    bool saveAs();
    void exportBodies(bool step);
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
    QToolBar* m_timelineBar = nullptr;
    int m_timelinePick = -1; // the history chip last tapped
    QString m_path;   // the open project file, empty until saved
    QString m_accent; // current icon accent color
    QString m_hint;   // the editor's current hint, shown again after a message
    QTimer m_messageTimer;
    QAction* m_exportStepAction = nullptr;
    QAction* m_exportStlAction = nullptr;
    std::vector<std::pair<QToolButton*, Icon>> m_contextButtons;
    QToolButton* m_ctxFillet = nullptr;
    QToolButton* m_ctxChamfer = nullptr;
    QToolButton* m_ctxSketch = nullptr;
    QToolButton* m_ctxPull = nullptr;
    QToolButton* m_ctxShell = nullptr;
    QAction* m_constructionAction = nullptr;
    ThemeMode m_theme = ThemeMode::System;
    bool m_applyingTheme = false;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;

    QLabel* m_hintLabel = nullptr;
    QLabel* m_dofLabel = nullptr;
    QLabel* m_inputLabel = nullptr;
    QLabel* m_glLabel = nullptr;
};
