#include "input/PalmRejection.h"

void PalmRejection::penNear(bool near, std::int64_t time)
{
    m_penNear = near;
    m_lastPen = time;
}

bool PalmRejection::rejectTouch(std::int64_t time, double contactMm) const
{
    if (!m_enabled)
        return false;
    if (m_penNear || time - m_lastPen < kPenGraceMs)
        return true;
    return contactMm > kPalmMm;
}
