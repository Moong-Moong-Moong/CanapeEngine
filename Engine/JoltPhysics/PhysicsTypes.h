#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace Canape
{
    enum class PhysicsMotion : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class PhysicsShapeType : std::uint8_t
    {
        Box,
        Sphere,
    };

    // Pose of a body origin (never the centre of mass); a compound's origin is its pivot.
    struct PhysicsPose
    {
        Vector3 position{};
        Quaternion rotation{};
    };

    struct PhysicsShapePart
    {
        PhysicsShapeType type = PhysicsShapeType::Box;
        Vector3 halfExtent{ 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        PhysicsPose localPose{};
        std::optional<float> friction{};
    };

    // One part at the identity local pose with no friction override is a plain shape; anything else is a compound.
    struct PhysicsBodyDesc
    {
        std::vector<PhysicsShapePart> parts{};
        PhysicsPose pose{};
        PhysicsMotion motion = PhysicsMotion::Dynamic;
        float mass = 0.0f; // 0 means density 1000 kg/m^3
        float friction = 0.2f;
        float restitution = 0.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
    };

    struct PhysicsWorldDesc
    {
        Vector3 gravity{ 0.0f, -9.81f, 0.0f };
        std::uint32_t maxBodies = 1024;
        std::uint32_t maxBodyPairs = 1024;
        std::uint32_t maxContactConstraints = 1024;
    };

    // Names a body of one world; stale and foreign ids are invalid and every query on them returns defaults.
    struct PhysicsBodyId
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        std::uint32_t world = 0;

        constexpr bool IsValid() const { return world != 0; }
        constexpr bool operator==(const PhysicsBodyId&) const = default;
    };

    struct PhysicsWorldStats
    {
        std::size_t bodyCount = 0;
        std::size_t activeBodies = 0;
    };

    // Bit flags; Step reports every condition that held during the update.
    enum class PhysicsStepError : std::uint8_t
    {
        None = 0,
        ManifoldCacheFull = 1,
        BodyPairCacheFull = 2,
        ContactConstraintsFull = 4,
        InvalidTimeStep = 8,
        NoRuntime = 16,
    };

    constexpr PhysicsStepError operator|(PhysicsStepError a, PhysicsStepError b)
    {
        return static_cast<PhysicsStepError>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    constexpr PhysicsStepError operator&(PhysicsStepError a, PhysicsStepError b)
    {
        return static_cast<PhysicsStepError>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    constexpr bool StepFailed(PhysicsStepError error) { return error != PhysicsStepError::None; }

    constexpr bool HasStepError(PhysicsStepError error, PhysicsStepError flag) { return (error & flag) != PhysicsStepError::None; }
}
