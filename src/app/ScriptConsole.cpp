#include "app/ScriptConsole.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

ScriptConsole::ScriptConsole(Hooks hooks, QWidget* parent)
    : QWidget(parent)
    , m_hooks(std::move(hooks))
{
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setMaximumBlockCount(5000);
    m_prompt = new QLabel(QStringLiteral(">>>"), this);
    m_prompt->setFont(mono);
    m_input = new QLineEdit(this);
    m_input->setFont(mono);
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, &ScriptConsole::submit);
    m_runFile = new QPushButton(this);
    m_record = new QPushButton(this);
    m_record->setCheckable(true);
    m_clear = new QPushButton(this);
    connect(m_runFile, &QPushButton::clicked, this, &ScriptConsole::runFileRequested);
    connect(m_record, &QPushButton::toggled, this, &ScriptConsole::recordToggled);
    connect(m_clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);

    auto* line = new QHBoxLayout;
    line->addWidget(m_prompt);
    line->addWidget(m_input, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_runFile);
    buttons->addWidget(m_record);
    buttons->addStretch(1);
    buttons->addWidget(m_clear);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(buttons);
    layout->addWidget(m_output, 1);
    layout->addLayout(line);
    retranslate();
}

void ScriptConsole::retranslate()
{
    m_runFile->setText(tr("Run File…"));
    m_record->setText(m_record->isChecked() ? tr("Stop Recording") : tr("Record Macro"));
    m_record->setToolTip(tr("What you do while recording becomes a script that does it again"));
    m_clear->setText(tr("Clear"));
    m_input->setPlaceholderText(tr("Python: doc = nf.document(), help(nf) …"));
    if (m_output->document()->isEmpty())
        print(tr("Nibform scripting: `nf` is the nibform module, nf.document() the project in the window.\n"));
}

void ScriptConsole::setRecording(bool on)
{
    const QSignalBlocker block(m_record);
    m_record->setChecked(on);
    retranslate();
}

void ScriptConsole::focusInput()
{
    m_input->setFocus();
}

void ScriptConsole::print(const QString& text)
{
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void ScriptConsole::printError(const QString& text)
{
    QTextCharFormat red;
    red.setForeground(QColor(220, 70, 60));
    m_output->moveCursor(QTextCursor::End);
    m_output->textCursor().insertText(text, red);
    m_output->setCurrentCharFormat(QTextCharFormat());
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void ScriptConsole::submit()
{
    const QString text = m_input->text();
    m_input->clear();
    if (!text.trimmed().isEmpty()) {
        m_history.removeAll(text);
        m_history << text;
    }
    m_historyAt = static_cast<int>(m_history.size());
    print((m_pending.isEmpty() ? QStringLiteral(">>> ") : QStringLiteral("... ")) + text + QLatin1Char('\n'));
    m_pending << text;
    const QString code = m_pending.join(QLatin1Char('\n'));
    // A statement runs as soon as it is whole; a block (def, for, …) once an empty line ends it.
    const bool more = m_pending.size() == 1 ? !m_hooks.complete(code) : !text.trimmed().isEmpty();
    if (more) {
        m_prompt->setText(QStringLiteral("..."));
        return;
    }
    m_pending.clear();
    m_prompt->setText(QStringLiteral(">>>"));
    if (!code.trimmed().isEmpty())
        m_hooks.run(code);
}

bool ScriptConsole::eventFilter(QObject* watched, QEvent* event)
{
    // The window's shortcuts (Enter accepts, Esc cancels, letters pick tools) don't
    // apply while typing here.
    if (watched == m_input && event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier
            || key->modifiers() == Qt::KeypadModifier) {
            event->accept();
            return true;
        }
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Up && m_historyAt > 0) {
            m_input->setText(m_history[--m_historyAt]);
            return true;
        }
        if (key->key() == Qt::Key_Down && m_historyAt < m_history.size()) {
            ++m_historyAt;
            m_input->setText(m_historyAt < m_history.size() ? m_history[m_historyAt] : QString());
            return true;
        }
        if (key->key() == Qt::Key_Tab) {
            m_input->insert(QStringLiteral("    "));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
