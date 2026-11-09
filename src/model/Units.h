#pragma once

// Lengths are millimetres everywhere inside; the interface can show and take inches.
enum class LengthUnit { Millimetre, Inch };

LengthUnit lengthUnit();
void setLengthUnit(LengthUnit unit);

// Millimetres to the unit shown, and back.
double toDisplayLength(double mm);
double fromDisplayLength(double value);
// "mm" or "in".
const char* lengthSymbol();
// Decimals that read well in the unit shown (2 for mm, 3 for inches).
int lengthDecimals();
