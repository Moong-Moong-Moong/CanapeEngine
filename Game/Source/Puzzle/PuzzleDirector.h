#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Puzzle/GoalCup.h"
#include "Puzzle/HingeRelease.h"
#include "Puzzle/PoseTrack.h"
#include "Puzzle/PuzzleLane.h"

// Runs every lane on the fixed ticks, judges the cups at the horizon and reloads the scene in the other mode after each run.
// The lanes are the objects of the scene with a PuzzleLane; a lane without a cup is an empty slot and is skipped.
// R and C reload the scene in Solution and Control mode; they are read in Update. L logs the state of every lane.
// A reload restores the scene as it was when the run began (saved to a temp file at the first Start), not the file on disk.
class PuzzleDirector final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    struct LaneEntry {
        PuzzleLane* lane = nullptr;
        std::vector<PoseTrack*> tracks;
        std::vector<HingeRelease*> releases;
        std::vector<GoalCup*> cups;
    };

    using LaneEntries = std::vector<LaneEntry>;

private:
    PuzzleMode startMode = PuzzleMode::Solution;

    // Run state, not registered.
    bool restarted = false;
    std::string snapshotPath;
    bool snapshotHandedOver = false;
    std::uint32_t restTicks = 0;
    // The pose of the fly camera of the scene being left; the new camera takes it over once it has started.
    std::optional<Canape::PhysicsPose> carriedCamera;

public:
    PuzzleDirector() = default;
    ~PuzzleDirector() override;
    PuzzleDirector(const PuzzleDirector&) = delete;
    PuzzleDirector& operator=(const PuzzleDirector&) = delete;
    PuzzleDirector(PuzzleDirector&&) = delete;
    PuzzleDirector& operator=(PuzzleDirector&&) = delete;

    // The mode every lane runs in; the scene reload passes it on to the new scene's director.
    PuzzleMode GetStartMode() const { return startMode; }
    void SetStartMode(PuzzleMode mode) { startMode = mode; }
    // Fixed ticks spent at rest since every lane with content reached its horizon.
    std::uint32_t GetRestTicks() const { return restTicks; }

    // The start-time snapshot of the scene; the director that reloads from it hands the path to the next one.
    const std::string& GetSnapshotPath() const { return snapshotPath; }
    void SetSnapshotPath(std::string path) { snapshotPath = std::move(path); }

protected:
    void Start() override;
    void FixedUpdate() override;
    void Update() override;

private:
    LaneEntries gatherLanes() const;
    void saveSnapshot();
    void restart(PuzzleMode mode);
    void carryCamera();
    void releaseHinges(const LaneEntry& entry) const;
    void driveTracks(const LaneEntry& entry) const;
    void judge(const LaneEntry& entry) const;
    void logLaneStates() const;
};
