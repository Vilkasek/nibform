// Palm rejection (which touches are ignored around pen use) and multi-finger taps.

#include "Check.h"

#include "input/PalmRejection.h"
#include "input/TapGesture.h"

namespace {

void testFingersWorkWithoutAPen()
{
    PalmRejection palm;
    CHECK(!palm.rejectTouch(1000, 0.0));
    CHECK(!palm.rejectTouch(1000, 9.0)); // a fingertip
}

void testTouchesIgnoredAroundPenUse()
{
    PalmRejection palm;
    palm.penSeen(10'000);
    CHECK(palm.rejectTouch(10'000, 0.0));
    CHECK(palm.rejectTouch(10'000 + PalmRejection::kPenGraceMs - 1, 9.0));
    CHECK(!palm.rejectTouch(10'000 + PalmRejection::kPenGraceMs + 1, 9.0)); // pen put away
}

void testProximityHoldsUntilThePenLeaves()
{
    PalmRejection palm;
    palm.penNear(true, 0);
    CHECK(palm.rejectTouch(60'000, 9.0)); // hovering for a minute: still the pen's turn
    palm.penNear(false, 60'000);
    CHECK(palm.rejectTouch(60'000 + 100, 9.0));
    CHECK(!palm.rejectTouch(60'000 + PalmRejection::kPenGraceMs + 1, 9.0));
}

void testWideContactsAreAPalm()
{
    PalmRejection palm;
    CHECK(palm.rejectTouch(0, 30.0));
    CHECK(!palm.rejectTouch(0, 12.0));
}

void testCanBeTurnedOff()
{
    PalmRejection palm;
    palm.setEnabled(false);
    palm.penNear(true, 0);
    CHECK(!palm.rejectTouch(0, 40.0));
}

void testTwoAndThreeFingerTaps()
{
    TapGesture tap;
    tap.down(1, 100, 100, 0);
    tap.down(2, 200, 100, 20);
    CHECK(tap.up(1, 120) == 0); // one still down
    CHECK(tap.up(2, 140) == 2);

    tap.down(5, 100, 100, 1000);
    tap.down(6, 150, 100, 1010);
    tap.down(7, 200, 100, 1030);
    tap.move(6, 154, 103); // a fingertip wobbles
    CHECK(tap.up(7, 1100) == 0);
    CHECK(tap.up(5, 1110) == 0);
    CHECK(tap.up(6, 1150) == 3);
}

void testPansPinchesAndHoldsAreNoTaps()
{
    TapGesture tap;
    tap.down(1, 100, 100, 0); // one finger orbits, never a tap
    CHECK(tap.up(1, 50) == 0);

    tap.down(1, 100, 100, 1000); // pinch
    tap.down(2, 200, 100, 1000);
    tap.move(2, 260, 100);
    tap.up(1, 1100);
    CHECK(tap.up(2, 1100) == 0);

    tap.down(1, 100, 100, 2000); // held too long
    tap.down(2, 200, 100, 2000);
    tap.up(1, 2000 + TapGesture::kMaxMs + 50);
    CHECK(tap.up(2, 2000 + TapGesture::kMaxMs + 60) == 0);

    tap.down(1, 100, 100, 3000); // the pen took over
    tap.down(2, 200, 100, 3000);
    tap.cancel();
    CHECK(tap.up(1, 3050) == 0);
    CHECK(tap.up(2, 3060) == 0);

    tap.down(1, 100, 100, 4000); // and the next tap counts again
    tap.down(2, 200, 100, 4000);
    tap.up(1, 4050);
    CHECK(tap.up(2, 4060) == 2);
}

} // namespace

int main()
{
    return runTests({
        {"fingers work without a pen", testFingersWorkWithoutAPen},
        {"touches ignored around pen use", testTouchesIgnoredAroundPenUse},
        {"proximity holds until the pen leaves", testProximityHoldsUntilThePenLeaves},
        {"wide contacts are a palm", testWideContactsAreAPalm},
        {"can be turned off", testCanBeTurnedOff},
        {"two- and three-finger taps", testTwoAndThreeFingerTaps},
        {"pans, pinches and holds are no taps", testPansPinchesAndHoldsAreNoTaps},
    });
}
