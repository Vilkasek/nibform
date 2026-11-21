// Scripting (M19): the Python API on a project, generators as features of the history
// (the spur gear of examples/gear.py), trust of scripts from elsewhere, the batch export
// of every configuration (examples/export_configurations.py), recorded macros.

#include "Check.h"

#include "io/ProjectFile.h"
#include "model/Export.h"
#include "model/Parameters.h"
#include "script/ScriptEngine.h"

#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <clocale>
#include <cmath>
#include <string>

namespace {

const std::string kExamples = std::string(NIBFORM_SOURCE_DIR) + "/examples/";

ScriptEngine& engine()
{
    return ScriptEngine::instance();
}

std::string g_out, g_err;
ScriptEngine::Output capture()
{
    g_out.clear();
    g_err.clear();
    return {[](const std::string& s) { g_out += s; }, [](const std::string& s) { g_err += s; }};
}

// Across the tips of a gear about the Z axis: twice its farthest corner from the axis.
double diameterOf(const TopoDS_Shape& shape)
{
    double far = 0.0;
    for (TopExp_Explorer it(shape, TopAbs_VERTEX); it.More(); it.Next()) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(it.Current()));
        far = std::max(far, std::hypot(p.X(), p.Y()));
    }
    return 2.0 * far;
}

void testConsoleBasics()
{
    auto doc = std::make_shared<ScriptDocument>();
    auto r = engine().run("x = 6 * 7\nprint('hello', x)", "<test>", doc, capture(), "console");
    CHECK(r.ok && g_out == "hello 42\n");
    // The console keeps its variables, and shows an expression's value.
    r = engine().run("x + 1", "<test>", doc, capture(), "console");
    CHECK(r.ok && g_out == "43\n");
    // Errors come back with where they happened.
    r = engine().run("1 / 0", "<test>", doc, capture(), "console");
    CHECK(!r.ok && r.error.find("ZeroDivisionError") != std::string::npos);
    CHECK(!engine().complete("def f():"));
    CHECK(engine().complete("def f():\n    return 1\n\n"));
    CHECK(engine().complete("x = 1"));
}

void testSketchesAndFeatures()
{
    auto doc = std::make_shared<ScriptDocument>();
    const auto r = engine().run(R"PY(
import nibform as nf
doc = nf.document()
doc.set_parameter("width", 40)
s = doc.sketch("XY")
bottom, right, top, left = s.rectangle(0, 0, 40, 20)
s.horizontal(bottom)
s.horizontal(top)
s.vertical(left)
s.vertical(right)
s.fix(s.ends(bottom)[0])
s.length(bottom, "width")
plate = doc.extrude(s, 5)
hole = doc.hole(nf.face((20, 10, 5), (0, 0, 1)), [(10, 10, 5)], 4)
assert not doc.errors, doc.errors
print(len(doc.bodies), round(doc.bodies[0].volume, 3))
)PY",
                                "<test>", doc, capture());
    if (!r.ok)
        std::printf("    %s\n", r.error.c_str());
    CHECK(r.ok);
    const double expected = 40.0 * 20.0 * 5.0 - M_PI * 4.0 * 5.0;
    CHECK(g_out.find("1 ") == 0 && std::abs(std::stod(g_out.substr(2)) - expected) < 1e-2);
    CHECK(doc->changed());
    // The width follows the parameter.
    ExpressionError error;
    CHECK(setParameterValue(doc->parameters(), "width", ValueKind::Length, 60.0, LengthUnit::Millimetre, &error));
    doc->applyParameters();
    CHECK(std::abs(volumeOf(doc->bodies().at(0)) - (60.0 * 20.0 * 5.0 - M_PI * 4.0 * 5.0)) < 1e-2);
}

void testGearGenerator()
{
    auto doc = std::make_shared<ScriptDocument>();
    auto r = engine().runFile(kExamples + "gear.py", doc, capture());
    if (!r.ok)
        std::printf("    %s\n", r.error.c_str());
    CHECK(r.ok);
    CHECK(doc->bodies().size() == 1);
    // m (z + 2) across the tips.
    CHECK(std::abs(diameterOf(doc->bodies().at(0)) - 2.0 * 22.0) < 1e-3);
    const Feature& gear = doc->features().back();
    CHECK(gear.kind == Feature::Kind::Script && gear.script.name == "spur_gear" && gear.label == "Spur gear");
    CHECK(gear.expressions.count("values.teeth") == 1);

    // An editable feature: the tooth count from its parameter, the module typed in.
    ExpressionError error;
    CHECK(setParameterValue(doc->parameters(), "teeth", ValueKind::Number, 30.0, LengthUnit::Millimetre, &error));
    doc->applyParameters();
    CHECK(std::abs(diameterOf(doc->bodies().at(0)) - 2.0 * 32.0) < 1e-3);
    Feature& f = doc->features().back();
    f.expressions.erase("values.module");
    setFeatureValue(f, "values.module", 1.5);
    doc->touch();
    CHECK(std::abs(diameterOf(doc->bodies().at(0)) - 1.5 * 32.0) < 1e-3);

    // Saved and opened again: the generator is kept in the project and runs there.
    const QByteArray json = projectToJson(doc->project());
    const auto loaded = projectFromJson(json);
    CHECK(loaded.has_value());
    if (!loaded)
        return;
    ScriptDocument again(*loaded);
    CHECK(again.errors().back().empty());
    CHECK(std::abs(diameterOf(again.bodies().at(0)) - 1.5 * 32.0) < 1e-3);
}

void testScriptsFromElsewhereNeedTrust()
{
    // A project made somewhere else, with a generator this session never saw.
    ProjectData project;
    Feature f;
    f.kind = Feature::Kind::Script;
    f.operation = BodyOperation::NewBody;
    f.script.source = std::make_shared<const std::string>("def cube(size):\n    return nf.box(size, size, size)\n");
    f.script.name = "cube";
    f.script.values = {{"size", 10.0, ValueKind::Length}};
    project.features = {f};
    numberFeatures(project.features);
    ScriptDocument doc(project);
    CHECK(engine().untrusted(doc.project().features).size() == 1);
    CHECK(doc.bodies().empty() && doc.errors().front().find("trusted") != std::string::npos);
    engine().trust(*f.script.source);
    ScriptDocument trusted(project);
    CHECK(trusted.errors().front().empty() && std::abs(volumeOf(trusted.bodies().at(0)) - 1000.0) < 1e-6);

    // A generator that fails says why, on its feature.
    project.features[0].script.source = std::make_shared<const std::string>("def cube(size):\n    return 1 / 0\n");
    engine().trust(*project.features[0].script.source);
    ScriptDocument broken(project);
    CHECK(broken.errors().front().find("ZeroDivisionError") != std::string::npos);
}

void testBatchExportOfConfigurations()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const std::string folder = dir.path().toStdString();
    // A block whose length follows a parameter, in two configurations.
    auto doc = std::make_shared<ScriptDocument>();
    auto r = engine().run(R"PY(
import nibform as nf, sys
doc = nf.document()
doc.set_parameter("length", 20)
s = doc.sketch("XY")
s.rectangle(0, 0, 10, 10)
f = doc.extrude(s, "length")
doc.save_configuration("Short")
doc.set_parameter("length", 50)
doc.save_configuration("Long")
doc.save(sys.argv[1])
)PY",
                          "<test>", doc, capture(), {}, {"make", folder + "/block.nibform"});
    if (!r.ok)
        std::printf("    %s\n", r.error.c_str());
    CHECK(r.ok);
    r = engine().runFile(kExamples + "export_configurations.py", nullptr, capture(),
                         {"export_configurations.py", folder + "/block.nibform", folder + "/out"});
    if (!r.ok)
        std::printf("    %s\n", r.error.c_str());
    CHECK(r.ok);
    for (const auto& [name, length] : {std::pair("Short", 20.0), std::pair("Long", 50.0)}) {
        const std::string path = folder + "/out/block-" + name + ".step";
        std::string error;
        const auto solids = importStep(path, &error);
        CHECK(solids.size() == 1);
        if (solids.size() == 1)
            CHECK(std::abs(volumeOf(solids[0]) - 100.0 * length) < 1e-3);
    }
}

void testMacroReplays()
{
    // What was made while recording...
    auto doc = std::make_shared<ScriptDocument>();
    const ProjectData before = doc->project();
    auto r = engine().run(R"PY(
import nibform as nf
doc = nf.document()
doc.set_parameter("h", 12)
s = doc.sketch("XY")
s.circle((0, 0), 5)
doc.extrude(s, "h")
)PY",
                          "<test>", doc, capture());
    CHECK(r.ok);
    const std::string macro = macroScript(before, doc->project());
    // ...made again in another project.
    auto other = std::make_shared<ScriptDocument>();
    r = engine().run(macro, "<macro>", other, capture());
    if (!r.ok)
        std::printf("    %s\n%s\n", r.error.c_str(), macro.c_str());
    CHECK(r.ok);
    CHECK(other->bodies().size() == 1);
    if (!other->bodies().empty())
        CHECK(std::abs(volumeOf(other->bodies()[0]) - M_PI * 25.0 * 12.0) < 1e-3);
    CHECK(other->project().parameters.indexOf("h") >= 0);
}

void testPlugins()
{
    QTemporaryDir dir;
    QFile command(dir.filePath("tools.py"));
    CHECK(command.open(QIODevice::WriteOnly));
    command.write("import nibform as nf\n\n@nf.command('Add a cube', help='A 10 mm cube')\ndef add_cube():\n"
                  "    s = nf.document().sketch('XY')\n    s.rectangle(0, 0, 10, 10)\n    nf.document().extrude(s, 10)\n");
    command.close();
    QFile plain(dir.filePath("say_hello.py"));
    CHECK(plain.open(QIODevice::WriteOnly));
    plain.write("print('hello')\n");
    plain.close();
    engine().clearPlugins();
    const auto errors = engine().loadPlugins(dir.path().toStdString(), nullptr, capture());
    CHECK(errors.empty());
    const auto commands = engine().commands();
    CHECK(commands.size() == 2);
    auto doc = std::make_shared<ScriptDocument>();
    for (const auto& c : commands) {
        const auto r = engine().runCommand(c.id, doc, capture());
        CHECK(r.ok);
        if (c.name == "Say hello")
            CHECK(g_out == "hello\n");
    }
    CHECK(doc->bodies().size() == 1);
    engine().clearPlugins();
}

} // namespace

int main()
{
    std::setlocale(LC_NUMERIC, "C");
    std::string error;
    if (!engine().start(&error)) {
        std::printf("Python didn't start: %s\n", error.c_str());
        return 1;
    }
    const int result = runTests({
        {"console basics", testConsoleBasics},
        {"sketches and features", testSketchesAndFeatures},
        {"the gear generator", testGearGenerator},
        {"scripts from elsewhere need trust", testScriptsFromElsewhereNeedTrust},
        {"batch export of configurations", testBatchExportOfConfigurations},
        {"a macro replays", testMacroReplays},
        {"plugins", testPlugins},
    });
    engine().stop();
    return result;
}
