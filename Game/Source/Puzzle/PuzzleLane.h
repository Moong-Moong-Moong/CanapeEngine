#pragma once

#include <cstdint>

#include "Puzzle/PuzzleTypes.h"

// The run state of one puzzle on a lane root; the cups it must fill and the tracks it drives are the root's children.
// The PuzzleDirector writes the state; everything else reads it.
class PuzzleLane final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    // Run state, not registered.
    PuzzleMode mode = PuzzleMode::Solution;
    std::uint32_t tick = 0;
    bool solved = false;
    bool failed = false;
    bool hasVerdict = false;

public:
    PuzzleMode GetMode() const { return mode; }
    std::uint32_t GetTick() const { return tick; }
    bool IsSolved() const { return solved; }
    bool HasFailed() const { return failed; }
    // True once the run reached its horizon and was judged; the verdict is final.
    bool HasVerdict() const { return hasVerdict; }

private:
    friend class PuzzleDirector;

    void reset(PuzzleMode startMode);
    void advanceTick() { ++tick; }
    void fail() { failed = true; }
    void judge(bool solvedNow);
};
