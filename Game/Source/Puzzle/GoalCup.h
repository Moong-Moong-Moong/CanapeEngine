#pragma once

#include <cstdint>
#include <optional>

#include "Puzzle/PuzzleGoal.h"

// Consecutive ticks inside the cup that meet it: one second.
inline constexpr std::uint32_t CupDwellTicks = 60;

// A cup of static box pieces under this object: a floor and a wall on each of its four sides. The region lies between
// the walls, from the floor top up to half the height of the lowest wall. The cup is met once the payload stayed
// inside for CupDwellTicks in a row.
class GoalCup final: public PuzzleGoal {
    RTTR_ENABLE(PuzzleGoal)

private:
    // Run state, not registered.
    std::uint32_t insideTicks = 0;
    mutable bool shapeLogged = false;
    mutable bool rotationLogged = false;

public:
    std::optional<GoalRegion> GetRegion() const override;

protected:
    bool Observe(bool inside) override;

private:
    void logShape(const char* why) const;
};
