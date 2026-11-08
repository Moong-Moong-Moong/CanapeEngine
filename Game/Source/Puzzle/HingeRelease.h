#pragma once

#include "Puzzle/PuzzleTypes.h"

// On the arm a lane lets go (the object with the HingeComponent): the lane tick of the release for each mode, -1 for
// never. The PuzzleDirector fires it once per run, at that tick before the tracks and the step, while the hinge holds.
// Firing hands every rider its own body at the arm's velocity there and moves it next to the arm, then releases the
// hinge, drops the arm's body and hides the arm. A rider is an object below the arm whose disabled RigidBody rode
// along in the arm's body; its mass must be above 0 to keep its speed.
class HingeRelease final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    int solutionTick = -1;
    int controlTick = -1;

    // Run state, not registered.
    bool applied = false;

public:
    int GetTick(PuzzleMode mode) const
    {
        return mode == PuzzleMode::Solution ?
            solutionTick :
            controlTick;
    }
    // True once the arm was let go in this run.
    bool IsApplied() const { return applied; }

private:
    friend class PuzzleDirector;

    // The hinge on this object, which must hold.
    void fire(Canape::HingeComponent& hinge);
    void detachRiders(const Canape::RigidBodyComponent& arm);
    void hideArm();
};
