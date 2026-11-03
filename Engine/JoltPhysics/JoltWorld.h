#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/PhysicsTypes.h"

namespace Canape
{
    using PhysicsBodyIds = std::vector<PhysicsBodyId>;

    // One Jolt PhysicsSystem. Single threaded; the runtime must outlive the world.
    // Failures are values: an invalid body id, or a PhysicsStepError from Step.
    class JoltWorld
    {
    public:
        explicit JoltWorld(JoltRuntime& runtime, const PhysicsWorldDesc& desc = {});
        ~JoltWorld();

        JoltWorld(const JoltWorld&) = delete;
        JoltWorld& operator=(const JoltWorld&) = delete;

        PhysicsBodyId CreateBody(const PhysicsBodyDesc& desc);
        void DestroyBody(PhysicsBodyId id);

        PhysicsStepError Step(float dt);

        void AddImpulse(PhysicsBodyId id, const Vector3& impulse);
        void AddImpulseAt(PhysicsBodyId id, const Vector3& impulse, const Vector3& worldPoint);

        bool IsValid(PhysicsBodyId id) const;
        PhysicsPose GetPose(PhysicsBodyId id) const;
        Vector3 GetLinearVelocity(PhysicsBodyId id) const;
        Vector3 GetAngularVelocity(PhysicsBodyId id) const;
        float GetMass(PhysicsBodyId id) const;
        bool IsAwake(PhysicsBodyId id) const;
        PhysicsMotion GetMotion(PhysicsBodyId id) const;

        std::size_t GetBodyCount() const;
        PhysicsWorldStats GetStats() const;

        // Moving bodies whose shape overlaps the box, sleeping ones included, in creation order.
        PhysicsBodyIds QueryBox(const Vector3& center, const Vector3& halfExtent) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
