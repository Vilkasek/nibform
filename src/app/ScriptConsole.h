#pragma once

#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// The script console (M19): Python typed a line at a time (blocks continue on "..."
// lines), its output and errors below. The window runs what is typed on its project.
class ScriptConsole : public QWidget
{
    Q_OBJECT

public:
    struct Hooks
    {
        // Runs code typed in (the console keeps its variables): false when it raised.
        std::function<bool(const QString& code)> run;
        // Whether the code is a whole statement yet (else more lines follow).
        std::function<bool(const QString& code)> complete;
    };
    explicit ScriptConsole(Hooks hooks, QWidget* parent = nullptr);

    void print(const QString& text);      // the script's output
    void printError(const QString& text); // its errors
    void setRecording(bool on);
    void retranslate();
    void focusInput();

signals:
    void runFileRequested();
    void recordToggled(bool on);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submit();

    Hooks m_hooks;
    QPlainTextEdit* m_output = nullptr;
    QLabel* m_prompt = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_runFile = nullptr;
    QPushButton* m_record = nullptr;
    QPushButton* m_clear = nullptr;
    QStringList m_pending; // lines of a block not finished yet
    QStringList m_history;
    int m_historyAt = 0;
};
