#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Puzzle/PuzzleTypes.h"

// The inside of a cup: a world-space, axis-aligned box.
struct CupRegion {
    Canape::Vector3 center{};
    Canape::Vector3 halfExtent{};
};

// A cup of static box pieces under this object: a floor and a wall on each of its four sides. The payload is held
// when its body overlaps the inside, between the walls and up to half the height of the lowest one above the floor.
// The PuzzleDirector asks at the horizon; every tick the cup shows whether the payload is inside right now.
class GoalCup final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    Handle<Canape::GameObject> payload{};
    // Every piece shows the idle material while the payload is outside, the solved one while it is inside.
    std::string idleMaterial;
    std::string solvedMaterial;

    // Run state, not registered.
    bool shownSolved = false;
    bool lookLogged = false;
    mutable bool shapeLogged = false;
    mutable bool rotationLogged = false;

public:
    //***** Authoring
    const Handle<Canape::GameObject>& GetPayload() const { return payload; }
    const std::string& GetIdleMaterial() const { return idleMaterial; }
    const std::string& GetSolvedMaterial() const { return solvedMaterial; }

    //***** Queries
    // The objects right below this one.
    std::vector<Canape::GameObject*> GetPieces() const;
    // Empty when the pieces do not make a cup; that is logged once.
    std::optional<CupRegion> GetRegion() const;
    bool HoldsPayload() const;
    bool IsShownSolved() const { return shownSolved; }

protected:
    void FixedUpdate() override;

private:
    void show(bool solved);
    void logShape(const char* why) const;
};
