#pragma once

#include <QMainWindow>

class OcctViewerWidget;
class QLabel;
struct PenSample;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildToolbar();
    void showDemoScene();
    void onPenInput(const PenSample& sample);

    OcctViewerWidget* m_viewer = nullptr;
    QLabel* m_inputLabel = nullptr;
    QLabel* m_glLabel = nullptr;
};
