#include "model/Units.h"

namespace {
LengthUnit g_unit = LengthUnit::Millimetre;
constexpr double kMmPerInch = 25.4;
} // namespace

LengthUnit lengthUnit()
{
    return g_unit;
}

void setLengthUnit(LengthUnit unit)
{
    g_unit = unit;
}

double toDisplayLength(double mm)
{
    return g_unit == LengthUnit::Inch ? mm / kMmPerInch : mm;
}

double fromDisplayLength(double value)
{
    return g_unit == LengthUnit::Inch ? value * kMmPerInch : value;
}

const char* lengthSymbol()
{
    return g_unit == LengthUnit::Inch ? "in" : "mm";
}

int lengthDecimals()
{
    return g_unit == LengthUnit::Inch ? 3 : 2;
}
