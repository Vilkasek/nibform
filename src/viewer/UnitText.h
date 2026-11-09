#pragma once

#include "model/Units.h"

#include <QString>

// A length (in millimetres) as the interface shows it: in the current unit, with its
// symbol unless `unit` is false.
inline QString lengthText(double mm, bool unit = true)
{
    const QString number = QString::number(toDisplayLength(mm), 'f', lengthDecimals());
    return unit ? number + QLatin1Char(' ') + QString::fromLatin1(lengthSymbol()) : number;
}

// The same, without trailing zeros (history entries: "12 mm", "0.5 in").
inline QString lengthShort(double mm, bool unit = true)
{
    QString number = QString::number(toDisplayLength(mm), 'f', lengthDecimals() + 1);
    while (number.contains(QLatin1Char('.')) && (number.endsWith(QLatin1Char('0')) || number.endsWith(QLatin1Char('.'))))
        number.chop(1);
    return unit ? number + QLatin1Char(' ') + QString::fromLatin1(lengthSymbol()) : number;
}
