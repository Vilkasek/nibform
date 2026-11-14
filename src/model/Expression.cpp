#include "model/Expression.h"

#include "model/Tr.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <set>

namespace {

constexpr double kMmPerInch = 25.4;
constexpr double kDegPerRad = 180.0 / M_PI;
constexpr int kMaxDepth = 100; // nested parentheses / operators, so odd input can't exhaust the stack
constexpr std::size_t kMaxNameLength = 64;

struct UnitDef
{
    const char* name;
    Quantity quantity;
};
const UnitDef kUnits[] = {
    {"mm", {1.0, 1, 0}},       {"cm", {10.0, 1, 0}},      {"m", {1000.0, 1, 0}},
    {"in", {kMmPerInch, 1, 0}}, {"inch", {kMmPerInch, 1, 0}}, {"ft", {304.8, 1, 0}},
    {"deg", {1.0, 0, 1}},      {"rad", {kDegPerRad, 0, 1}},
};
const char* const kFunctions[] = {"sqrt", "abs", "round", "floor", "ceil", "min", "max",
                                  "sin", "cos", "tan", "asin", "acos", "atan"};
const char* const kConstants[] = {"pi"};

std::optional<Quantity> unitNamed(const std::string& name)
{
    for (const UnitDef& u : kUnits) {
        if (name == u.name)
            return u.quantity;
    }
    return std::nullopt;
}

bool isFunction(const std::string& name)
{
    return std::any_of(std::begin(kFunctions), std::end(kFunctions), [&](const char* f) { return name == f; });
}

bool isReserved(const std::string& name)
{
    return unitNamed(name) || isFunction(name)
        || std::any_of(std::begin(kConstants), std::end(kConstants), [&](const char* c) { return name == c; });
}

// --- Reading -----------------------------------------------------------------------------

struct Token
{
    enum class Kind { Number, Name, Op, End };
    Kind kind = Kind::End;
    std::string text; // the name, the operator ("+", "(", "°", ...) or the number as written
    double number = 0.0;
    std::size_t begin = 0, end = 0; // in the source
};

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}
bool isAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// UTF-8 sequences read as operators (or spaces), so they never end up inside a name.
struct Symbol
{
    const char* utf8;
    const char* op; // empty: a space
};
const Symbol kSymbols[] = {{"\xC2\xB0", "°"}, {"\xC3\x97", "*"}, {"\xC2\xB7", "*"}, {"\xC3\xB7", "/"},
                           {"\xE2\x88\x92", "-"}, {"\xC2\xA0", ""}};

const Symbol* symbolAt(const std::string& s, std::size_t i)
{
    for (const Symbol& sym : kSymbols) {
        const std::size_t n = std::char_traits<char>::length(sym.utf8);
        if (s.compare(i, n, sym.utf8) == 0)
            return &sym;
    }
    return nullptr;
}

// Splits `s` into tokens; false (with `error`) at a character that has no place in an expression.
bool tokenize(const std::string& s, std::vector<Token>& tokens, ExpressionError& error)
{
    // For each open parenthesis: whether it holds a function's arguments (where a comma
    // separates them rather than being a decimal comma).
    std::vector<bool> parens;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (const Symbol* sym = symbolAt(s, i)) {
            const std::size_t n = std::char_traits<char>::length(sym->utf8);
            if (*sym->op)
                tokens.push_back({Token::Kind::Op, sym->op, 0.0, i, i + n});
            i += n;
            continue;
        }
        if (isDigit(c) || (c == '.' && i + 1 < s.size() && isDigit(s[i + 1]))) {
            const bool decimalComma = parens.empty() || !parens.back();
            std::string digits;
            const std::size_t begin = i;
            while (i < s.size() && isDigit(s[i]))
                digits += s[i++];
            if (i < s.size() && (s[i] == '.' || (s[i] == ',' && decimalComma && i + 1 < s.size() && isDigit(s[i + 1])))) {
                digits += '.';
                ++i;
                while (i < s.size() && isDigit(s[i]))
                    digits += s[i++];
            }
            // An exponent only when digits follow, so `2e` stays a number and a name.
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                std::size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-'))
                    ++j;
                if (j < s.size() && isDigit(s[j])) {
                    digits += 'e';
                    digits.append(s, i + 1, j - i - 1);
                    i = j;
                    while (i < s.size() && isDigit(s[i]))
                        digits += s[i++];
                }
            }
            // from_chars: whatever the locale (Qt sets the system's, where 12.5 may read as 12).
            double number = 0.0;
            const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
            if (ec == std::errc::result_out_of_range)
                number = HUGE_VAL; // too big: "Not a number" when evaluated
            tokens.push_back({Token::Kind::Number, s.substr(begin, i - begin), number, begin, i});
            continue;
        }
        if (isAsciiLetter(c) || static_cast<unsigned char>(c) >= 0x80) {
            const std::size_t begin = i;
            while (i < s.size() && (isAsciiLetter(s[i]) || isDigit(s[i]) || static_cast<unsigned char>(s[i]) >= 0x80)
                   && !symbolAt(s, i))
                ++i;
            tokens.push_back({Token::Kind::Name, s.substr(begin, i - begin), 0.0, begin, i});
            continue;
        }
        if (std::string("+-*/^();,").find(c) != std::string::npos) {
            if (c == '(') {
                parens.push_back(!tokens.empty() && tokens.back().kind == Token::Kind::Name
                                 && isFunction(tokens.back().text));
            } else if (c == ')' && !parens.empty()) {
                parens.pop_back();
            }
            tokens.push_back({Token::Kind::Op, std::string(1, c), 0.0, i, i + 1});
            ++i;
            continue;
        }
        error = {QT_TRANSLATE_NOOP("Model", "Unexpected %1"), std::string(1, c)};
        return false;
    }
    tokens.push_back({Token::Kind::End, std::string(), 0.0, s.size(), s.size()});
    return true;
}

// --- Evaluating ----------------------------------------------------------------------------

class Parser
{
public:
    Parser(const std::vector<Token>& tokens, LengthUnit unit, const NameLookup& names, ExpressionError& error)
        : m_tokens(tokens)
        , m_scale(unit == LengthUnit::Inch ? kMmPerInch : 1.0)
        , m_names(names)
        , m_error(error)
    {
    }

    std::optional<Quantity> parse()
    {
        if (peek().kind == Token::Kind::End)
            return fail(QT_TRANSLATE_NOOP("Model", "Empty expression"));
        auto q = sum();
        if (q && peek().kind != Token::Kind::End)
            return unexpected();
        if (q && !std::isfinite(q->value))
            return fail(QT_TRANSLATE_NOOP("Model", "Not a number"));
        return q;
    }

private:
    const Token& peek() const { return m_tokens[m_pos]; }
    bool isOp(const char* op) const { return peek().kind == Token::Kind::Op && peek().text == op; }
    std::nullopt_t fail(const char* message, std::string detail = {})
    {
        if (m_error.message.empty())
            m_error = {message, std::move(detail)};
        return std::nullopt;
    }
    std::nullopt_t unexpected()
    {
        if (peek().kind == Token::Kind::End)
            return fail(QT_TRANSLATE_NOOP("Model", "Unexpected end"));
        return fail(QT_TRANSLATE_NOOP("Model", "Unexpected %1"), peek().text);
    }

    // Brings a plain number to the dimension of `other` (a length in the typed unit, an
    // angle in degrees), for adding and comparing; false when the two can't be combined.
    bool match(Quantity& a, Quantity& b)
    {
        auto adopt = [this](Quantity& plain, const Quantity& other) {
            if (other.length == 1 && other.angle == 0) {
                plain.value *= m_scale;
                plain.length = 1;
                return true;
            }
            if (other.length == 0 && other.angle == 1) {
                plain.angle = 1;
                return true;
            }
            return false;
        };
        if (a.length == b.length && a.angle == b.angle)
            return true;
        if (a.plain() ? adopt(a, b) : b.plain() && adopt(b, a))
            return true;
        fail(QT_TRANSLATE_NOOP("Model", "Units don't match"));
        return false;
    }

    std::optional<Quantity> sum()
    {
        auto left = product();
        while (left && (isOp("+") || isOp("-"))) {
            const bool minus = peek().text == "-";
            ++m_pos;
            auto right = product();
            if (!right || !match(*left, *right))
                return std::nullopt;
            left->value += minus ? -right->value : right->value;
        }
        return left;
    }

    std::optional<Quantity> product()
    {
        auto left = unary();
        while (left && (isOp("*") || isOp("/"))) {
            const bool divide = peek().text == "/";
            ++m_pos;
            auto right = unary();
            if (!right)
                return std::nullopt;
            if (divide) {
                if (right->value == 0.0)
                    return fail(QT_TRANSLATE_NOOP("Model", "Division by zero"));
                left->value /= right->value;
                left->length -= right->length;
                left->angle -= right->angle;
            } else {
                left->value *= right->value;
                left->length += right->length;
                left->angle += right->angle;
            }
        }
        return left;
    }

    std::optional<Quantity> unary()
    {
        if (isOp("-") || isOp("+")) {
            const bool minus = peek().text == "-";
            ++m_pos;
            if (++m_depth > kMaxDepth)
                return fail(QT_TRANSLATE_NOOP("Model", "Too deeply nested"));
            auto q = unary();
            --m_depth;
            if (q && minus)
                q->value = -q->value;
            return q;
        }
        return power();
    }

    std::optional<Quantity> power()
    {
        auto base = postfix();
        if (!base || !isOp("^"))
            return base;
        ++m_pos;
        if (++m_depth > kMaxDepth)
            return fail(QT_TRANSLATE_NOOP("Model", "Too deeply nested"));
        auto exponent = unary(); // right to left: 2^3^2 is 2^9
        --m_depth;
        if (!exponent)
            return std::nullopt;
        if (!exponent->plain())
            return fail(QT_TRANSLATE_NOOP("Model", "An exponent can't have a unit"));
        if (!base->plain()) {
            const double n = exponent->value;
            if (n != std::round(n) || std::abs(n) > 12)
                return fail(QT_TRANSLATE_NOOP("Model", "A unit can only be raised to a whole power"));
            base->length *= static_cast<int>(n);
            base->angle *= static_cast<int>(n);
        }
        base->value = std::pow(base->value, exponent->value);
        return base;
    }

    // A value, then units it is in: `2 mm`, `(a + b) in`, `45°`.
    std::optional<Quantity> postfix()
    {
        auto q = primary();
        while (q) {
            std::optional<Quantity> unit;
            if (isOp("°"))
                unit = Quantity{1.0, 0, 1};
            else if (peek().kind == Token::Kind::Name)
                unit = unitNamed(peek().text);
            if (!unit)
                break;
            ++m_pos;
            q->value *= unit->value;
            q->length += unit->length;
            q->angle += unit->angle;
        }
        return q;
    }

    std::optional<Quantity> primary()
    {
        const Token& t = peek();
        if (t.kind == Token::Kind::Number) {
            ++m_pos;
            return Quantity{t.number, 0, 0};
        }
        if (isOp("(")) {
            ++m_pos;
            if (++m_depth > kMaxDepth)
                return fail(QT_TRANSLATE_NOOP("Model", "Too deeply nested"));
            auto q = sum();
            --m_depth;
            if (!q)
                return std::nullopt;
            if (!isOp(")"))
                return unexpected();
            ++m_pos;
            return q;
        }
        if (t.kind != Token::Kind::Name)
            return unexpected();
        const std::string name = t.text;
        ++m_pos;
        if (isFunction(name)) {
            if (!isOp("("))
                return fail(QT_TRANSLATE_NOOP("Model", "%1 needs its values in parentheses"), name);
            return call(name);
        }
        if (const auto unit = unitNamed(name))
            return unit; // `mm` alone is one millimetre
        if (name == "pi")
            return Quantity{M_PI, 0, 0};
        if (!m_names)
            return fail(QT_TRANSLATE_NOOP("Model", "Unknown name: %1"), name);
        ExpressionError error;
        auto q = m_names(name, error);
        if (!q) {
            if (error.message.empty())
                error = {QT_TRANSLATE_NOOP("Model", "Unknown name: %1"), name};
            if (m_error.message.empty())
                m_error = error;
            return std::nullopt;
        }
        return q;
    }

    std::optional<Quantity> call(const std::string& name)
    {
        ++m_pos; // (
        if (++m_depth > kMaxDepth)
            return fail(QT_TRANSLATE_NOOP("Model", "Too deeply nested"));
        std::vector<Quantity> args;
        if (!isOp(")")) {
            while (true) {
                auto q = sum();
                if (!q)
                    return std::nullopt;
                args.push_back(*q);
                if (isOp(";") || isOp(",")) {
                    ++m_pos;
                    continue;
                }
                break;
            }
        }
        --m_depth;
        if (!isOp(")"))
            return unexpected();
        ++m_pos;

        if (name == "min" || name == "max") {
            if (args.empty())
                return fail(QT_TRANSLATE_NOOP("Model", "%1 needs values"), name);
            Quantity best = args[0];
            for (std::size_t i = 1; i < args.size(); ++i) {
                if (!match(best, args[i]))
                    return std::nullopt;
                if (name == "min" ? args[i].value < best.value : args[i].value > best.value)
                    best.value = args[i].value;
            }
            return best;
        }
        if (args.size() != 1)
            return fail(QT_TRANSLATE_NOOP("Model", "%1 takes one value"), name);
        Quantity q = args[0];
        if (name == "sqrt") {
            if (q.length % 2 != 0 || q.angle % 2 != 0)
                return fail(QT_TRANSLATE_NOOP("Model", "Square root of a unit that isn't squared"));
            if (q.value < 0.0)
                return fail(QT_TRANSLATE_NOOP("Model", "Square root of a negative value"));
            return Quantity{std::sqrt(q.value), q.length / 2, q.angle / 2};
        }
        if (name == "abs")
            return Quantity{std::abs(q.value), q.length, q.angle};
        if (name == "round" || name == "floor" || name == "ceil") {
            const double v = name == "round" ? std::round(q.value) : name == "floor" ? std::floor(q.value) : std::ceil(q.value);
            return Quantity{v, q.length, q.angle};
        }
        if (name == "sin" || name == "cos" || name == "tan") {
            if (!(q.plain() || (q.length == 0 && q.angle == 1)))
                return fail(QT_TRANSLATE_NOOP("Model", "%1 takes an angle"), name);
            const double r = q.value / kDegPerRad;
            return Quantity{name == "sin" ? std::sin(r) : name == "cos" ? std::cos(r) : std::tan(r), 0, 0};
        }
        // asin, acos, atan
        if (!q.plain())
            return fail(QT_TRANSLATE_NOOP("Model", "%1 takes a plain number"), name);
        if (name != "atan" && std::abs(q.value) > 1.0)
            return fail(QT_TRANSLATE_NOOP("Model", "%1 takes a value from -1 to 1"), name);
        const double r = name == "asin" ? std::asin(q.value) : name == "acos" ? std::acos(q.value) : std::atan(q.value);
        return Quantity{r * kDegPerRad, 0, 1};
    }

    const std::vector<Token>& m_tokens;
    std::size_t m_pos = 0;
    double m_scale;
    const NameLookup& m_names;
    ExpressionError& m_error;
    int m_depth = 0;
};

} // namespace

std::optional<Quantity> evaluateQuantity(const std::string& text, LengthUnit unit, const NameLookup& names,
                                         ExpressionError& error)
{
    error = {};
    std::vector<Token> tokens;
    if (!tokenize(text, tokens, error))
        return std::nullopt;
    return Parser(tokens, unit, names, error).parse();
}

std::optional<double> asKind(const Quantity& q, ValueKind kind, LengthUnit unit, ExpressionError& error)
{
    switch (kind) {
    case ValueKind::Number:
        if (q.plain())
            return q.value;
        error = {QT_TRANSLATE_NOOP("Model", "Expected a plain number"), {}};
        return std::nullopt;
    case ValueKind::Length:
        if (q.plain())
            return q.value * (unit == LengthUnit::Inch ? kMmPerInch : 1.0);
        if (q.length == 1 && q.angle == 0)
            return q.value;
        error = {QT_TRANSLATE_NOOP("Model", "Expected a length"), {}};
        return std::nullopt;
    case ValueKind::Angle:
        if (q.plain() || (q.length == 0 && q.angle == 1))
            return q.value;
        error = {QT_TRANSLATE_NOOP("Model", "Expected an angle"), {}};
        return std::nullopt;
    }
    return std::nullopt;
}

EvalResult evaluate(const std::string& text, ValueKind kind, LengthUnit unit, const NameLookup& names)
{
    EvalResult result;
    if (const auto q = evaluateQuantity(text, unit, names, result.error))
        result.value = asKind(*q, kind, unit, result.error);
    return result;
}

std::optional<Definition> definitionIn(const std::string& text)
{
    const auto equals = text.find('=');
    if (equals == std::string::npos || text.find('=', equals + 1) != std::string::npos)
        return std::nullopt;
    std::vector<Token> tokens;
    ExpressionError error;
    const std::string name = text.substr(0, equals);
    if (!tokenize(name, tokens, error) || tokens.size() != 2 || tokens[0].kind != Token::Kind::Name)
        return std::nullopt;
    std::string rest = text.substr(equals + 1);
    rest.erase(0, rest.find_first_not_of(" \t"));
    rest.erase(rest.find_last_not_of(" \t") + 1);
    return Definition{tokens[0].text, rest};
}

std::string numberText(double value)
{
    if (!std::isfinite(value))
        return "0";
    char buffer[400]; // fixed notation of the largest double fits
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 6);
    std::string s(buffer, ec == std::errc() ? end : buffer);
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}

bool isPlainNumber(const std::string& text)
{
    std::size_t i = 0, n = text.size();
    while (i < n && text[i] == ' ')
        ++i;
    while (n > i && text[n - 1] == ' ')
        --n;
    if (i < n && (text[i] == '-' || text[i] == '+'))
        ++i;
    bool digits = false, separator = false;
    for (; i < n; ++i) {
        if (isDigit(text[i])) {
            digits = true;
        } else if ((text[i] == '.' || text[i] == ',') && !separator) {
            separator = true;
        } else {
            return false;
        }
    }
    return digits;
}

std::vector<std::string> namesIn(const std::string& text)
{
    std::vector<Token> tokens;
    ExpressionError error;
    if (!tokenize(text, tokens, error))
        return {};
    std::vector<std::string> names;
    for (const Token& t : tokens) {
        if (t.kind == Token::Kind::Name && !isReserved(t.text)
            && std::find(names.begin(), names.end(), t.text) == names.end())
            names.push_back(t.text);
    }
    return names;
}

std::string renameInExpression(const std::string& text, const std::string& from, const std::string& to)
{
    std::vector<Token> tokens;
    ExpressionError error;
    if (!tokenize(text, tokens, error))
        return text;
    std::string out = text;
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (it->kind == Token::Kind::Name && it->text == from)
            out.replace(it->begin, it->end - it->begin, to);
    }
    return out;
}

bool isValidName(const std::string& name, ExpressionError* error)
{
    auto no = [error](const char* message, const std::string& detail) {
        if (error)
            *error = {message, detail};
        return false;
    };
    if (name.empty())
        return no(QT_TRANSLATE_NOOP("Model", "A parameter needs a name"), name);
    if (name.size() > kMaxNameLength)
        return no(QT_TRANSLATE_NOOP("Model", "The name is too long"), name);
    std::vector<Token> tokens;
    ExpressionError ignored;
    if (!tokenize(name, tokens, ignored) || tokens.size() != 2 || tokens[0].kind != Token::Kind::Name
        || tokens[0].text != name)
        return no(QT_TRANSLATE_NOOP("Model", "A name is a letter, then letters, digits or _: %1"), name);
    if (isReserved(name))
        return no(QT_TRANSLATE_NOOP("Model", "%1 is a unit or a function"), name);
    return true;
}
