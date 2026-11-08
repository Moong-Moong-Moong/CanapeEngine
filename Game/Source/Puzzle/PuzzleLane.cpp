#include "Puzzle/PuzzleLane.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<PuzzleLane>("PuzzleLane")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

void PuzzleLane::reset(PuzzleMode startMode)
{
    mode = startMode;
    tick = 0;
    solved = false;
    failed = false;
    hasVerdict = false;
}

void PuzzleLane::judge(bool solvedNow)
{
    solved = solvedNow;
    hasVerdict = true;
}
