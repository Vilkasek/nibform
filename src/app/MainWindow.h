#pragma once

#include "app/Theme.h"
#include "sketch/ConstraintCommands.h"
#include "sketch/SketchTools.h"

#include <QMainWindow>

#include <map>

class OcctViewerWidget;
class QAction;
class QLabel;
class QToolBar;
class SketchEditor;
struct PenSample;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildActions();
    void onPenInput(const PenSample& sample);
    void updateEditActions();
    void setTheme(ThemeMode mode);
    void applyTheme();

    OcctViewerWidget* m_viewer = nullptr;
    SketchEditor* m_editor = nullptr;

    std::map<ToolKind, QAction*> m_toolActions;
    std::map<ConstraintCommand, QAction*> m_constraintActions;
    QToolBar* m_constraintBar = nullptr;
    std::map<ThemeMode, QAction*> m_themeActions;
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
