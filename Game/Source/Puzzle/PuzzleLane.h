#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "Puzzle/PuzzleTypes.h"

// One puzzle on a lane root: what it asks for, and how the current run is going.
// The PuzzleDirector writes the run state; everything else reads it.
class PuzzleLane final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    PuzzleKind kind = PuzzleKind::KickerJump;
    Handle<Canape::GameObject> payload{};
    // Relative to the lane root; the goal is met when the payload overlaps this box.
    Canape::Vector3 goalCenter{};
    Canape::Vector3 goalHalfExtent{};
    // The goal pieces show the idle material until the goal is met, then the solved one.
    GameObjectHandles goalPieces;
    std::string goalIdleMaterial;
    std::string goalSolvedMaterial;

    // Run state, not registered.
    PuzzleMode mode = PuzzleMode::Solution;
    std::uint32_t tick = 0;
    bool solved = false;
    std::uint32_t solvedAt = 0;
    bool failed = false;
    bool hasVerdict = false;
    bool goalShownSolved = false;

public:
    //***** Authoring
    PuzzleKind GetKind() const { return kind; }
    void SetKind(PuzzleKind puzzleKind) { kind = puzzleKind; }
    const Handle<Canape::GameObject>& GetPayload() const { return payload; }
    void SetPayload(const Handle<Canape::GameObject>& payloadObject) { payload = payloadObject; }
    const Canape::Vector3& GetGoalCenter() const { return goalCenter; }
    const Canape::Vector3& GetGoalHalfExtent() const { return goalHalfExtent; }
    void SetGoalZone(const Canape::Vector3& center, const Canape::Vector3& halfExtent)
    {
        goalCenter = center;
        goalHalfExtent = halfExtent;
    }
    const GameObjectHandles& GetGoalPieces() const { return goalPieces; }
    void AddGoalPiece(const Handle<Canape::GameObject>& piece) { goalPieces.push_back(piece); }
    const std::string& GetGoalIdleMaterial() const { return goalIdleMaterial; }
    const std::string& GetGoalSolvedMaterial() const { return goalSolvedMaterial; }
    void SetGoalMaterials(std::string idle, std::string solvedLook)
    {
        goalIdleMaterial = std::move(idle);
        goalSolvedMaterial = std::move(solvedLook);
    }

    // A lane without a payload is an empty slot and is skipped.
    bool HasContent() const { return payload.index >= 0; }

    //***** Run state
    PuzzleMode GetMode() const { return mode; }
    std::uint32_t GetTick() const { return tick; }
    bool IsSolved() const { return solved; }
    std::uint32_t GetSolvedAt() const { return solvedAt; }
    bool HasFailed() const { return failed; }
    // True once the run reached its horizon and the goal was judged for the last time; solvedAt is final.
    bool HasVerdict() const { return hasVerdict; }
    bool IsGoalShownSolved() const { return goalShownSolved; }

private:
    friend class PuzzleDirector;

    void reset(PuzzleMode startMode);
    void advanceTick() { ++tick; }
    void solve();
    void fail() { failed = true; }
    void judge() { hasVerdict = true; }
    void showGoal(bool shown);
};
