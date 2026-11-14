// Parameters: expressions with units, the parameter table (cycles, renames,
// configurations, CSV) and values of the model following it.

#include "Check.h"

#include "model/Features.h"
#include "model/Parameters.h"
#include "sketch/Solver.h"

#include <clocale>
#include <map>

namespace {

std::optional<double> eval(const std::string& text, ValueKind kind = ValueKind::Length,
                           LengthUnit unit = LengthUnit::Millimetre, const NameLookup& names = {})
{
    return evaluate(text, kind, unit, names).value;
}

std::string errorOf(const std::string& text, ValueKind kind = ValueKind::Length, const NameLookup& names = {})
{
    return evaluate(text, kind, LengthUnit::Millimetre, names).error.message;
}

Parameter param(const std::string& name, const std::string& text, ValueKind kind = ValueKind::Length,
                LengthUnit unit = LengthUnit::Millimetre)
{
    return {name, {text, unit}, kind, {}};
}

void testNumbersAndArithmetic()
{
    CHECK(eval("12.5") == 12.5);
    CHECK(eval("12,5") == 12.5); // a decimal comma
    CHECK(eval("2 + 3 * 4") == 14.0);
    CHECK(eval("(2 + 3) * 4") == 20.0);
    CHECK(eval("-2^2") == -4.0);
    CHECK(eval("2^3^2") == 512.0);
    CHECK(eval("10 / 4") == 2.5);
    CHECK(eval("1e2") == 100.0);
    CHECK(eval(".5") == 0.5);
    CHECK(eval("12.") == 12.0);
    CHECK(eval("3 × 2 ÷ 4") == 1.5);
    CHECK(eval("max(2,5; 1)") == 5.0); // inside a function a comma separates
    CHECK(eval("max(2;5)") == 5.0);
    CHECK(eval("min(4, 2,5)") == 2.0);
    CHECK(near(*eval("2 * pi", ValueKind::Number), 2 * M_PI));
    CHECK(eval("sqrt(16)") == 4.0);
    CHECK(eval("round(2.6) + floor(1.9) + ceil(0.1) + abs(-1)") == 6.0);
}

void testUnits()
{
    CHECK(eval("1 in") == 25.4);
    CHECK(eval("2cm + 3 mm") == 23.0);
    CHECK(eval("1 m") == 1000.0);
    CHECK(eval("2", ValueKind::Length, LengthUnit::Inch) == 50.8); // plain numbers in the typed unit
    CHECK(eval("1 in + 1", ValueKind::Length, LengthUnit::Inch) == 50.8);
    CHECK(eval("10 mm + 1", ValueKind::Length, LengthUnit::Inch) == 35.4);
    CHECK(eval("10 mm * 2") == 20.0); // a factor stays a factor
    CHECK(eval("45°", ValueKind::Angle) == 45.0);
    CHECK(near(*eval("pi rad", ValueKind::Angle), 180.0));
    CHECK(eval("90 deg / 2", ValueKind::Angle) == 45.0);
    CHECK(near(*eval("sin(30)", ValueKind::Number), 0.5));
    CHECK(near(*eval("cos(60 deg)", ValueKind::Number), 0.5));
    CHECK(near(*eval("atan(1)", ValueKind::Angle), 45.0));
    CHECK(eval("sqrt(9 mm * 16 mm)") == 12.0);
    CHECK(eval("(2 mm)^2 / 4 mm") == 1.0);
    CHECK(eval("mm") == 1.0);
}

void testErrors()
{
    CHECK(errorOf("") == "Empty expression");
    CHECK(errorOf("2 +") == "Unexpected end");
    CHECK(errorOf("2 + * 3") == "Unexpected %1");
    CHECK(errorOf("(2") == "Unexpected end");
    CHECK(errorOf("2)") == "Unexpected %1");
    CHECK(errorOf("2 $") == "Unexpected %1");
    CHECK(errorOf("width") == "Unknown name: %1");
    CHECK(evaluate("width", ValueKind::Length, LengthUnit::Millimetre, {}).error.detail == "width");
    CHECK(errorOf("2 mm + 3 deg") == "Units don't match");
    CHECK(errorOf("2 mm * 3 mm") == "Expected a length");
    CHECK(errorOf("2 mm", ValueKind::Angle) == "Expected an angle");
    CHECK(errorOf("2 mm", ValueKind::Number) == "Expected a plain number");
    CHECK(errorOf("1 / 0") == "Division by zero");
    CHECK(errorOf("sqrt(-1)") == "Square root of a negative value");
    CHECK(errorOf("sqrt(2 mm)") == "Square root of a unit that isn't squared");
    CHECK(errorOf("sin(2 mm)") == "%1 takes an angle");
    CHECK(errorOf("sqrt") == "%1 needs its values in parentheses");
    CHECK(errorOf("2 ^ 1 mm") == "An exponent can't have a unit");
    CHECK(errorOf("(2 mm) ^ 0.5") == "A unit can only be raised to a whole power");
    CHECK(errorOf("10^400") == "Not a number");
    CHECK(errorOf(std::string(5000, '(') + "1" + std::string(5000, ')')) == "Too deeply nested");
    CHECK(errorOf(std::string(5000, '-') + "1") == "Too deeply nested");
}

void testNames()
{
    CHECK(isPlainNumber("12"));
    CHECK(isPlainNumber(" -12,5 "));
    CHECK(isPlainNumber(".5"));
    CHECK(!isPlainNumber("1.2.3"));
    CHECK(!isPlainNumber("2*3"));
    CHECK(!isPlainNumber("-"));
    CHECK(!isPlainNumber("width"));

    CHECK(isValidName("width"));
    CHECK(isValidName("_w2"));
    CHECK(isValidName("szerokość"));
    CHECK(!isValidName("2w"));
    CHECK(!isValidName("a b"));
    CHECK(!isValidName("a-b"));
    CHECK(!isValidName("mm"));
    CHECK(!isValidName("sin"));
    CHECK(!isValidName("pi"));
    CHECK(!isValidName(""));

    CHECK((namesIn("width / 2 + max(t; mm) + width") == std::vector<std::string>{"width", "t"}));
    CHECK(renameInExpression("w + w2 + w*2", "w", "width") == "width + w2 + width*2");
}

void testParameterTable()
{
    ParameterTable table;
    table.parameters = {param("half", "width / 2"), param("width", "40"), param("inches", "2", ValueKind::Length, LengthUnit::Inch),
                        param("n", "3", ValueKind::Number), param("turn", "360 / n", ValueKind::Angle),
                        param("bad", "half * mm"), param("a", "b + 1"), param("b", "a + 1"), param("c", "a"),
                        param("d", "nothing")};
    const ParameterScope scope(table);
    const auto& v = scope.values();
    CHECK(v[0].value == 20.0); // uses one further down
    CHECK(v[1].value == 40.0);
    CHECK(v[2].value == 50.8);
    CHECK(v[3].value == 3.0);
    CHECK(v[4].value == 120.0);
    CHECK(!v[5].value && v[5].error.message == "Expected a length");
    CHECK(!v[6].value && !v[7].value); // a cycle
    CHECK(v[6].error.message == "Circular reference through %1" && v[6].error.detail == "b");
    CHECK(v[7].error.message == "Circular reference through %1");
    CHECK(!v[8].value && v[8].error.message == "Circular reference through %1" && v[8].error.detail == "a");
    CHECK(!v[9].value && v[9].error.message == "Unknown name: %1");

    CHECK(scope.evaluate({"width + 2", LengthUnit::Millimetre}, ValueKind::Length).value == 42.0);
    CHECK(scope.evaluate({"turn / 2", LengthUnit::Millimetre}, ValueKind::Angle).value == 60.0);
    CHECK(scope.evaluate({"n", LengthUnit::Millimetre}, ValueKind::Number).value == 3.0);
    CHECK(scope.evaluate({"turn", LengthUnit::Millimetre}, ValueKind::Length).error.message == "Expected a length");
    CHECK(scope.evaluate({"half + bad", LengthUnit::Millimetre}, ValueKind::Length).error.detail == "bad");

    // A self-reference and a long chain don't hang or overflow.
    ParameterTable chain;
    chain.parameters.push_back(param("p0", "p0 + 1"));
    for (int i = 1; i < 100000; ++i)
        chain.parameters.push_back(param("q" + std::to_string(i), i == 99999 ? "1" : "q" + std::to_string(i + 1) + " + 1"));
    const ParameterScope chained(chain);
    CHECK(!chained.values()[0].value && chained.values()[0].error.detail == "p0");
    CHECK(chained.values()[1].value == 99999.0);
}

void testRenameAndUses()
{
    ParameterTable table;
    table.parameters = {param("w", "40"), param("h", "w / 2")};
    saveConfiguration(table, "Small");
    std::vector<Feature> features(1);
    features[0].kind = Feature::Kind::Extrude;
    features[0].expressions["value"] = {"w + h", LengthUnit::Millimetre};
    Sketch sketch;
    const PointId a = sketch.addPoint({0, 0}), b = sketch.addPoint({10, 0});
    sketch.addLine(a, b);
    SketchConstraint d;
    d.kind = ConstraintKind::Distance;
    d.p1 = a;
    d.p2 = b;
    d.value = 10.0;
    d.expression = {"w", LengthUnit::Millimetre};
    const ConstraintId id = sketch.addConstraint(d);

    const ParameterUses uses = usesOf("w", table, features, {&sketch});
    CHECK(uses.parameters == std::vector<std::string>{"h"});
    CHECK(uses.values == 2);

    ExpressionError error;
    CHECK(!renameParameter(table, features, {&sketch}, "w", "h", &error));
    CHECK(error.message == "%1 is already taken");
    CHECK(!renameParameter(table, features, {&sketch}, "w", "mm", &error));
    CHECK(renameParameter(table, features, {&sketch}, "w", "width", &error));
    CHECK(table.parameters[0].name == "width");
    CHECK(table.parameters[1].expression.text == "width / 2");
    CHECK(features[0].expressions["value"].text == "width + h");
    CHECK(sketch.constraint(id)->expression.text == "width");
    CHECK(table.configurations[0].values.count("width") == 1 && table.configurations[0].values.count("w") == 0);
    CHECK(table.configurations[0].values["h"] == "width / 2");
}

void testDefinitionsAndManualValues()
{
    CHECK(definitionIn("width = 40").has_value());
    CHECK(definitionIn("width=40")->name == "width");
    CHECK(definitionIn(" w = a / 2 ")->expression == "a / 2");
    CHECK(!definitionIn("40"));
    CHECK(!definitionIn("a + b = 3"));
    CHECK(!definitionIn("a = b = 3"));
    CHECK(numberText(40.0) == "40");
    CHECK(numberText(2.54) == "2.54");
    CHECK(numberText(-0.0000001) == "0");
    CHECK(numberText(1.0 / 3.0) == "0.333333");

    ParameterTable table;
    ExpressionError error;
    CHECK(setParameterValue(table, "width", ValueKind::Length, 40.0, LengthUnit::Millimetre));
    CHECK(table.parameters.size() == 1 && table.parameters[0].expression.text == "40");
    CHECK(setParameterValue(table, "t", ValueKind::Length, 25.4 / 8, LengthUnit::Inch));
    CHECK(table.parameters[1].expression.text == "0.125"); // in its own unit
    CHECK(setParameterValue(table, "width", ValueKind::Length, 55.5, LengthUnit::Inch)); // kept in mm
    CHECK(table.parameters[0].expression == (Expression{"55.5", LengthUnit::Millimetre}));
    CHECK(!setParameterValue(table, "width", ValueKind::Angle, 30.0, LengthUnit::Millimetre, &error));
    CHECK(error.message == "%1 is a length");
    CHECK(!setParameterValue(table, "mm", ValueKind::Length, 1.0, LengthUnit::Millimetre, &error));
    CHECK(table.parameters.size() == 2);
}

void testConfigurations()
{
    ParameterTable table;
    table.parameters = {param("w", "40"), param("h", "20")};
    saveConfiguration(table, "Small");
    CHECK(table.active == "Small");
    CHECK(!configurationModified(table));
    table.parameters[0].expression.text = "80";
    table.parameters[1].expression.text = "30";
    CHECK(configurationModified(table));
    saveConfiguration(table, "Large");
    CHECK(table.configurations.size() == 2);
    CHECK(applyConfiguration(table, "Small"));
    CHECK(table.parameters[0].expression.text == "40" && table.parameters[1].expression.text == "20");
    CHECK(table.active == "Small");
    CHECK(applyConfiguration(table, "Large"));
    CHECK(table.parameters[0].expression.text == "80");
    CHECK(!applyConfiguration(table, "Medium"));
    // Saving under an existing name replaces it.
    table.parameters[0].expression.text = "90";
    saveConfiguration(table, "Large");
    CHECK(table.configurations.size() == 2 && table.configurations[1].values["w"] == "90");
}

void testCsv()
{
    ParameterTable table;
    table.parameters = {param("w", "40"), param("t", "max(1, 2)", ValueKind::Length, LengthUnit::Inch),
                        param("a", "30", ValueKind::Angle), param("n", "4", ValueKind::Number)};
    table.parameters[0].comment = "the \"outer\" width; with commas, too";
    saveConfiguration(table, "Small");
    table.parameters[0].expression.text = "80";
    saveConfiguration(table, "Large, wide");
    const std::string csv = parametersToCsv(table);
    const auto back = parametersFromCsv(csv);
    CHECK(back.has_value());
    if (back) {
        CHECK(back->parameters == table.parameters);
        CHECK(back->configurations == table.configurations);
    }

    // Spreadsheet flavours: semicolons, a byte order mark, CRLF, blank lines, capitals.
    const auto excel = parametersFromCsv("\xEF\xBB\xBFName;Unit;Expression;Comment;S\r\nw;mm;12,5;;10\r\n\r\nk;;2;x;\r\n");
    CHECK(excel.has_value());
    if (excel) {
        CHECK(excel->parameters.size() == 2);
        CHECK(excel->parameters[0].expression.text == "12,5");
        CHECK(excel->parameters[1].kind == ValueKind::Number);
        CHECK(excel->configurations.size() == 1 && excel->configurations[0].values.size() == 1);
    }

    ExpressionError error;
    CHECK(!parametersFromCsv("", &error));
    CHECK(!parametersFromCsv("a,b,c\n", &error));
    CHECK(!parametersFromCsv("name,unit,expression,comment\nmm,mm,1,\n", &error));
    CHECK(error.message == "Line %1: not a valid parameter name" && error.detail == "2");
    CHECK(!parametersFromCsv("name,unit,expression,comment\nw,mm,1,\nw,mm,2,\n", &error));
    CHECK(error.detail == "3");
    CHECK(!parametersFromCsv("name,unit,expression,comment\nw,furlong,1,\n", &error));
    CHECK(!parametersFromCsv("name,unit,expression,comment\nw,mm,,\n", &error));
    CHECK(!parametersFromCsv("name,unit,expression,comment,A,A\n", &error));
    CHECK(parametersFromCsv("name,unit,expression\nw,mm,\"unterminated\n").has_value() == false);
}

// --- The model following -------------------------------------------------------------------------

struct Box
{
    Sketch sketch;
    ConstraintId width = kNoId, depth = kNoId;
};

// A rectangle from the origin, its width and depth dimensioned.
Box rectangle(double w, double d)
{
    Box box;
    Sketch& s = box.sketch;
    const PointId a = s.addPoint({0, 0}), b = s.addPoint({w, 0}), c = s.addPoint({w, d}), e = s.addPoint({0, d});
    const EntityId ab = s.addLine(a, b), bc = s.addLine(b, c), ce = s.addLine(c, e), ea = s.addLine(e, a);
    auto add = [&s](ConstraintKind kind, PointId p1, PointId p2, EntityId e1, double value = 0.0) {
        SketchConstraint k;
        k.kind = kind;
        k.p1 = p1;
        k.p2 = p2;
        k.e1 = e1;
        k.value = value;
        if (kind == ConstraintKind::Fixed)
            k.target = s.pos(p1);
        return s.addConstraint(k);
    };
    add(ConstraintKind::Fixed, a, kNoId, kNoId);
    add(ConstraintKind::Horizontal, a, b, ab);
    add(ConstraintKind::Horizontal, c, e, ce);
    add(ConstraintKind::Vertical, b, c, bc);
    add(ConstraintKind::Vertical, e, a, ea);
    box.width = add(ConstraintKind::Distance, a, b, ab, w);
    box.depth = add(ConstraintKind::Distance, b, c, bc, d);
    CHECK(solve(s).ok);
    return box;
}

double volumeOf(const std::map<int, Sketch>& sketches, const std::vector<Feature>& features)
{
    const SketchLookup lookup = [&sketches](int id) -> const Sketch* {
        const auto it = sketches.find(id);
        return it == sketches.end() ? nullptr : &it->second;
    };
    const RebuildResult r = rebuild(lookup, features);
    double v = 0.0;
    for (const TopoDS_Shape& b : r.bodies)
        v += volumeOf(b);
    return v;
}

void testModelFollowsParameters()
{
    // A box with a lid for a board: the box's outline and height and the lid's thickness
    // come from three parameters.
    ParameterTable table;
    table.parameters = {param("board", "60"), param("wall", "2"), param("height", "20"),
                        param("outer", "board + 2 * wall"), param("depth", "outer * 2 / 3")};
    Box box = rectangle(10, 10);
    box.sketch.constraint(box.width)->expression = {"outer", LengthUnit::Millimetre};
    box.sketch.constraint(box.depth)->expression = {"depth", LengthUnit::Millimetre};
    std::map<int, Sketch> sketches{{1, box.sketch}};

    std::vector<Feature> features(2);
    features[0].kind = Feature::Kind::Sketch;
    features[0].sketch = 1;
    features[1].kind = Feature::Kind::Extrude;
    features[1].id = 1;
    features[1].regions = {{1, gp_Pnt2d(1, 1)}};
    features[1].value = 5.0;
    features[1].expressions["value"] = {"height", LengthUnit::Millimetre};

    auto apply = [&] {
        return applyParameters(table, features, {{1, &sketches[1]}});
    };
    ApplyResult r = apply();
    CHECK(r.issues.empty());
    CHECK(r.features && r.sketches == std::vector<int>{1});
    CHECK(features[1].value == 20.0);
    CHECK(near(volumeOf(sketches, features), 64.0 * 64.0 * 2.0 / 3.0 * 20.0, 1e-3));
    // Nothing changed: nothing to do.
    r = apply();
    CHECK(!r.features && r.sketches.empty());

    saveConfiguration(table, "Small");
    table.parameters[0].expression.text = "100";
    table.parameters[2].expression.text = "35";
    saveConfiguration(table, "Large");
    CHECK(applyConfiguration(table, "Small"));
    apply();
    const double small = volumeOf(sketches, features);
    CHECK(applyConfiguration(table, "Large"));
    r = apply();
    CHECK(r.issues.empty());
    CHECK(near(volumeOf(sketches, features), 104.0 * 104.0 * 2.0 / 3.0 * 35.0, 1e-3));
    CHECK(applyConfiguration(table, "Small"));
    apply();
    CHECK(near(volumeOf(sketches, features), small, 1e-6));

    // A value out of range or a broken expression keeps the last value and says why.
    table.parameters[2].expression.text = "-5 + nothing";
    r = apply();
    CHECK(r.issues.size() == 1 && r.issues[0].feature == 1 && r.issues[0].key == "value");
    CHECK(r.issues[0].error.message == "%1 has an error" && r.issues[0].error.detail == "height");
    CHECK(features[1].value == 20.0);
    table.parameters[2].expression.text = "20";
    table.parameters[0].expression.text = "-100"; // the outline can't be negative
    r = apply();
    CHECK(r.issues.size() == 2 && r.issues[0].sketch == 1 && r.issues[0].error.message == "Out of range: %1");
    CHECK(near(measure(sketches[1], *sketches[1].constraint(box.width)), 64.0, 1e-6));
}

void testFeatureFields()
{
    Feature pattern;
    pattern.kind = Feature::Kind::Pattern;
    pattern.pattern = Feature::PatternKind::Circular;
    pattern.expressions["count"] = {"n * 2", LengthUnit::Millimetre};
    pattern.expressions["value"] = {"360 / n", LengthUnit::Millimetre};
    Feature hole;
    hole.kind = Feature::Kind::Hole;
    hole.expressions["hole.diameter"] = {"m3", LengthUnit::Millimetre};
    Feature combine;
    combine.kind = Feature::Kind::Combine;
    combine.expressions["value"] = {"1", LengthUnit::Millimetre};
    std::vector<Feature> features{pattern, hole, combine};

    ParameterTable table;
    table.parameters = {param("n", "3", ValueKind::Number), param("m3", "3.2")};
    ApplyResult r = applyParameters(table, features, {});
    CHECK(features[0].count == 6);
    CHECK(features[0].value == 120.0);
    CHECK(features[1].hole.diameter == 3.2);
    CHECK(r.issues.size() == 1 && r.issues[0].feature == 2);

    table.parameters[0].expression.text = "2.5";
    r = applyParameters(table, features, {});
    CHECK(features[0].count == 5); // 2.5 * 2 is whole
    table.parameters[0].expression.text = "2.25";
    r = applyParameters(table, features, {});
    CHECK(features[0].count == 5);
    CHECK(r.issues.size() == 2 && r.issues[0].error.message == "Expected a whole number");
    table.parameters[0].expression.text = "0.5"; // a count of 1, and a turn of 720°
    r = applyParameters(table, features, {});
    CHECK(features[0].count == 5 && features[0].value == 360.0 / 2.25);
    CHECK(r.issues.size() == 3);

    // A dimension keeps its side; an angle goes to radians.
    CHECK(dimensionValue(ConstraintKind::PointLineDistance, 5.0, -2.0) == -5.0);
    CHECK(near(dimensionValue(ConstraintKind::Angle, 90.0, 0.3), M_PI / 2.0));
}

} // namespace

int main()
{
    // As the app runs: Qt sets the system's locale, where a decimal comma is usual. Numbers
    // must read and print the same anyway.
    std::setlocale(LC_ALL, "pl_PL.UTF-8");
    return runTests({
        {"numbers and arithmetic", testNumbersAndArithmetic},
        {"units", testUnits},
        {"errors", testErrors},
        {"names", testNames},
        {"parameter table", testParameterTable},
        {"rename and uses", testRenameAndUses},
        {"definitions and manual values", testDefinitionsAndManualValues},
        {"configurations", testConfigurations},
        {"csv", testCsv},
        {"model follows parameters", testModelFollowsParameters},
        {"feature fields", testFeatureFields},
    });
}
