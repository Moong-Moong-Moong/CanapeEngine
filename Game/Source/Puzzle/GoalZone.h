#pragma once

#include <optional>

#include "Puzzle/PuzzleGoal.h"

// A box in the air: size times the Transform scale, centered on the Transform position, axis aligned (a rotation is
// logged once and ignored). The zone is met once the payload touched it, in flight or at rest.
class GoalZone final: public PuzzleGoal {
    RTTR_ENABLE(PuzzleGoal)
    RTTR_REGISTRATION_FRIEND

private:
    // Meters before the Transform scale.
    Canape::Vector3 size = Canape::Vector3::One;

    // Run state, not registered.
    mutable bool sizeLogged = false;
    mutable bool rotationLogged = false;

public:
    const Canape::Vector3& GetSize() const { return size; }

    std::optional<GoalRegion> GetRegion() const override;

protected:
    bool Observe(bool inside) override { return inside; }
};
