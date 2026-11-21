// MainWindow, scripting (M19): the Scripts menu, the console, running script files,
// recording macros, plugins (commands and generators from a folder), and asking before a
// project's scripts from elsewhere run.

#include "app/MainWindow.h"

#include "app/Icons.h"
#include "app/ScriptConsole.h"
#include "sketch/SketchEditor.h"

#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#ifdef NIBFORM_HAS_SCRIPTING
#include "script/ScriptEngine.h"

namespace {

QString pluginFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

} // namespace

void MainWindow::buildScriptConsole()
{
    m_scriptDoc = std::make_shared<ScriptDocument>();
    m_scriptDock = new QDockWidget(this);
    m_scriptDock->setObjectName(QStringLiteral("scriptDock"));
    ScriptConsole::Hooks hooks;
    hooks.complete = [](const QString& code) { return ScriptEngine::instance().complete(code.toStdString()); };
    hooks.run = [this](const QString& code) {
        startScripting();
        settleModel();
        if (m_editor->mode() == EditorMode::Sketch)
            m_editor->closeSketch();
        m_scriptDoc->reset(m_editor->project());
        const auto result = ScriptEngine::instance().run(
            code.toStdString(), "<console>", m_scriptDoc,
            {[this](const std::string& s) { m_scriptConsole->print(QString::fromStdString(s)); },
             [this](const std::string& s) { m_scriptConsole->printError(QString::fromStdString(s)); }},
            "console");
        if (!result.ok)
            m_scriptConsole->printError(QString::fromStdString(result.error) + QLatin1Char('\n'));
        if (m_scriptDoc->changed())
            m_editor->applyProject(m_scriptDoc->project());
        updatePluginButton(); // a command or generator typed in shows up
        return result.ok;
    };
    m_scriptConsole = new ScriptConsole(hooks, m_scriptDock);
    connect(m_scriptConsole, &ScriptConsole::runFileRequested, this, &MainWindow::runScriptFile);
    connect(m_scriptConsole, &ScriptConsole::recordToggled, this, &MainWindow::toggleMacro);
    m_scriptDock->setWidget(m_scriptConsole);
    addDockWidget(Qt::BottomDockWidgetArea, m_scriptDock);
    m_scriptDock->setVisible(QSettings().value(QStringLiteral("scriptConsole"), false).toBool());
    connect(m_scriptDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!isMinimized())
            QSettings().setValue(QStringLiteral("scriptConsole"), visible);
        if (visible) {
            startScripting();
            m_scriptConsole->focusInput();
        }
    });
    // Plugins load once the window is up.
    QTimer::singleShot(0, this, [this] {
        startScripting();
        reloadPlugins();
    });
}

void MainWindow::startScripting()
{
    ScriptEngine& engine = ScriptEngine::instance();
    if (engine.running())
        return;
    std::string error;
    if (!engine.start(&error)) {
        showMessage(tr("Python didn't start: %1").arg(QString::fromStdString(error)));
        return;
    }
    engine.setCurrent(m_scriptDoc);
}

void MainWindow::buildScriptsMenu()
{
    m_scriptsMenu = menuBar()->addMenu(tr("&Scripts"));
    m_scriptsMenu->setToolTipsVisible(true);
    QAction* console = m_scriptDock->toggleViewAction();
    console->setText(tr("Script Console"));
    console->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    iconize(console, Icon::Console);
    m_scriptDock->setWindowTitle(tr("Script Console"));
    m_scriptsMenu->addAction(console);
    bindable(console, QStringLiteral("scripts.console"), tr("Scripts"));
    QAction* run = m_scriptsMenu->addAction(tr("Run Script…"), this, &MainWindow::runScriptFile);
    run->setToolTip(tr("Runs a Python file on the project (as one step to undo)"));
    iconize(run, Icon::Script);
    bindable(run, QStringLiteral("scripts.run"), tr("Scripts"));
    m_recordAction = m_scriptsMenu->addAction(tr("Record Macro"), this, &MainWindow::toggleMacro);
    m_recordAction->setCheckable(true);
    m_recordAction->setChecked(m_recordingFrom != nullptr);
    m_recordAction->setToolTip(tr("What you add to the project while recording becomes a script that adds it again"));
    bindable(m_recordAction, QStringLiteral("scripts.record"), tr("Scripts"));
    m_scriptsMenu->addSeparator();

    // Generators: features a script makes (from plugins, or scripts run in this session).
    QMenu* generators = m_scriptsMenu->addMenu(tr("Insert Generated Feature"));
    connect(generators, &QMenu::aboutToShow, this, [this, generators] {
        generators->clear();
        const auto all = ScriptEngine::instance().generators();
        if (all.empty()) {
            generators->addAction(tr("None yet: plugins and scripts add them (nibform.generator)"))->setEnabled(false);
            return;
        }
        for (const auto& g : all) {
            generators->addAction(QString::fromStdString(g.help.empty() ? g.name : g.help), this, [this, g] {
                settleModel();
                if (m_editor->mode() == EditorMode::Sketch)
                    m_editor->closeSketch();
                m_scriptDoc->reset(m_editor->project());
                Feature f;
                f.kind = Feature::Kind::Script;
                f.operation = BodyOperation::NewBody;
                f.script.source = g.source;
                f.script.name = g.name;
                f.script.values = g.values;
                f.label = g.help;
                const int index = m_scriptDoc->addFeature(f);
                m_editor->applyProject(m_scriptDoc->project());
                m_editor->activateTimelineItem(index); // its values, asked with a live preview
            });
        }
    });

    // Plugins: commands from the plugin folder.
    m_pluginMenu = m_scriptsMenu->addMenu(tr("Plugins"));
    connect(m_pluginMenu, &QMenu::aboutToShow, this, [this] {
        m_pluginMenu->clear();
        for (QAction* a : pluginActions())
            m_pluginMenu->addAction(a);
        if (m_pluginActions.empty())
            m_pluginMenu->addAction(tr("None: put .py files in the plugin folder"))->setEnabled(false);
        m_pluginMenu->addSeparator();
        m_pluginMenu->addAction(tr("Reload Plugins"), this, &MainWindow::reloadPlugins);
        m_pluginMenu->addAction(tr("Open Plugin Folder"), this, [] {
            QDir().mkpath(pluginFolder());
            QDesktopServices::openUrl(QUrl::fromLocalFile(pluginFolder()));
        });
    });
    m_scriptsMenu->addSeparator();
    QAction* trust = m_scriptsMenu->addAction(tr("Trust This Project's Scripts"), this, [this] { trustProjectScripts(false); });
    trust->setToolTip(tr("Lets the generated features of a project from elsewhere run their Python code"));

    // On the model bar: the plugins' commands, while there are any.
    auto* button = new QToolButton(m_modelBar);
    auto* menu = new QMenu(button);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        for (QAction* a : pluginActions())
            menu->addAction(a);
    });
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setText(tr("Plugins"));
    button->setToolTip(tr("Commands added by plugins (Scripts → Plugins)"));
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIcon(makeIcon(Icon::Script, palette().color(QPalette::WindowText), palette().color(QPalette::Highlight)));
    m_groupButtons.push_back(button);
    m_pluginButton = m_modelBar->addWidget(button);
    updatePluginButton();
}

std::vector<QAction*> MainWindow::pluginActions()
{
    // Made again when the commands change; owned by the window.
    const auto commands = ScriptEngine::instance().commands();
    if (commands.size() != m_pluginActions.size()) {
        for (QAction* a : m_pluginActions)
            a->deleteLater();
        m_pluginActions.clear();
        for (const auto& c : commands) {
            auto* action = new QAction(QString::fromStdString(c.name), this);
            action->setToolTip(QString::fromStdString(c.help));
            action->setIcon(makeIcon(Icon::Script, palette().color(QPalette::WindowText), palette().color(QPalette::Highlight)));
            const std::string id = c.id;
            connect(action, &QAction::triggered, this, [this, id, name = action->text()] {
                settleModel();
                if (m_editor->mode() == EditorMode::Sketch)
                    m_editor->closeSketch();
                m_scriptDoc->reset(m_editor->project());
                const auto result = ScriptEngine::instance().runCommand(
                    id, m_scriptDoc,
                    {[this](const std::string& s) { m_scriptConsole->print(QString::fromStdString(s)); },
                     [this](const std::string& s) { m_scriptConsole->printError(QString::fromStdString(s)); }});
                if (m_scriptDoc->changed())
                    m_editor->applyProject(m_scriptDoc->project());
                if (!result.ok) {
                    m_scriptConsole->printError(QString::fromStdString(result.error) + QLatin1Char('\n'));
                    m_scriptDock->show();
                    showMessage(tr("%1 failed: see the script console").arg(name));
                }
            });
            m_pluginActions.push_back(action);
        }
    }
    return m_pluginActions;
}

void MainWindow::updatePluginButton()
{
    if (m_pluginButton)
        m_pluginButton->setVisible(!pluginActions().empty());
}

void MainWindow::reloadPlugins()
{
    startScripting();
    ScriptEngine& engine = ScriptEngine::instance();
    if (!engine.running())
        return;
    engine.clearPlugins();
    for (QAction* a : m_pluginActions)
        a->deleteLater();
    m_pluginActions.clear();
    m_scriptDoc->reset(m_editor->project());
    const auto errors = engine.loadPlugins(
        pluginFolder().toStdString(), m_scriptDoc,
        {[this](const std::string& s) { m_scriptConsole->print(QString::fromStdString(s)); },
         [this](const std::string& s) { m_scriptConsole->printError(QString::fromStdString(s)); }});
    for (const std::string& e : errors)
        m_scriptConsole->printError(QString::fromStdString(e) + QLatin1Char('\n'));
    if (!errors.empty())
        showMessage(tr("A plugin failed to load: see the script console"));
    // Loading a plugin changes nothing of the project.
    m_scriptDoc->clearChanged();
    updatePluginButton();
}

void MainWindow::runScriptFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Run Script"), lastDirectory(), tr("Python scripts (*.py)"));
    if (path.isEmpty())
        return;
    rememberDirectory(path);
    startScripting();
    settleModel();
    if (m_editor->mode() == EditorMode::Sketch)
        m_editor->closeSketch();
    m_scriptDoc->reset(m_editor->project());
    m_scriptConsole->print(tr("Running %1\n").arg(QFileInfo(path).fileName()));
    const auto result = ScriptEngine::instance().runFile(
        path.toStdString(), m_scriptDoc,
        {[this](const std::string& s) { m_scriptConsole->print(QString::fromStdString(s)); },
         [this](const std::string& s) { m_scriptConsole->printError(QString::fromStdString(s)); }});
    if (m_scriptDoc->changed())
        m_editor->applyProject(m_scriptDoc->project());
    updatePluginButton();
    if (!result.ok) {
        m_scriptConsole->printError(QString::fromStdString(result.error) + QLatin1Char('\n'));
        m_scriptDock->show();
        showMessage(tr("%1 failed: see the script console").arg(QFileInfo(path).fileName()));
    } else {
        showMessage(tr("Ran %1").arg(QFileInfo(path).fileName()));
    }
}

void MainWindow::toggleMacro(bool on)
{
    if (on) {
        settleModel();
        if (m_editor->mode() == EditorMode::Sketch)
            m_editor->closeSketch();
        m_recordingFrom = std::make_shared<ProjectData>(m_editor->project());
        showMessage(tr("Recording a macro: add sketches and features, then Scripts → Record Macro again"));
    } else if (m_recordingFrom) {
        if (m_editor->mode() == EditorMode::Sketch)
            m_editor->closeSketch(); // a sketch being drawn is part of it
        const std::string script = macroScript(*m_recordingFrom, m_editor->project());
        m_recordingFrom.reset();
        QDir().mkpath(pluginFolder());
        const QString path = QFileDialog::getSaveFileName(this, tr("Save Macro"), pluginFolder() + QStringLiteral("/macro.py"),
                                                          tr("Python scripts (*.py)"));
        if (!path.isEmpty()) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(QByteArray::fromStdString(script)) == qint64(script.size())) {
                showMessage(tr("Saved the macro to %1").arg(QFileInfo(path).fileName()));
                if (QFileInfo(path).absolutePath() == QFileInfo(pluginFolder()).absoluteFilePath())
                    reloadPlugins(); // in the plugin folder: a command now
            } else {
                QMessageBox::warning(this, tr("Can't Save"), tr("Could not write %1").arg(path));
            }
        }
    }
    if (m_recordAction) {
        const QSignalBlocker block(m_recordAction);
        m_recordAction->setChecked(on);
    }
    m_scriptConsole->setRecording(on);
}

void MainWindow::trustProjectScripts(bool ask)
{
    const std::vector<Feature> features = m_editor->project().features;
    const bool any = std::any_of(features.begin(), features.end(), [](const Feature& f) { return f.kind == Feature::Kind::Script; });
    if (!any) {
        if (!ask)
            showMessage(tr("This project has no scripts"));
        return;
    }
    startScripting();
    ScriptEngine& engine = ScriptEngine::instance();
    const auto untrusted = engine.untrusted(features);
    if (untrusted.empty()) {
        if (!ask)
            showMessage(tr("This project's scripts already run"));
        return;
    }
    QMessageBox box(QMessageBox::Warning, tr("Run This Project's Scripts?"),
                    tr("This project has generated features: Python code kept in the file, which runs on this "
                       "computer with your rights when the model is built.\n\n"
                       "Let it run only if you trust where the project comes from. Until then those features "
                       "are left out of the model."),
                    QMessageBox::NoButton, this);
    QPushButton* allow = box.addButton(tr("Run the Scripts"), QMessageBox::AcceptRole);
    QPushButton* notNow = box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    box.setDefaultButton(notNow);
    box.exec();
    if (box.clickedButton() != allow)
        return;
    for (const std::string& source : untrusted)
        engine.trust(source);
    m_editor->rebuildFresh();
}

#else // no Python: the menu says so

void MainWindow::buildScriptConsole() {}
void MainWindow::startScripting() {}
void MainWindow::runScriptFile() {}
void MainWindow::toggleMacro(bool) {}
void MainWindow::reloadPlugins() {}
void MainWindow::trustProjectScripts(bool) {}
void MainWindow::updatePluginButton() {}
std::vector<QAction*> MainWindow::pluginActions()
{
    return {};
}

void MainWindow::buildScriptsMenu()
{
    m_scriptsMenu = menuBar()->addMenu(tr("&Scripts"));
    m_scriptsMenu->addAction(tr("This build has no Python scripting"))->setEnabled(false);
}

#endif
