#pragma once

#include "model/Features.h"
#include "script/ScriptDocument.h"

#include <TopoDS_Shape.hxx>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Scripting (M19): Python embedded in the app, with the `nibform` module (see
// docs/scripting.md). One interpreter for the process; everything here is called from
// the window's thread, except generators, which rebuilds also run on their own thread.
class ScriptEngine
{
public:
    static ScriptEngine& instance();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    // Starts Python (once) and hooks generators into rebuilds. False (with `error`) if it can't.
    bool start(std::string* error = nullptr);
    // Ends Python: before the app quits (and the objects it holds go).
    void stop();
    bool running() const { return m_running; }

    struct Output
    {
        std::function<void(const std::string&)> out, err; // what the script prints
    };
    struct Result
    {
        bool ok = true;
        std::string error; // the exception, with where it was raised
        int exitCode = 0;  // sys.exit()'s, for `nibform --run`
    };
    // Runs Python code (`filename` shows in tracebacks), nf.document() being `document`.
    // `scope` keeps the variables between runs (the console's); empty: a fresh one.
    // `argv` becomes sys.argv.
    Result run(const std::string& code, const std::string& filename, std::shared_ptr<ScriptDocument> document,
               const Output& output = {}, const std::string& scope = {}, const std::vector<std::string>& argv = {});
    Result runFile(const std::string& path, std::shared_ptr<ScriptDocument> document, const Output& output = {},
                   const std::vector<std::string>& argv = {});
    // Whether the code parses as one statement / expression yet (the console's continuation lines).
    bool complete(const std::string& code);

    // --- Generators (features made by a script) ---------------------------------------------
    struct Generator
    {
        std::string name;
        std::shared_ptr<const std::string> source;
        std::vector<ScriptValue> values; // their defaults
        std::string help;
    };
    std::vector<Generator> generators() const { return m_generators; }
    void addGenerator(Generator g);
    // A rebuild's call: the solid, or empty with `error`.
    std::optional<TopoDS_Shape> generate(const ScriptCall& call, std::string& error);

    // Scripts kept in a project from elsewhere run only once trusted: those written in this
    // session are, the rest after the user says so (or everything, for `nibform --run`).
    void trust(const std::string& source);
    bool trusted(const std::string& source) const;
    void trustEverything(bool on) { m_trustAll = on; }
    // The project's scripts not trusted yet.
    std::vector<std::string> untrusted(const std::vector<Feature>& features) const;

    // --- Commands (added by plugins and scripts) --------------------------------------------
    struct Command
    {
        std::string id, name, help;
    };
    std::vector<Command> commands() const { return m_commands; }
    std::string addCommand(const std::string& name, const std::string& help); // its id
    Result runCommand(const std::string& id, std::shared_ptr<ScriptDocument> document, const Output& output = {});
    // Runs every .py file in the folder: each adds commands and generators (a file that adds
    // none is a command itself, running it). The errors, by file.
    std::vector<std::string> loadPlugins(const std::string& folder, std::shared_ptr<ScriptDocument> document,
                                         const Output& output = {});
    void clearPlugins();

    // What nf.document() gives while nothing runs (a new project, for headless runs).
    std::shared_ptr<ScriptDocument> current() const { return m_current; }
    void setCurrent(std::shared_ptr<ScriptDocument> document) { m_current = std::move(document); }

private:
    ScriptEngine() = default;
    ~ScriptEngine();
    friend struct ScriptEngineAccess;

    bool m_running = false;
    void* m_threadState = nullptr; // the window's thread state while it doesn't hold the GIL
    bool m_trustAll = false;
    std::set<std::size_t> m_trusted; // hashes of trusted sources
    std::vector<Generator> m_generators;
    std::vector<Command> m_commands;
    std::shared_ptr<ScriptDocument> m_current;
    int m_nextCommand = 1;
};

// A recorded macro: the script that adds again what was added to the project between
// `before` and `after` (new sketches, features and parameters).
std::string macroScript(const ProjectData& before, const ProjectData& after);
