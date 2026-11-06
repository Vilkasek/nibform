#pragma once

#include <cstdint>
#include <limits>

// Which touches to ignore while the pen is in use: a palm resting on the screen while
// drawing must not orbit or zoom the view. Times are in milliseconds, from any clock.
class PalmRejection
{
public:
    // How long after the pen was last seen touches stay ignored (it hovers a bit before
    // and after strokes, and the hand lands just before the tip does).
    static constexpr std::int64_t kPenGraceMs = 600;
    // Contacts wider than this are a palm or the side of a hand, never a fingertip.
    static constexpr double kPalmMm = 18.0;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool enabled() const { return m_enabled; }

    // The pen hovered, touched or was lifted at `time`.
    void penSeen(std::int64_t time) { m_lastPen = time; }
    // The pen came into / left proximity (when the hardware reports it).
    void penNear(bool near, std::int64_t time);

    // Whether a touch starting at `time` should be ignored; `contactMm` is the contact's
    // larger diameter in millimetres, 0 when the screen doesn't report it.
    bool rejectTouch(std::int64_t time, double contactMm) const;

private:
    bool m_enabled = true;
    bool m_penNear = false;
    std::int64_t m_lastPen = std::numeric_limits<std::int64_t>::min() / 2;
};
