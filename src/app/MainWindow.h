#pragma once

#include "sketch/SketchTools.h"

#include <QMainWindow>

#include <map>

class OcctViewerWidget;
class QAction;
class QLabel;
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

    OcctViewerWidget* m_viewer = nullptr;
    SketchEditor* m_editor = nullptr;

    std::map<ToolKind, QAction*> m_toolActions;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;

    QLabel* m_hintLabel = nullptr;
    QLabel* m_inputLabel = nullptr;
    QLabel* m_glLabel = nullptr;
};
