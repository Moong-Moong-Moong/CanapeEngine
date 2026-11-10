#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Puzzle/PuzzleTypes.h"

// A world-space, axis-aligned box.
struct GoalRegion {
    Canape::Vector3 center{};
    Canape::Vector3 halfExtent{};
};

// What a lane must do with its payload: a region its body overlaps, watched every fixed tick. A goal latches once its
// condition was met in the run; the PuzzleDirector reads the latch at the horizon. The pieces, the objects right below
// this one, show the idle material while the payload is outside the region right now and the solved material while it
// is inside.
class PuzzleGoal: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    Handle<Canape::GameObject> payload{};
    std::string idleMaterial;
    std::string solvedMaterial;

    // Run state, not registered.
    bool satisfied = false;
    bool shownInside = false;
    bool lookLogged = false;

public:
    //***** Authoring
    const Handle<Canape::GameObject>& GetPayload() const { return payload; }
    const std::string& GetIdleMaterial() const { return idleMaterial; }
    const std::string& GetSolvedMaterial() const { return solvedMaterial; }

    //***** Queries
    std::vector<Canape::GameObject*> GetPieces() const;
    // Empty when the goal cannot be measured; the goal logs that once.
    virtual std::optional<GoalRegion> GetRegion() const = 0;
    // Whether the payload's body overlaps the region right now.
    bool HoldsPayload() const;
    // True once the condition was met in this run.
    bool IsSatisfied() const { return satisfied; }
    bool IsShownInside() const { return shownInside; }

protected:
    // Every fixed tick, with whether the payload is inside now; true when the condition is met.
    virtual bool Observe(bool inside) = 0;

    void FixedUpdate() override;

private:
    void show(bool inside);
};
