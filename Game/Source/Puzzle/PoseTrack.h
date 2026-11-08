#pragma once

#include <cstdint>
#include <vector>

#include "Math/Scalar.h"
#include "Puzzle/PuzzleTypes.h"

// The pose a track holds from `tick` on, reached from the previous key over the ticks between them.
struct PoseKey {
    std::uint32_t tick = 0;
    Canape::PhysicsPose pose{};
    Canape::Easing easing = Canape::Easing::Smooth;
};

using PoseKeys = std::vector<PoseKey>;

// Keys rise in tick (a key that does not rise is ignored). The start pose is an implicit key at tick 0;
// after the last key the pose holds.
Canape::PhysicsPose EvaluatePoseKeys(const PoseKeys& keys, const Canape::PhysicsPose& start, std::uint32_t tick);

// Drives a kinematic piece along keys that are relative to its parent (the lane), one key list per mode.
class PoseTrack final: public Canape::ScriptComponent {
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

private:
    PoseKeys solutionKeys;
    PoseKeys controlKeys;
    // The local pose when the scene started; not registered.
    Canape::PhysicsPose startPose{};

public:
    const PoseKeys& GetKeys(PuzzleMode mode) const
    {
        return mode == PuzzleMode::Solution ?
            solutionKeys :
            controlKeys;
    }
    bool HasKeys(PuzzleMode mode) const { return !GetKeys(mode).empty(); }

    // Pose relative to the parent at a lane tick.
    Canape::PhysicsPose PoseAt(PuzzleMode mode, std::uint32_t tick) const;
    // The same pose in world space.
    Canape::PhysicsPose WorldPoseAt(PuzzleMode mode, std::uint32_t tick) const;

protected:
    void Start() override;
};
