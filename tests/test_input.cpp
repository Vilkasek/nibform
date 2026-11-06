// Palm rejection: which touches are ignored around pen use.

#include "Check.h"

#include "input/PalmRejection.h"

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

} // namespace

int main()
{
    return runTests({
        {"fingers work without a pen", testFingersWorkWithoutAPen},
        {"touches ignored around pen use", testTouchesIgnoredAroundPenUse},
        {"proximity holds until the pen leaves", testProximityHoldsUntilThePenLeaves},
        {"wide contacts are a palm", testWideContactsAreAPalm},
        {"can be turned off", testCanBeTurnedOff},
    });
}
